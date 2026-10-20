#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "pet_core.h"
static uint32_t event(pet_state_t *s, pet_event_kind_t kind, uint64_t now, int value) {
    return pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,.generation=s->generation,.turn=s->turn});
}
static void ready(pet_state_t *s,uint64_t now) {
    pet_step(s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=now,.generation=s->generation+1,.value=PET_CONN_READY});
}
static void clock_at(pet_state_t *s,uint64_t now,int minute) {
    pet_step(s,(pet_event_t){.kind=PET_EV_CLOCK,.now_ms=now,.value=minute,.day=20261018});
}
/* Numeric fallbacks let this behavioral regression fail against the v0.1.1 core. */
#ifndef PET_EXPRESSION_INPUT_VERSION
#define PET_EV_TOUCH_TAP ((pet_event_kind_t)17)
#define PET_EXPR_CURIOUS ((pet_expression_t)7)
#define PET_EXPR_CONFUSED ((pet_expression_t)8)
#endif
static void companion_feedback(void) {
    pet_state_t s;pet_init(&s,0);
    uint32_t fx=event(&s,PET_EV_TOUCH_TAP,100,0);
    assert(pet_pose(&s,100).expression==PET_EXPR_CURIOUS);
    assert(pet_pose(&s,2099).expression==PET_EXPR_CURIOUS);
    assert(pet_pose(&s,2100).expression==PET_EXPR_IDLE);
    assert(!(fx&(PET_FX_START|PET_FX_SUBMIT)));
    event(&s,PET_EV_MUTED,2200,1);event(&s,PET_EV_TOUCH_TAP,2300,0);
    assert(pet_pose(&s,2300).expression==PET_EXPR_CURIOUS);
    event(&s,PET_EV_PET,2400,0);
    assert(pet_pose(&s,4199).expression==PET_EXPR_HAPPY);
    assert(pet_pose(&s,4200).expression==PET_EXPR_IDLE);

    ready(&s,5000);event(&s,PET_EV_PRESS,5100,0);
    event(&s,PET_EV_TOUCH_TAP,5200,0);event(&s,PET_EV_PET,5300,0);
    assert(pet_pose(&s,5300).expression==PET_EXPR_LISTEN);
    event(&s,PET_EV_RELEASE,5500,0);event(&s,PET_EV_TOUCH_TAP,5600,0);
    assert(pet_pose(&s,5600).expression==PET_EXPR_THINK);
    assert(pet_pose(&s,26000).expression==PET_EXPR_THINK);
    event(&s,PET_EV_PLAYING,27000,700);event(&s,PET_EV_PET,27100,0);
    event(&s,PET_EV_TOUCH_TAP,27200,0);
    assert(pet_pose(&s,27200).expression==PET_EXPR_SPEAK);
    event(&s,PET_EV_RESPONSE_DONE,27300,0);
    assert(pet_pose(&s,27300).expression==PET_EXPR_SPEAK);
    assert(pet_response_audio_allowed(&s,s.generation,s.turn));
    assert(!pet_response_audio_allowed(&s,s.generation-1,s.turn));
    event(&s,PET_EV_DRAIN,27400,0);
    assert(pet_pose(&s,27400).expression==PET_EXPR_IDLE);

    event(&s,PET_EV_PRESS,28000,0);event(&s,PET_EV_RELEASE,28200,0);
    fx=event(&s,PET_EV_TICK,88200,0);assert(fx&PET_FX_CANCEL);
    assert(pet_pose(&s,88200).expression==PET_EXPR_CONFUSED);
    event(&s,PET_EV_TICK,89000,0);
    assert(pet_pose(&s,90199).expression==PET_EXPR_CONFUSED);
    assert(pet_pose(&s,90200).expression==PET_EXPR_IDLE);
    assert(event(&s,PET_EV_BUFFERED,90300,0)==PET_FX_NONE);
    assert(event(&s,PET_EV_PLAYING,90400,800)==PET_FX_NONE);
    assert(s.dialogue==PET_DIALOGUE_IDLE);
    assert(!pet_response_audio_allowed(&s,s.generation,s.turn));
    event(&s,PET_EV_PRESS,91000,0);assert(pet_pose(&s,91000).expression==PET_EXPR_LISTEN);
    event(&s,PET_EV_CANCEL,91500,0);
    assert(event(&s,PET_EV_BUFFERED,91600,0)==PET_FX_NONE);

    pet_init(&s,0);ready(&s,1);
    event(&s,PET_EV_LINK,100,PET_CONN_OFFLINE);
    assert(pet_pose(&s,100).expression==PET_EXPR_CONFUSED);
    event(&s,PET_EV_LINK,1000,PET_CONN_OFFLINE);
    assert(pet_pose(&s,2100).expression==PET_EXPR_IDLE);
    event(&s,PET_EV_LINK,2200,PET_CONN_CONNECTING);
    event(&s,PET_EV_LINK,2300,PET_CONN_UNAVAILABLE);
    assert(pet_pose(&s,2300).expression==PET_EXPR_IDLE);
    event(&s,PET_EV_TOUCH_TAP,2400,0);
    assert(pet_pose(&s,2400).expression==PET_EXPR_CURIOUS);
    event(&s,PET_EV_TICK,123000,0);assert(pet_pose(&s,123000).expression==PET_EXPR_SLEEP);
    event(&s,PET_EV_TOUCH_TAP,123100,0);
    assert(s.life==PET_LIFE_AWAKE&&pet_pose(&s,123100).expression==PET_EXPR_CURIOUS);
    ready(&s,126000);event(&s,PET_EV_LINK,126100,PET_CONN_UNAVAILABLE);
    assert(pet_pose(&s,126100).expression==PET_EXPR_CONFUSED);
    ready(&s,126200);assert(pet_pose(&s,126200).expression==PET_EXPR_IDLE);
    event(&s,PET_EV_PRESS,127000,0);event(&s,PET_EV_RELEASE,127050,0);
    event(&s,PET_EV_BUFFERED,127100,0);
    assert(s.dialogue==PET_DIALOGUE_IDLE);
}
int main(void) {
    companion_feedback();
    assert(pet_delay_ticks(1,100)==1&&pet_delay_ticks(2,100)==1&&pet_delay_ticks(5,1000)==5);
    pet_state_t s;pet_init(&s,0);
    assert(s.connection==PET_CONN_OFFLINE&&s.dialogue==PET_DIALOGUE_IDLE);
    event(&s,PET_EV_PET,10,0);
    assert(pet_pose(&s,50).expression==PET_EXPR_HAPPY);
    uint32_t fx=event(&s,PET_EV_PRESS,60,0);
    assert((fx&PET_FX_CONNECT)&&!(fx&PET_FX_START));
    event(&s,PET_EV_RELEASE,70,0);ready(&s,100);
    assert(s.dialogue==PET_DIALOGUE_IDLE);
    fx=event(&s,PET_EV_PRESS,200,0);assert(fx&PET_FX_START);
    assert(pet_pose(&s,210).expression==PET_EXPR_LISTEN);
    event(&s,PET_EV_PET,220,0);assert(pet_pose(&s,230).expression==PET_EXPR_LISTEN);
    fx=event(&s,PET_EV_RELEASE,700,0);assert((fx&PET_FX_SUBMIT)&&(fx&PET_FX_STOP));
    assert(s.dialogue==PET_DIALOGUE_WAIT);
    event(&s,PET_EV_PLAYING,750,500);event(&s,PET_EV_RESPONSE_DONE,760,0);
    assert(s.dialogue==PET_DIALOGUE_SPEAK);
    event(&s,PET_EV_DRAIN,800,0);assert(s.dialogue==PET_DIALOGUE_IDLE);
    fx=event(&s,PET_EV_PRESS,1000,0);assert((fx&PET_FX_CANCEL)&&(fx&PET_FX_START));
    uint32_t old=s.turn;event(&s,PET_EV_RELEASE,1600,0);event(&s,PET_EV_PLAYING,1700,750);
    event(&s,PET_EV_PRESS,1800,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_PLAYING,.now_ms=1900,.generation=s.generation,.turn=old,.value=900});
    assert(s.dialogue==PET_DIALOGUE_LISTEN);
    fx=event(&s,PET_EV_TICK,56800,0);assert((fx&PET_FX_STOP)&&(fx&PET_FX_CANCEL));
    assert(s.dialogue==PET_DIALOGUE_IDLE);
    event(&s,PET_EV_RELEASE,57000,0);
    event(&s,PET_EV_PRESS,58000,0);fx=event(&s,PET_EV_RELEASE,58050,0);
    assert(!(fx&PET_FX_SUBMIT)&&s.dialogue==PET_DIALOGUE_IDLE);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=59000,.generation=s.generation+1,.value=PET_CONN_OFFLINE});
    fx=event(&s,PET_EV_PRESS,60000,0);assert(!(fx&PET_FX_START));
    ready(&s,61000);assert(s.dialogue==PET_DIALOGUE_LISTEN);
    fx=event(&s,PET_EV_CANCEL,62000,0);assert(fx&PET_FX_STOP);
    pet_step(&s,(pet_event_t){.kind=PET_EV_PLAYING,.now_ms=63000,.generation=s.generation-1,.turn=s.turn,.value=900});
    assert(s.dialogue==PET_DIALOGUE_IDLE);

    pet_init(&s,0);ready(&s,10);event(&s,PET_EV_PRESS,20,0);event(&s,PET_EV_RELEASE,500,0);
    fx=event(&s,PET_EV_BUFFERED,600,0);assert(!(fx&PET_FX_WAKE));event(&s,PET_EV_RESPONSE_DONE,610,0);
    assert(s.dialogue==PET_DIALOGUE_WAIT);
    event(&s,PET_EV_PLAYING,650,300);assert(s.dialogue==PET_DIALOGUE_SPEAK);
    event(&s,PET_EV_DRAIN,800,0);assert(s.dialogue==PET_DIALOGUE_IDLE);

    pet_init(&s,0);event(&s,PET_EV_USB,0,1);clock_at(&s,0,21*60+59);
    event(&s,PET_EV_TICK,800000,0);assert(s.greetings_today==0);

    assert(pet_gesture(230,300,235,220)==PET_GESTURE_SETTINGS);
    assert(pet_gesture(200,200,285,208)==PET_GESTURE_STROKE);
    assert(pet_gesture(200,200,204,206)==PET_GESTURE_TAP);
    assert(pet_gesture(200,200,300,300)==PET_GESTURE_NONE);

    pet_init(&s,0);event(&s,PET_EV_TICK,90001,0);assert(s.life==PET_LIFE_DROWSY);
    event(&s,PET_EV_TICK,120001,0);assert(s.life==PET_LIFE_SLEEP);
    event(&s,PET_EV_PET,120100,0);assert(s.life==PET_LIFE_AWAKE);
    pet_init(&s,0);event(&s,PET_EV_USB,0,1);event(&s,PET_EV_TICK,120001,0);
    assert(s.life==PET_LIFE_AWAKE);
    event(&s,PET_EV_TICK,900001,0);assert(s.life==PET_LIFE_DROWSY);
    event(&s,PET_EV_TICK,1800001,0);assert(s.life==PET_LIFE_SLEEP);

    pet_init(&s,0);event(&s,PET_EV_USB,0,1);clock_at(&s,0,23*60);
    for(uint64_t now=10000;now<=800000;now+=10000)event(&s,PET_EV_TICK,now,0);
    assert(s.greetings_today==0);
    pet_init(&s,0);event(&s,PET_EV_USB,0,1);clock_at(&s,0,10*60);
    for(uint64_t now=10000;now<=800000;now+=10000)event(&s,PET_EV_TICK,now,0);
    assert(s.greetings_today==1);
    for(int i=1;i<=4;i++){
        uint64_t now=(uint64_t)i*2400000;
        event(&s,PET_EV_PET,now,0);event(&s,PET_EV_TICK,now+7000,0);
    }
    assert(s.greetings_today==3);
    clock_at(&s,s.now_ms,12*60);
    assert(s.greetings_today==3);
    pet_pose_t p=pet_pose(&s,s.now_ms);
    assert(isfinite(p.scale_x)&&isfinite(p.scale_y));
    pet_init(&s,0);ready(&s,1);event(&s,PET_EV_PRESS,2,0);event(&s,PET_EV_RELEASE,200,0);
    event(&s,PET_EV_RESPONSE_DONE,300,0);event(&s,PET_EV_TICK,1801000,0);
    assert(s.life==PET_LIFE_SLEEP);
    fx=event(&s,PET_EV_BUFFERED,1802000,0);assert((fx&PET_FX_WAKE)&&s.life==PET_LIFE_AWAKE);
    pet_init(&s,0);ready(&s,1);event(&s,PET_EV_PRESS,2,0);event(&s,PET_EV_RELEASE,200,0);
    event(&s,PET_EV_BUFFERED,300,0);event(&s,PET_EV_PLAYING,400,100);
    fx=event(&s,PET_EV_SETUP,500,1);assert((fx&PET_FX_CANCEL)&&s.setup&&s.dialogue==PET_DIALOGUE_IDLE);
    fx=event(&s,PET_EV_PRESS,600,0);assert((fx&PET_FX_START)&&!s.setup&&s.dialogue==PET_DIALOGUE_LISTEN);
    fx=event(&s,PET_EV_TASK_CHANNEL,700,0);assert(!s.task_channel&&fx==0&&s.dialogue==PET_DIALOGUE_LISTEN);
    pet_init(&s,0);event(&s,PET_EV_TICK,100,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.generation=1,.value=PET_CONN_READY,.now_ms=90});assert(s.connection==PET_CONN_READY);
    event(&s,PET_EV_PRESS,110,0);event(&s,PET_EV_TICK,500,0);
    fx=event(&s,PET_EV_RELEASE,450,0);assert((fx&PET_FX_SUBMIT)&&!s.held);
    pet_init(&s,0);event(&s,PET_EV_TICK,120001,0);assert(pet_network_should_sleep(&s));
    event(&s,PET_EV_USB,120002,1);assert(!pet_network_should_sleep(&s));
    event(&s,PET_EV_USB,120003,0);assert(pet_network_should_sleep(&s));
    puts("PET_CORE_OK");
    return 0;
}
