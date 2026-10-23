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
 cb.event=got_event;assert(pet_audio_push(1,1,pet_audio_receive_epoch(),pcm,sizeof(pcm))==1);assert(pet_audio_push(1,1,pet_audio_receive_epoch(),pcm,sizeof(pcm))==1);assert(buffered_notices==1);pet_audio_clear(1,1);assert(credits==3);
 /* Link is paused after reading an allowed model, then app clears the same turn. */
 uint32_t receive_epoch=atomic_load(&playback_epoch);assert(receive_epoch==atomic_load(&playback_epoch));
 int writes_before=writes;pet_audio_clear(1,1);
 int accepted=pet_audio_push(1,1,receive_epoch,pcm,sizeof(pcm));
 if(pet_pcm_pop(queue,&in_flight)){in_flight.epoch=atomic_load(&playback_epoch);playback_block(&in_flight);}
 assert(accepted==0&&writes==writes_before&&amp_on==0);
 /* A newly gated frame, including a normal completed task's follow-up, is valid. */
 assert(pet_audio_push(1,1,pet_audio_receive_epoch(),pcm,sizeof(pcm))==1);
 assert(pet_pcm_pop(queue,&in_flight));in_flight.epoch=pet_audio_receive_epoch();playback_block(&in_flight);
 assert(writes==writes_before+1);
 pet_audio_begin(1,2);read_left=64;read_right=8192;capture_block();
 pet_audio_status_t mic=pet_audio_status();
 assert(mic.mic_left_db<=-50&&mic.mic_right_db>=-13&&mic.mic_right_db<=-12);
 assert(mic.mic_right_db-mic.mic_left_db>=40);
 pet_audio_begin(1,3);read_left=read_right=0;capture_block();mic=pet_audio_status();
 assert(mic.mic_left_db==-100&&mic.mic_right_db==-100);
 free(queue);puts("SAME_TURN_CLEAR_OK");return 0;
}
