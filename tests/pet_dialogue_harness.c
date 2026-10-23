#include "pet_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t event(pet_state_t *s,pet_event_kind_t kind,uint64_t now,int value){
    return pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,.generation=s->generation,.turn=s->turn});
}
static void ready(pet_state_t *s){
    pet_init(s,0);pet_step(s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
}
static void waiting(pet_state_t *s){
    event(s,PET_EV_PRESS,100,0);event(s,PET_EV_RELEASE,300,0);
}
static void wait_stages(void){
    pet_state_t s;ready(&s);waiting(&s);
    pet_pose_t focused=pet_pose(&s,1300),searching=pet_pose(&s,5000),patient=pet_pose(&s,10000);
    assert(focused.expression==PET_EXPR_THINK&&fabsf(focused.look_x)<.01f);
    assert(searching.expression==PET_EXPR_THINK&&fabsf(searching.look_x)>.2f);
    assert(patient.expression==PET_EXPR_THINK&&patient.look_y<-.5f);
    assert(pet_pose(&s,3299).look_x==0&&pet_pose(&s,8299).look_y==0);
    pet_pose_t before=pet_pose(&s,10000);
    event(&s,PET_EV_TURN_STATUS,10000,PET_NOTICE_WORKING);
    event(&s,PET_EV_TURN_STATUS,10001,PET_NOTICE_PROGRESS);
    pet_pose_t after=pet_pose(&s,10001);
    assert(after.expression==PET_EXPR_THINK&&fabsf(before.look_y-after.look_y)<.02f);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.wait_started_ms==300);
#endif
}
static void finish_orders(void){
    for(int order=0;order<2;order++){
        pet_state_t s;ready(&s);waiting(&s);event(&s,PET_EV_BUFFERED,400,0);event(&s,PET_EV_PLAYING,500,700);
        event(&s,order?PET_EV_DRAIN:PET_EV_RESPONSE_DONE,600,0);
        assert(s.dialogue==PET_DIALOGUE_SPEAK);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
        assert(s.settle_until_ms==0);
#endif
        event(&s,order?PET_EV_RESPONSE_DONE:PET_EV_DRAIN,700,0);
        assert(s.dialogue==PET_DIALOGUE_IDLE&&pet_pose(&s,700).expression==PET_EXPR_IDLE);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
        assert(s.settle_until_ms==1600);
#endif
        pet_pose_t settled=pet_pose(&s,850);
        assert(settled.expression==PET_EXPR_IDLE&&fabsf(settled.offset_y)>.5f&&fabsf(settled.look_x)<.01f);
        event(&s,PET_EV_RESPONSE_DONE,900,0);event(&s,PET_EV_DRAIN,1000,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
        assert(s.settle_until_ms==1600);
#endif
        event(&s,PET_EV_PRESS,1100,0);assert(s.dialogue==PET_DIALOGUE_LISTEN);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
        assert(s.settle_until_ms==0);
#endif
    }
    pet_state_t s;ready(&s);waiting(&s);event(&s,PET_EV_RESPONSE_DONE,700,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.settle_until_ms==0); /* Text-only responses cannot imitate spoken completion. */
#endif
    ready(&s);waiting(&s);event(&s,PET_EV_PLAYING,500,700);event(&s,PET_EV_DRAIN,600,0);
    assert(s.dialogue==PET_DIALOGUE_SPEAK); /* A chunk gap is not the end of the reply. */
    event(&s,PET_EV_BUFFERED,800,0);event(&s,PET_EV_PLAYING,900,500);
    assert(s.dialogue==PET_DIALOGUE_SPEAK);
}
static void warmth_snapshot(void){
    pet_state_t warm,neutral;ready(&warm);ready(&neutral);
    event(&warm,PET_EV_PET,10,0);waiting(&warm);waiting(&neutral);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    float snapshot=warm.turn_warmth;assert(snapshot>34&&snapshot<35&&neutral.turn_warmth==0);
    assert(warm.warmth_at_ms==10); /* Conversation never renews the local one-minute mood. */
#endif
    pet_pose_t a=pet_pose(&warm,1300),b=pet_pose(&neutral,1300);
    assert(a.expression==b.expression&&a.offset_y<b.offset_y&&a.eye_open<b.eye_open);
    event(&warm,PET_EV_PET,2000,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth==snapshot); /* Touch in the current turn affects the next turn. */
#endif
    event(&warm,PET_EV_PLAYING,61000,700);event(&neutral,PET_EV_PLAYING,61000,700);
    a=pet_pose(&warm,61000);b=pet_pose(&neutral,61000);
    assert(pet_warmth(&warm,62000)==0&&a.expression==PET_EXPR_SPEAK&&a.level==b.level&&a.offset_y<b.offset_y);
    event(&warm,PET_EV_RESPONSE_DONE,62000,0);event(&warm,PET_EV_DRAIN,62001,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth==snapshot);
#endif
    event(&warm,PET_EV_TICK,62901,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth==0&&warm.settle_until_ms==0);
#endif
    event(&warm,PET_EV_PRESS,63000,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth==0);
#endif

    ready(&warm);event(&warm,PET_EV_PET,10,0);event(&warm,PET_EV_PRESS,100,0);
    a=pet_pose(&warm,100);
    neutral=warm;neutral.turn_warmth=0;b=pet_pose(&neutral,100);
    assert(a.expression==PET_EXPR_LISTEN&&a.offset_y<b.offset_y&&a.eye_open<b.eye_open);
    event(&warm,PET_EV_RELEASE,300,0);event(&warm,PET_EV_CANCEL,400,0);
    event(&warm,PET_EV_PRESS,30100,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth>17&&warm.turn_warmth<17.5f&&warm.warmth_at_ms==10);
#endif
    event(&warm,PET_EV_CANCEL,30200,0);event(&warm,PET_EV_LINK,30300,PET_CONN_CONNECTING);
    event(&warm,PET_EV_PRESS,30400,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(warm.turn_warmth==0); /* A connecting press did not start a real recording. */
#endif
}
static void cleanup(void){
    const pet_event_kind_t kinds[]={PET_EV_CANCEL,PET_EV_PRESS,PET_EV_LINK,PET_EV_SETUP,PET_EV_TURN_STATUS};
    for(unsigned i=0;i<sizeof(kinds)/sizeof(kinds[0]);i++){
        pet_state_t s;ready(&s);event(&s,PET_EV_PET,10,0);waiting(&s);
        event(&s,PET_EV_PLAYING,500,700);event(&s,PET_EV_RESPONSE_DONE,600,0);event(&s,PET_EV_DRAIN,700,0);
        int value=kinds[i]==PET_EV_LINK?PET_CONN_CONNECTING:kinds[i]==PET_EV_SETUP?1:
            kinds[i]==PET_EV_TURN_STATUS?PET_NOTICE_RESPONSE_FAILED:0;
        event(&s,kinds[i],800,value);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
        assert(s.settle_until_ms==0);
        if(kinds[i]!=PET_EV_PRESS)assert(s.turn_warmth==0);
#endif
    }
    pet_state_t s;ready(&s);event(&s,PET_EV_PET,10,0);event(&s,PET_EV_PRESS,100,0);event(&s,PET_EV_RELEASE,150,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.turn_warmth==0&&s.wait_started_ms==0);
#endif
    ready(&s);event(&s,PET_EV_PET,10,0);waiting(&s);event(&s,PET_EV_TICK,60300,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.turn_warmth==0&&s.wait_started_ms==0&&s.settle_until_ms==0);
#endif
    ready(&s);event(&s,PET_EV_PET,10,0);event(&s,PET_EV_PRESS,100,0);event(&s,PET_EV_TICK,55100,0);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.turn_warmth==0&&s.wait_started_ms==0&&s.settle_until_ms==0);
#endif
    ready(&s);event(&s,PET_EV_PET,10,0);waiting(&s);
    event(&s,PET_EV_PLAYING,500,700);event(&s,PET_EV_RESPONSE_DONE,600,0);event(&s,PET_EV_DRAIN,700,0);
    event(&s,PET_EV_TICK,120700,0);assert(s.life==PET_LIFE_SLEEP);
#ifdef PET_DIALOGUE_PRESENTATION_VERSION
    assert(s.turn_warmth==0&&s.settle_until_ms==0);
#endif
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"wait"))wait_stages();
    else if(argc>1&&!strcmp(argv[1],"finish"))finish_orders();
    else if(argc>1&&!strcmp(argv[1],"warmth"))warmth_snapshot();
    else if(argc>1&&!strcmp(argv[1],"cleanup"))cleanup();
    else {wait_stages();finish_orders();warmth_snapshot();cleanup();}
    puts("PET_DIALOGUE_OK");return 0;
}
