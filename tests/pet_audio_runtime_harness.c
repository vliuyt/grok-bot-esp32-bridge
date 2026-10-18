#include "../firmware/pet/components/pet_audio/pet_audio.c"

#include <assert.h>
#include <stdio.h>
static int buffered_notices;
static void got_event(pet_event_t e,void *ctx){(void)ctx;if(e.kind==PET_EV_BUFFERED)buffered_notices++;}
static void got_credit(uint32_t g,uint32_t t,void *ctx){(void)g;(void)t;(void)ctx;credits++;}
int main(void){
 queue=calloc(1,sizeof(*queue));state_lock=io_lock=(void*)1;enabled=true;atomic_store(&powered,true);cb.credit=got_credit;
 int before=mutex_takes;pet_audio_power(true);assert(mutex_takes==before);
 atomic_store(&generation,1);atomic_store(&turn,1);pet_pcm_reset(queue,1,1);
 int16_t pcm[320]={100};pet_pcm_push(queue,1,1,pcm,sizeof(pcm));
 pet_pcm_packet_t in_flight;assert(pet_pcm_pop(queue,&in_flight));
 /* Audio thread is descheduled after pop; link clears the SAME turn. */
 pet_audio_flush_playback(1,1);assert(amp_on==0&&queue->count==0);
 playback_block(&in_flight);
 fprintf(stderr,"AFTER_CLEAR: codec_writes=%d amp_on=%d credits=%d\n",writes,amp_on,credits);
 assert(writes==0 && amp_on==0 && credits==1);
 cb.event=got_event;assert(pet_audio_push(1,1,pcm,sizeof(pcm))==1);assert(pet_audio_push(1,1,pcm,sizeof(pcm))==1);assert(buffered_notices==1);pet_audio_clear(1,1);assert(credits==3);
 free(queue);puts("SAME_TURN_CLEAR_OK");return 0;
}
