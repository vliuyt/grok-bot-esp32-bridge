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
    if(voice_event(e.kind) && (e.generation!=s->generation || e.turn!=s->turn || s->response_cancelled)) return PET_FX_NONE;
    uint32_t fx=PET_FX_NONE;
    switch(e.kind) {
    case PET_EV_PRESS:
        if(s->held) break;
        s->held=true; s->expired=false; s->setup=false;s->notice=PET_NOTICE_NONE; pet_note_activity(s,e.now_ms);
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
        if(s->connection==PET_CONN_READY && s->held && !s->expired && s->dialogue==PET_DIALOGUE_IDLE)
            fx|=begin(s,e.now_ms);
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
        pet_note_activity(s,e.now_ms); fx|=PET_FX_WAKE;
        if(s->dialogue==PET_DIALOGUE_IDLE) {
            s->pet_until_ms=e.now_ms+1800;
            s->curious_until_ms=s->confused_until_ms=s->greet_until_ms=0;
        }
        break;
    case PET_EV_TOUCH_TAP:
        pet_note_activity(s,e.now_ms); fx|=PET_FX_WAKE;
        if(s->dialogue==PET_DIALOGUE_IDLE) {
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
            fx|=PET_FX_CANCEL;pet_note_activity(s,e.now_ms);
        }
        break;
    case PET_EV_CLOCK:
        if(e.value<0 || e.value>=1440 || !e.day) break;
        if(e.day>s->day) { s->day=e.day; s->greetings_today=0; }
        s->minute=e.value; s->clock_ms=e.now_ms; s->clock_valid=true;
        break;
    case PET_EV_TICK: {
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
        p.scale_y=1+p.level*.075f; p.scale_x=1-p.level*.04f; p.offset_y=-p.level*7;
    } else if(s->life!=PET_LIFE_AWAKE) {
        p.expression=PET_EXPR_SLEEP; p.eye_open=.08f; p.scale_y=.94f; p.offset_y=10;
    } else if(now<s->confused_until_ms) {
        p.expression=PET_EXPR_CONFUSED; p.rotation=sinf(t*3)*7;
        p.eye_open=.75f; p.eye_asymmetry=-.22f; p.eye_tilt=18;
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
        if((now+(s->seed%2000))%6200<130) p.eye_open=.08f;
    }
    return p;
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
