#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pet_pcm_queue.h"
#include "pet_core.h"
int main(void){
    pet_pcm_queue_t q={0};pet_pcm_packet_t packet;int16_t audio[320]={1000};
    assert(pet_pcm_reset(&q,1,1)==0);assert(!pet_pcm_drained(&q));
    assert(pet_pcm_push(&q,1,1,audio,640)==1);audio[0]=2000;
    assert(!pet_pcm_drained(&q));assert(pet_pcm_done(&q,1,1));assert(!pet_pcm_drained(&q));
    assert(pet_pcm_pop(&q,&packet)&&packet.pcm[0]==1000);assert(pet_pcm_drained(&q));
    assert(pet_pcm_push(&q,1,1,audio,640)==1);assert(!pet_pcm_drained(&q));
    assert(pet_pcm_pop(&q,&packet));
    assert(pet_pcm_reset(&q,1,2)==0);
    assert(pet_pcm_push(&q,1,1,audio,640)==0);assert(pet_pcm_push(&q,0,2,audio,640)==0);
    assert(pet_pcm_push(&q,1,2,audio,3)<0);assert(pet_pcm_push(&q,1,2,audio,642)<0);
    for(unsigned i=0;i<PET_PCM_CAPACITY;i++)assert(pet_pcm_push(&q,1,2,audio,640)==1);
    assert(pet_pcm_push(&q,1,2,audio,640)<0);assert(pet_pcm_reset(&q,2,3)==PET_PCM_CAPACITY);
    assert(!pet_pcm_pop(&q,&packet));assert(!pet_pcm_done(&q,1,2));assert(!pet_pcm_drained(&q));
    pet_state_t s;pet_init(&s,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
    pet_step(&s,(pet_event_t){.kind=PET_EV_SETUP,.now_ms=2,.value=1});
    uint32_t fx=pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=3});
    assert((fx&PET_FX_START)&&!s.setup&&s.dialogue==PET_DIALOGUE_LISTEN);
    fx=pet_step(&s,(pet_event_t){.kind=PET_EV_CANCEL,.now_ms=4});
    assert((fx&PET_FX_STOP)&&!s.held&&s.dialogue==PET_DIALOGUE_IDLE);
    pet_step(&s,(pet_event_t){.kind=PET_EV_BUFFERED,.now_ms=5,.generation=s.generation,.turn=s.turn});
    assert(s.dialogue==PET_DIALOGUE_IDLE); /* cancelled audio cannot resurrect this turn */
    pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=20});
    pet_step(&s,(pet_event_t){.kind=PET_EV_RELEASE,.now_ms=200});
    pet_step(&s,(pet_event_t){.kind=PET_EV_BUFFERED,.now_ms=300,.generation=s.generation,.turn=s.turn});
    assert(s.dialogue==PET_DIALOGUE_WAIT);
    pet_step(&s,(pet_event_t){.kind=PET_EV_PLAYING,.now_ms=400,.generation=s.generation,.turn=s.turn,.value=600});
    assert(s.dialogue==PET_DIALOGUE_SPEAK);
    pet_step(&s,(pet_event_t){.kind=PET_EV_CANCEL,.now_ms=1000});
    pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=1200});
    pet_step(&s,(pet_event_t){.kind=PET_EV_RELEASE,.now_ms=1500});
    assert(pet_pose(&s,22000).expression==PET_EXPR_THINK);
    pet_init(&s,0);pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
    pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=20});pet_step(&s,(pet_event_t){.kind=PET_EV_RELEASE,.now_ms=500});
    pet_event_t ordered[]={
        {.kind=PET_EV_BUFFERED,.now_ms=600,.generation=1,.turn=s.turn},
        {.kind=PET_EV_PLAYING,.now_ms=610,.generation=1,.turn=s.turn,.value=500},
        {.kind=PET_EV_RESPONSE_DONE,.now_ms=620,.generation=1,.turn=s.turn},
        {.kind=PET_EV_DRAIN,.now_ms=800,.generation=1,.turn=s.turn}
    };
    for(int pass=0;pass<2;pass++)for(unsigned i=0;i<4;i++){
        if(pet_event_urgent(ordered[i].kind)==(pass==0))pet_step(&s,ordered[i]);
    }
    assert(s.dialogue==PET_DIALOGUE_IDLE);
    puts("PET_AUDIO_OK");
}
