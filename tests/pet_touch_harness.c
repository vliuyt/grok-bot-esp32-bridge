#include "pet_core.h"
#include <assert.h>
#include <stdio.h>
static uint32_t send(pet_state_t *s,pet_event_kind_t kind,uint64_t now){return pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.generation=s->generation,.turn=s->turn});}
static void ready(pet_state_t *s){pet_step(s,(pet_event_t){.kind=PET_EV_LINK,.value=PET_CONN_READY,.generation=s->generation+1,.now_ms=s->now_ms+1});}
int main(void){
 pet_state_t s;pet_init(&s,0);ready(&s);
 uint32_t fx=send(&s,PET_EV_TOUCH_PRESS,100);
 assert((fx&PET_FX_START)&&s.dialogue==PET_DIALOGUE_LISTEN&&s.held);
 uint32_t turn=s.turn;assert(send(&s,PET_EV_TOUCH_PRESS,200)==0&&s.turn==turn);
 fx=send(&s,PET_EV_TOUCH_RELEASE,500);assert((fx&PET_FX_SUBMIT)&&s.dialogue==PET_DIALOGUE_WAIT&&!s.held);
 assert(!(send(&s,PET_EV_TOUCH_RELEASE,600)&PET_FX_SUBMIT));
 send(&s,PET_EV_TOUCH_PRESS,700);fx=send(&s,PET_EV_TOUCH_CANCEL,1000);
 assert((fx&PET_FX_CANCEL)&&!(fx&PET_FX_SUBMIT)&&!s.held&&s.dialogue==PET_DIALOGUE_IDLE);
 send(&s,PET_EV_TOUCH_RELEASE,1100);assert(s.dialogue==PET_DIALOGUE_IDLE);
 send(&s,PET_EV_TOUCH_PRESS,1200);fx=send(&s,PET_EV_TOUCH_RELEASE,1250);assert(!(fx&PET_FX_SUBMIT)&&s.dialogue==PET_DIALOGUE_IDLE);
 /* BOOT owns the turn until its own release. */
 send(&s,PET_EV_PRESS,2000);turn=s.turn;
 assert(send(&s,PET_EV_TOUCH_PRESS,2100)==0&&s.turn==turn);
 assert(send(&s,PET_EV_TOUCH_CANCEL,2200)==0&&s.held&&s.dialogue==PET_DIALOGUE_LISTEN);
 fx=send(&s,PET_EV_RELEASE,2500);assert((fx&PET_FX_SUBMIT)&&!s.held);
 /* BOOT takes ownership from a finger; finger release must not submit. */
 send(&s,PET_EV_TOUCH_PRESS,3000);turn=s.turn;send(&s,PET_EV_PRESS,3100);
 assert(s.turn==turn&&send(&s,PET_EV_TOUCH_RELEASE,3300)==0&&s.held);
 fx=send(&s,PET_EV_RELEASE,3500);assert(fx&PET_FX_SUBMIT);
 /* BOOT release ends the turn even if a finger remains on screen. */
 send(&s,PET_EV_TOUCH_PRESS,4000);send(&s,PET_EV_PRESS,4100);fx=send(&s,PET_EV_RELEASE,4400);
 assert((fx&PET_FX_SUBMIT)&&!s.held);turn=s.turn;
 assert(send(&s,PET_EV_TOUCH_PRESS,4500)==0&&s.turn==turn);
 assert(send(&s,PET_EV_TOUCH_RELEASE,4600)==0);
 assert(send(&s,PET_EV_TOUCH_PRESS,4700)&PET_FX_START);
 send(&s,PET_EV_CANCEL,4800);send(&s,PET_EV_TOUCH_RELEASE,4900);
 assert(s.dialogue==PET_DIALOGUE_IDLE&&!s.held);
 /* A released offline hold must never record when readiness arrives later. */
 pet_init(&s,0);fx=send(&s,PET_EV_TOUCH_PRESS,100);assert((fx&PET_FX_CONNECT)&&!(fx&PET_FX_START));
 send(&s,PET_EV_TOUCH_RELEASE,500);ready(&s);assert(s.dialogue==PET_DIALOGUE_IDLE);
 pet_init(&s,0);send(&s,PET_EV_TOUCH_PRESS,100);ready(&s);assert(s.dialogue==PET_DIALOGUE_LISTEN);
 send(&s,PET_EV_TOUCH_CANCEL,500);assert(s.dialogue==PET_DIALOGUE_IDLE);
 assert(pet_event_urgent(PET_EV_TOUCH_PRESS)&&pet_event_urgent(PET_EV_TOUCH_RELEASE)&&pet_event_urgent(PET_EV_TOUCH_CANCEL));
 puts("PET_TOUCH_OK");
}
