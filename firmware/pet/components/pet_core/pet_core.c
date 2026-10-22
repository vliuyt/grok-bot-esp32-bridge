#include "pet_core.h"
#include <math.h>
#include <string.h>

static uint32_t random_next(pet_state_t *s) {
    uint32_t x=s->seed;
    x^=x<<13; x^=x>>17; x^=x<<5;
    s->seed=x ? x : 0x72599a31u;
    return s->seed;
}
void pet_note_activity(pet_state_t *s,uint64_t now) {
    s->life=PET_LIFE_AWAKE;
    s->last_input_ms=now;
}
float pet_warmth(const pet_state_t *s,uint64_t now){
    if(!(s->warmth>0))return 0;
    uint64_t elapsed=now>s->warmth_at_ms?now-s->warmth_at_ms:0;
    if(elapsed>=60000)return 0;
    return fminf(s->warmth,100)*(1-(float)elapsed/60000);
}
static void warm(pet_state_t *s,uint64_t now,float amount){
    s->warmth=fminf(100,pet_warmth(s,now)+amount);s->warmth_at_ms=now;
}
int pet_contact_value(pet_contact_phase_t phase,int x,int y){
    if(phase<PET_CONTACT_DOWN||phase>PET_CONTACT_CANCEL||x<0||x>465||y<0||y>465)return -1;
    return ((int)phase<<18)|(x<<9)|y;
}
static uint32_t contact_step(pet_state_t *s,pet_event_t e){
    if(e.value<0||((unsigned)e.value>>21))return PET_FX_NONE;
    int phase=e.value>>18,x=(e.value>>9)&511,y=e.value&511;
    if(pet_contact_value((pet_contact_phase_t)phase,x,y)!=e.value)return PET_FX_NONE;
    pet_contact_t *c=&s->contact;
    if(phase==PET_CONTACT_CANCEL){memset(c,0,sizeof(*c));return PET_FX_NONE;}
    if(phase==PET_CONTACT_DOWN){
        if(c->active)return PET_FX_NONE;
        if(s->life!=PET_LIFE_AWAKE)c->wake_until_ms=e.now_ms+900;
        c->active=c->seen=true;c->stroking=false;c->origin_x=x;c->origin_y=y;
    }else if(!c->active)return PET_FX_NONE;
    c->x=x;c->y=y;c->sample_ms=e.now_ms;
    if(phase==PET_CONTACT_MOVE){
        int dx=x-c->origin_x,dy=y-c->origin_y;
        c->stroking=(dx>=24||dx<=-24)&&dy>-40&&dy<40;
    }
    if(phase==PET_CONTACT_UP){c->active=false;c->until_ms=e.now_ms+700;}
    pet_note_activity(s,e.now_ms);
    return PET_FX_WAKE;
}
static uint32_t begin(pet_state_t *s,uint64_t now) {
    if(s->expired) return PET_FX_NONE;
    s->turn++;
    if(!s->turn) { s->connection=PET_CONN_OFFLINE; return PET_FX_CANCEL|PET_FX_CONNECT; }
    s->dialogue=PET_DIALOGUE_LISTEN;
    s->entered_ms=now;
    s->pet_until_ms=s->greet_until_ms=s->curious_until_ms=s->confused_until_ms=0;
    s->response_cancelled=false;
    s->response_done=false;
    s->playback_empty=true;
    return PET_FX_CANCEL|PET_FX_START;
}
static bool voice_event(pet_event_kind_t kind) {
    return kind==PET_EV_BUFFERED || kind==PET_EV_PLAYING || kind==PET_EV_RESPONSE_DONE || kind==PET_EV_DRAIN || kind==PET_EV_TURN_STATUS;
}
pet_notice_t pet_notice_from_code(const char *code){
    static const char *const names[]={"","waiting","working","short_input","no_reply","empty_response","unrecognized","response_failed","auth_required","progress"};
    if(code)for(unsigned i=1;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(code,names[i]))return (pet_notice_t)i;
    return PET_NOTICE_NONE;
}
void pet_init(pet_state_t *s,uint64_t now) {
    memset(s,0,sizeof(*s));
    s->now_ms=s->last_input_ms=s->started_ms=now;
    s->seed=(uint32_t)now^0x583cd5b1u;
    s->quiet=true;
    s->task_channel=true;
    s->playback_empty=true;
    s->next_greet_ms=now+300000+(random_next(s)%420001);
}
uint32_t pet_step(pet_state_t *s,pet_event_t e) {
    /* Queued input can arrive just after the main loop processed its timer.
     * Clamp time, but preserve events; identity guards reject stale audio/links. */
    if(e.now_ms<s->now_ms)e.now_ms=s->now_ms;
    s->now_ms=e.now_ms;
    bool connection_auth=e.kind==PET_EV_TURN_STATUS&&e.value==PET_NOTICE_AUTH_REQUIRED&&
        e.generation==s->generation&&e.turn==0&&s->connection!=PET_CONN_READY&&s->dialogue==PET_DIALOGUE_IDLE;
    if(e.kind==PET_EV_TURN_STATUS&&e.value==PET_NOTICE_AUTH_REQUIRED&&e.turn==0&&!connection_auth)return PET_FX_NONE;
    if(voice_event(e.kind) && !connection_auth && (e.generation!=s->generation || e.turn!=s->turn || s->response_cancelled)) return PET_FX_NONE;
    uint32_t fx=PET_FX_NONE;
    switch(e.kind) {
    case PET_EV_CONTACT:fx=contact_step(s,e);break;
    case PET_EV_PRESS:
        if(s->held) break;
        s->held=true; s->expired=false; s->setup=false;
        if(s->connection==PET_CONN_READY||s->notice!=PET_NOTICE_AUTH_REQUIRED)s->notice=PET_NOTICE_NONE;
        pet_note_activity(s,e.now_ms);
        memset(&s->contact,0,sizeof(s->contact));
        fx=PET_FX_WAKE|PET_FX_CANCEL;
        if(s->connection==PET_CONN_READY) fx|=begin(s,e.now_ms);
        else fx|=PET_FX_CONNECT;
        break;
    case PET_EV_RELEASE:
        s->held=false; s->expired=false; pet_note_activity(s,e.now_ms);
        if(s->dialogue==PET_DIALOGUE_LISTEN) {
            fx=PET_FX_STOP;
            if(e.now_ms-s->entered_ms>=100) { s->dialogue=PET_DIALOGUE_WAIT; fx|=PET_FX_SUBMIT; }
            else { s->dialogue=PET_DIALOGUE_IDLE; s->response_cancelled=true; fx|=PET_FX_CANCEL; }
            s->entered_ms=e.now_ms;
        }
        break;
    case PET_EV_CANCEL:
        memset(&s->contact,0,sizeof(s->contact));
        s->notice=PET_NOTICE_NONE;
        fx=PET_FX_CANCEL;
        if(s->dialogue==PET_DIALOGUE_LISTEN) fx|=PET_FX_STOP;
        s->held=false; s->expired=false; s->dialogue=PET_DIALOGUE_IDLE;
        s->response_cancelled=true;
        s->playback_empty=true; s->response_done=false;
        break;
    case PET_EV_LINK:
        if(e.generation<s->generation || e.value<PET_CONN_OFFLINE || e.value>PET_CONN_UNAVAILABLE) break;
        if(e.generation!=s->generation || e.value!=PET_CONN_READY) {
            if(s->notice==PET_NOTICE_WAITING||s->notice==PET_NOTICE_WORKING||
               (s->connection==PET_CONN_READY&&s->dialogue!=PET_DIALOGUE_IDLE&&s->notice!=PET_NOTICE_AUTH_REQUIRED))s->notice=PET_NOTICE_CONNECTION_LOST;
            if(s->dialogue==PET_DIALOGUE_LISTEN) fx|=PET_FX_STOP;
            fx|=PET_FX_CANCEL;
            s->dialogue=PET_DIALOGUE_IDLE; s->playback_empty=true; s->level=0;
        }
        s->generation=e.generation; s->connection=(pet_connection_t)e.value;
        if(e.value==PET_CONN_READY) {
            s->connection_problem_seen=false; s->confused_until_ms=0;s->notice=PET_NOTICE_NONE;
        } else if((e.value==PET_CONN_OFFLINE || e.value==PET_CONN_UNAVAILABLE) && !s->connection_problem_seen) {
            s->connection_problem_seen=true; s->confused_until_ms=e.now_ms+2000;
            s->curious_until_ms=s->pet_until_ms=s->greet_until_ms=0;
        }
        /* Connection recovery cannot resume a partly spoken utterance.
         * A fresh physical press after release owns the next complete turn. */
        break;
    case PET_EV_BUFFERED:
        if(s->dialogue!=PET_DIALOGUE_LISTEN&&s->connection==PET_CONN_READY) {
            s->notice=PET_NOTICE_NONE;
            bool sleeping=s->life!=PET_LIFE_AWAKE;
            pet_note_activity(s,e.now_ms);if(sleeping)fx|=PET_FX_WAKE;
            if(s->dialogue==PET_DIALOGUE_IDLE || (s->playback_empty&&s->response_done)) {
                s->response_done=false;s->dialogue=PET_DIALOGUE_WAIT;s->entered_ms=e.now_ms;
            }
            s->playback_empty=false;
        }
        break;
    case PET_EV_PLAYING:
        if(s->dialogue==PET_DIALOGUE_LISTEN || s->dialogue==PET_DIALOGUE_IDLE) break;
        s->dialogue=PET_DIALOGUE_SPEAK;
        s->playback_empty=false;
        s->level=e.value<0?0:e.value>1000?1000:e.value;
        s->entered_ms=e.now_ms;
        break;
    case PET_EV_RESPONSE_DONE:
        if(s->dialogue==PET_DIALOGUE_LISTEN) break;
        s->response_done=true;
        if(s->playback_empty) { s->dialogue=PET_DIALOGUE_IDLE; s->level=0; pet_note_activity(s,e.now_ms); }
        break;
    case PET_EV_DRAIN:
        s->playback_empty=true;
        if(s->response_done) { s->dialogue=PET_DIALOGUE_IDLE; s->level=0; pet_note_activity(s,e.now_ms); }
        break;
    case PET_EV_PET:
        warm(s,e.now_ms,35);
        pet_note_activity(s,e.now_ms); fx|=PET_FX_WAKE;
        if(s->dialogue==PET_DIALOGUE_IDLE&&e.now_ms>=s->confused_until_ms) {
            s->pet_until_ms=e.now_ms+1800;
            s->curious_until_ms=s->confused_until_ms=s->greet_until_ms=0;
        }
        break;
    case PET_EV_TOUCH_TAP:
        if(s->contact.seen){
            pet_contact_t *c=&s->contact;
            c->taps=c->last_tap_ms&&e.now_ms-c->last_tap_ms<=900?c->taps+1:1;
            if(c->taps==3)c->dodge_at_ms=e.now_ms;
            if(c->taps>3)c->taps=4;
            c->last_tap_ms=e.now_ms;
        }
        warm(s,e.now_ms,10);
        pet_note_activity(s,e.now_ms); fx|=PET_FX_WAKE;
        if(s->dialogue==PET_DIALOGUE_IDLE&&e.now_ms>=s->confused_until_ms) {
            s->curious_until_ms=e.now_ms+2000;
            s->pet_until_ms=s->confused_until_ms=s->greet_until_ms=0;
        }
        break;
    case PET_EV_USB: s->usb=e.value!=0; break;
    case PET_EV_QUIET: s->quiet=e.value!=0; break;
    case PET_EV_MUTED: s->muted=e.value!=0; break;
    case PET_EV_SETUP:
        s->setup=e.value!=0;
        if(s->setup) {
            fx=PET_FX_CANCEL;
            if(s->dialogue==PET_DIALOGUE_LISTEN) fx|=PET_FX_STOP;
            s->held=false; s->dialogue=PET_DIALOGUE_IDLE; s->response_cancelled=true; pet_note_activity(s,e.now_ms);
        }
        break;
    case PET_EV_TASK_CHANNEL: s->task_channel=e.value!=0; break;
    case PET_EV_TURN_STATUS:
        if(s->dialogue==PET_DIALOGUE_LISTEN||e.value<=PET_NOTICE_NONE||e.value>PET_NOTICE_PROGRESS)break;
        if(e.value==PET_NOTICE_PROGRESS){if(s->dialogue==PET_DIALOGUE_WAIT)s->entered_ms=e.now_ms;break;}
        s->notice=(pet_notice_t)e.value;
        if(e.value==PET_NOTICE_WORKING&&s->dialogue==PET_DIALOGUE_WAIT)s->entered_ms=e.now_ms;
        if(e.value>=PET_NOTICE_SHORT_INPUT){
            s->dialogue=PET_DIALOGUE_IDLE;s->response_cancelled=true;s->playback_empty=true;s->response_done=false;s->level=0;
            s->confused_until_ms=e.now_ms+2000;
            fx|=PET_FX_CANCEL;
            if(!connection_auth)pet_note_activity(s,e.now_ms);
        }
        break;
    case PET_EV_CLOCK:
        if(e.value<0 || e.value>=1440 || !e.day) break;
        if(e.day>s->day) { s->day=e.day; s->greetings_today=0; }
        s->minute=e.value; s->clock_ms=e.now_ms; s->clock_valid=true;
        break;
    case PET_EV_TICK: {
        if(s->contact.active&&e.now_ms-s->contact.sample_ms>=1000)memset(&s->contact,0,sizeof(s->contact));
        if(s->dialogue==PET_DIALOGUE_LISTEN && e.now_ms-s->entered_ms>=55000) {
            s->dialogue=PET_DIALOGUE_IDLE; s->expired=true; s->response_cancelled=true;
            fx|=PET_FX_STOP|PET_FX_CANCEL; pet_note_activity(s,e.now_ms);
        }
        if(s->dialogue==PET_DIALOGUE_WAIT && e.now_ms-s->entered_ms>=60000) {
            s->dialogue=PET_DIALOGUE_IDLE; s->response_cancelled=true;
            s->confused_until_ms=e.now_ms+2000;
            s->notice=PET_NOTICE_NO_REPLY;
            fx|=PET_FX_CANCEL; pet_note_activity(s,e.now_ms);
        }
        if(s->dialogue==PET_DIALOGUE_IDLE && !s->held && !s->setup) {
            uint64_t idle=e.now_ms-s->last_input_ms;
            s->life=idle>=(s->usb?1800000u:120000u)?PET_LIFE_SLEEP:
                idle>=(s->usb?900000u:90000u)?PET_LIFE_DROWSY:PET_LIFE_AWAKE;
            int minute=(s->minute+(int)((e.now_ms-s->clock_ms)/60000%1440))%1440;
            bool can_greet=s->life==PET_LIFE_AWAKE && s->clock_valid && !s->muted &&
                minute>=480 && minute<1320 && idle>=6000 && s->greetings_today<3;
            if(can_greet && e.now_ms>=s->next_greet_ms &&
               (!s->last_greet_ms || e.now_ms-s->last_greet_ms>=1800000)) {
                s->greet_until_ms=e.now_ms+3000; s->last_greet_ms=e.now_ms;
                s->greetings_today++; s->next_greet_ms=e.now_ms+1800000+random_next(s)%420001;
                fx|=PET_FX_SAVE_QUOTA;
            }
        }
        break;
    }
    }
    return fx;
}
pet_pose_t pet_pose(const pet_state_t *s,uint64_t now) {
    float t=(float)(now%60000)/1000.0f;
    pet_pose_t p={.expression=PET_EXPR_IDLE,.scale_x=1,.scale_y=1,.eye_open=1,
                  .screen_on=s->life!=PET_LIFE_SLEEP};
    p.dim=s->life!=PET_LIFE_AWAKE || (s->usb && now>=s->last_input_ms && now-s->last_input_ms>=300000);
    if(s->dialogue==PET_DIALOGUE_LISTEN) {
        p.expression=PET_EXPR_LISTEN; p.rotation=-6; p.offset_y=-7; p.eye_open=1.1f;
    } else if(s->dialogue==PET_DIALOGUE_WAIT) {
        p.expression=PET_EXPR_THINK;
        p.rotation=6+sinf(t*2)*3; p.look_x=4; p.eye_asymmetry=.12f;
    } else if(s->dialogue==PET_DIALOGUE_SPEAK) {
        p.expression=PET_EXPR_SPEAK; p.level=(float)s->level/1000;
        uint64_t age=now>=s->entered_ms?now-s->entered_ms:0;
        if(age>80)p.level*=expf(-(float)(age-80)/120);
        p.scale_y=1+p.level*.075f; p.scale_x=1-p.level*.04f; p.offset_y=-p.level*7;
    } else if(s->life!=PET_LIFE_AWAKE) {
        p.expression=PET_EXPR_SLEEP; p.eye_open=.08f; p.scale_y=.94f; p.offset_y=10;
    } else if(now<s->confused_until_ms) {
        p.expression=PET_EXPR_CONFUSED; p.rotation=sinf(t*3)*7;
        p.eye_open=.75f; p.eye_asymmetry=-.22f; p.eye_tilt=18;
    } else if(s->contact.seen&&(s->contact.active||now<s->contact.until_ms)) {
        const pet_contact_t *c=&s->contact;
        float fade=c->active?1:fminf(1,(float)(c->until_ms-now)/700);
        p.expression=c->stroking?PET_EXPR_HAPPY:PET_EXPR_CURIOUS;
        p.look_x=fmaxf(-8,fminf(8,(float)(c->x-233)/20))*fade;
        p.look_y=fmaxf(-5,fminf(5,(float)(c->y-210)/25))*fade;
        p.rotation=p.look_x*.45f;p.offset_y=-3*fade;
        p.eye_open=c->stroking?.2f:1.04f;p.scale_y=c->stroking?.97f:1;
        if(now<c->wake_until_ms){
            float stretch=sinf((float)(900-(c->wake_until_ms-now))*3.14159265f/900);
            p.scale_y+=.06f*stretch;p.offset_y-=4*stretch;
        }
    } else if(now<s->pet_until_ms) {
        p.expression=PET_EXPR_HAPPY; p.eye_open=.15f; p.scale_y=.94f+sinf(t*9)*.035f;
        p.scale_x=1.04f; p.offset_y=3-fabsf(sinf(t*9))*10;
    } else if(now<s->curious_until_ms) {
        p.expression=PET_EXPR_CURIOUS; p.rotation=-12+sinf(t*4)*3; p.offset_y=-5-fabsf(sinf(t*5))*5;
        p.look_x=10; p.eye_open=1; p.eye_asymmetry=.28f; p.eye_tilt=-8;
    } else if(now<s->greet_until_ms) {
        p.expression=PET_EXPR_GREET; p.offset_y=-fabsf(sinf(t*5))*28; p.rotation=sinf(t*5)*8;
        p.invitation=!s->quiet;
    } else {
        p.scale_y=1+sinf(t*1.3f)*.014f; p.offset_y=sinf(t*1.3f)*3;
        float warmth=pet_warmth(s,now)/100;
        p.scale_y+=sinf(t*2)*.012f*warmth;p.offset_y-=warmth*2;
        p.look_x=sinf(t*.65f)*3*warmth;p.rotation=sinf(t*1.1f)*2*warmth;
        p.eye_open=1-.15f*warmth;
        if((now+(s->seed%2000))%6200<130) p.eye_open=.08f;
    }
    if(s->dialogue==PET_DIALOGUE_IDLE&&s->life==PET_LIFE_AWAKE&&now>=s->confused_until_ms&&
       s->contact.dodge_at_ms&&now>=s->contact.dodge_at_ms&&now-s->contact.dodge_at_ms<900){
        float dodge=sinf((float)(now-s->contact.dodge_at_ms)*3.14159265f/900);
        p.expression=PET_EXPR_CURIOUS;p.scale_x-=.06f*dodge;
        p.rotation+=(s->contact.x>=233?-12:12)*dodge;p.offset_y+=3*dodge;
    }
    return p;
}
pet_pose_t pet_motion_step(pet_motion_t *m,pet_pose_t target,uint64_t now){
    if(!m->initialized||!target.screen_on||now<m->last_ms||now-m->last_ms>500){
        m->pose=target;m->last_ms=now;m->initialized=target.screen_on;return target;
    }
    float dt=(float)(now-m->last_ms);
    float body=-expm1f(-dt/90),eye=-expm1f(-dt/60);
#define FOLLOW(field,amount) m->pose.field+=(target.field-m->pose.field)*(amount)
    FOLLOW(offset_y,body);FOLLOW(scale_x,body);FOLLOW(scale_y,body);FOLLOW(rotation,body);
    FOLLOW(eye_open,eye);FOLLOW(look_x,eye);FOLLOW(look_y,eye);FOLLOW(level,body);
    FOLLOW(eye_asymmetry,eye);FOLLOW(eye_tilt,eye);
#undef FOLLOW
    m->pose.expression=target.expression;m->pose.screen_on=target.screen_on;
    m->pose.dim=target.dim;m->pose.invitation=target.invitation;m->last_ms=now;
    return m->pose;
}
pet_gesture_t pet_gesture(int x0,int y0,int x1,int y1) {
    if(x0<0||x0>465||x1<0||x1>465||y0<0||y0>465||y1<0||y1>465) return PET_GESTURE_NONE;
    int dx=x1-x0,dy=y1-y0;
    int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    if(dy< -40 && ay>ax) return PET_GESTURE_SETTINGS;
    if(ax>40 && ay<35) return PET_GESTURE_STROKE;
    if(ax<30 && ay<30) return PET_GESTURE_TAP;
    return PET_GESTURE_NONE;
}
bool pet_event_urgent(pet_event_kind_t kind){
    /* Audio buffer/done/drain events share a FIFO: promoting only done would reorder the stream. */
    return kind==PET_EV_PRESS||kind==PET_EV_RELEASE||kind==PET_EV_CANCEL||kind==PET_EV_LINK||kind==PET_EV_TURN_STATUS;
}

bool pet_network_should_sleep(const pet_state_t *state){return state->life==PET_LIFE_SLEEP&&!state->usb;}

uint32_t pet_delay_ticks(uint32_t ms,uint32_t rate){uint64_t ticks=((uint64_t)ms*rate+999)/1000;return ticks>UINT32_MAX?UINT32_MAX:ticks?(uint32_t)ticks:1;}

bool pet_response_audio_allowed(const pet_state_t *s,uint32_t generation,uint32_t turn) {
    return s->connection==PET_CONN_READY && !s->response_cancelled &&
           s->dialogue!=PET_DIALOGUE_LISTEN && generation==s->generation && turn==s->turn;
}
