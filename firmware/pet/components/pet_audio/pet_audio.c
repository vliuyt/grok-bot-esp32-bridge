#include "pet_audio.h"
#include "board_port.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdatomic.h>

static pet_pcm_queue_t *queue;
static pet_audio_callbacks_t cb;
static SemaphoreHandle_t state_lock,io_lock;
static esp_codec_dev_handle_t speaker,microphone;
static atomic_uint generation,turn,playback_epoch;
static atomic_bool recording,powered,faulted;
static bool finish_requested,drain_sent,buffered_announced;
static int volume,gain;
static bool enabled;
static uint32_t played_frames,captured_frames;
static uint64_t last_write_ms,last_level_ms;
static float hpf_x,hpf_y;
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
static bool owns(uint32_t g,uint32_t t){return atomic_load(&generation)==g&&atomic_load(&turn)==t;}
static void event(pet_event_kind_t kind,uint32_t g,uint32_t t,int value){
    if(cb.event)cb.event((pet_event_t){.kind=kind,.now_ms=now_ms(),.generation=g,.turn=t,.value=value},cb.context);
}
static void fault(void){if(!atomic_exchange(&faulted,true)&&cb.fault)cb.fault(cb.context);}
static bool open_codecs(void){
    esp_codec_dev_sample_info_t info={.sample_rate=16000,.channel=2,.bits_per_sample=16};
    if(esp_codec_dev_open(speaker,&info)!=ESP_CODEC_DEV_OK)return false;
    if(esp_codec_dev_open(microphone,&info)!=ESP_CODEC_DEV_OK){esp_codec_dev_close(speaker);return false;}
    esp_codec_dev_set_out_vol(speaker,volume);esp_codec_dev_set_in_gain(microphone,(float)gain);
    return true;
}
void pet_audio_power(bool on){
    if(!io_lock||on==atomic_load(&powered))return;
    xSemaphoreTake(io_lock,portMAX_DELAY);
    if(on!=atomic_load(&powered)){
        if(on){
            if(pet_board()->codec_power)pet_board()->codec_power(true);
            bool ok=open_codecs();atomic_store(&powered,ok);
            if(ok)atomic_store(&faulted,false);else fault();
        }else{
            pet_board()->audio_power(false);esp_codec_dev_close(speaker);esp_codec_dev_close(microphone);
            atomic_store(&powered,false);
            if(pet_board()->codec_power)pet_board()->codec_power(false);
        }
    }
    xSemaphoreGive(io_lock);
}
void pet_audio_settings(const pet_config_t *config){
    if(!io_lock||!pet_config_valid(config))return;
    xSemaphoreTake(io_lock,portMAX_DELAY);
    volume=config->volume;gain=config->mic_gain;enabled=config->speaker!=0;
    if(atomic_load(&powered)){
        esp_codec_dev_set_out_vol(speaker,enabled?volume:0);esp_codec_dev_set_in_gain(microphone,(float)gain);
        if(!enabled)pet_board()->audio_power(false);
    }
    xSemaphoreGive(io_lock);
}
void pet_audio_clear(uint32_t g,uint32_t t){
    if(!queue)return;
    atomic_store(&recording,false);
    xSemaphoreTake(state_lock,portMAX_DELAY);
    uint32_t old_g=queue->generation,old_t=queue->turn;
    atomic_fetch_add(&playback_epoch,1);
    unsigned discarded=pet_pcm_reset(queue,g,t);
    atomic_store(&generation,g);atomic_store(&turn,t);
    finish_requested=false;drain_sent=false;buffered_announced=false;
    xSemaphoreGive(state_lock);
    xSemaphoreTake(io_lock,portMAX_DELAY);pet_board()->audio_power(false);xSemaphoreGive(io_lock);
    while(discarded--)if(cb.credit)cb.credit(old_g,old_t,cb.context);
}
void pet_audio_begin(uint32_t g,uint32_t t){
    pet_audio_clear(g,t);pet_audio_power(true);
    xSemaphoreTake(state_lock,portMAX_DELAY);hpf_x=hpf_y=0;xSemaphoreGive(state_lock);
    if(atomic_load(&powered))atomic_store(&recording,true);
}
void pet_audio_finish(void){
    xSemaphoreTake(state_lock,portMAX_DELAY);finish_requested=true;
    bool active=atomic_load(&recording);uint32_t g=atomic_load(&generation),t=atomic_load(&turn);
    xSemaphoreGive(state_lock);
    if(!active&&cb.stopped)cb.stopped(g,t,cb.context);
}
int pet_audio_push(uint32_t g,uint32_t t,const void *pcm,size_t bytes){
    if(!queue||atomic_load(&recording)||!owns(g,t))return 0;
    if(!atomic_load(&powered)){pet_audio_power(true);if(!atomic_load(&powered))return -1;}
    xSemaphoreTake(state_lock,portMAX_DELAY);
    int result=pet_pcm_push(queue,g,t,pcm,bytes);
    if(result==1){drain_sent=false;if(!buffered_announced){buffered_announced=true;event(PET_EV_BUFFERED,g,t,0);}}
    xSemaphoreGive(state_lock);
    return result;
}
void pet_audio_flush_playback(uint32_t g,uint32_t t){
    if(!queue||!owns(g,t))return;
    xSemaphoreTake(state_lock,portMAX_DELAY);
    atomic_fetch_add(&playback_epoch,1);
    unsigned discarded=pet_pcm_reset(queue,g,t);drain_sent=false;buffered_announced=false;
    xSemaphoreGive(state_lock);
    xSemaphoreTake(io_lock,portMAX_DELAY);pet_board()->audio_power(false);xSemaphoreGive(io_lock);
    while(discarded--)if(cb.credit)cb.credit(g,t,cb.context);
}
void pet_audio_done(uint32_t g,uint32_t t){
    xSemaphoreTake(state_lock,portMAX_DELAY);bool ok=pet_pcm_done(queue,g,t);xSemaphoreGive(state_lock);
    if(ok)event(PET_EV_RESPONSE_DONE,g,t,0);
}
static void capture_block(void){
    int16_t stereo[640],mono[320];
    uint32_t g=atomic_load(&generation),t=atomic_load(&turn);
    xSemaphoreTake(io_lock,portMAX_DELAY);
    bool ok=atomic_load(&powered)&&esp_codec_dev_read(microphone,stereo,sizeof(stereo))==ESP_CODEC_DEV_OK;
    xSemaphoreGive(io_lock);
    if(!ok){atomic_store(&recording,false);fault();return;}
    xSemaphoreTake(state_lock,portMAX_DELAY);
    bool valid=owns(g,t)&&atomic_load(&recording);
    if(valid){
        for(unsigned i=0;i<320;i++){
            float x=stereo[i*2];float y=.96954f*(hpf_y+x-hpf_x);hpf_x=x;hpf_y=y;
            mono[i]=(int16_t)(y>32767?32767:y< -32768?-32768:y);
        }
        captured_frames+=320;
    }
    bool finish=valid&&finish_requested;
    if(finish){finish_requested=false;atomic_store(&recording,false);}
    xSemaphoreGive(state_lock);
    if(valid&&cb.capture)cb.capture(g,t,mono,sizeof(mono),cb.context);
    if(finish&&cb.stopped)cb.stopped(g,t,cb.context);
}
static void playback_block(const pet_pcm_packet_t *packet){
    int16_t stereo[640];double energy=0;
    unsigned frames=packet->bytes/2;
    for(unsigned i=0;i<frames;i++){
        stereo[2*i]=stereo[2*i+1]=packet->pcm[i];energy+=(double)packet->pcm[i]*packet->pcm[i];
    }
    xSemaphoreTake(io_lock,portMAX_DELAY);
    bool valid=packet->epoch==atomic_load(&playback_epoch)&&owns(packet->generation,packet->turn)&&!atomic_load(&recording)&&atomic_load(&powered);
    bool ok=true;int level=0;
    if(valid){
        pet_board()->audio_power(enabled);
        ok=esp_codec_dev_write(speaker,stereo,(int)(frames*4))==ESP_CODEC_DEV_OK;
        level=enabled?(int)(sqrt(energy/(frames?frames:1))/32.768):0;
    }
    xSemaphoreGive(io_lock);
    if(valid&&ok){
        xSemaphoreTake(state_lock,portMAX_DELAY);played_frames+=frames;last_write_ms=now_ms();
        bool notify=last_write_ms-last_level_ms>=40||!last_level_ms;
        if(notify)last_level_ms=last_write_ms;
        xSemaphoreGive(state_lock);
        if(notify)event(PET_EV_PLAYING,packet->generation,packet->turn,level>1000?1000:level);
    }else if(!ok)fault();
    if(cb.credit)cb.credit(packet->generation,packet->turn,cb.context);
}
static void audio_task(void *unused){
    (void)unused;
    for(;;){
        if(atomic_load(&recording)){capture_block();continue;}
        pet_pcm_packet_t packet;
        xSemaphoreTake(state_lock,portMAX_DELAY);
        bool have=pet_pcm_pop(queue,&packet);
        if(have)packet.epoch=atomic_load(&playback_epoch);
        bool drained=!have&&!drain_sent&&pet_pcm_drained(queue)&&now_ms()-last_write_ms>=120;
        uint32_t g=queue->generation,t=queue->turn;
        if(drained){drain_sent=true;buffered_announced=false;event(PET_EV_DRAIN,g,t,0);}
        xSemaphoreGive(state_lock);
        if(have){playback_block(&packet);continue;}
        if(drained){
            xSemaphoreTake(io_lock,portMAX_DELAY);pet_board()->audio_power(false);xSemaphoreGive(io_lock);
        }
        /* Keep capture DMA near the present while awake; these idle samples never leave the device. */
        xSemaphoreTake(io_lock,portMAX_DELAY);
        if(atomic_load(&powered)){
            int16_t discard[640];esp_codec_dev_read(microphone,discard,sizeof(discard));
        }
        xSemaphoreGive(io_lock);
        vTaskDelay(pet_delay_ticks(5,configTICK_RATE_HZ));
    }
}
esp_err_t pet_audio_start(const pet_config_t *config,pet_audio_callbacks_t callbacks){
    if(queue||!pet_config_valid(config))return ESP_ERR_INVALID_STATE;
    cb=callbacks;state_lock=xSemaphoreCreateMutex();io_lock=xSemaphoreCreateMutex();
    queue=heap_caps_calloc(1,sizeof(*queue),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!queue||!state_lock||!io_lock)return ESP_ERR_NO_MEM;
    volume=config->volume;gain=config->mic_gain;enabled=config->speaker!=0;
    esp_err_t err=pet_board_audio_init(&speaker,&microphone);
    if(err!=ESP_OK)return err;
    pet_audio_power(true);
    if(!atomic_load(&powered))return ESP_FAIL;
    return xTaskCreatePinnedToCore(audio_task,"pet_audio",8192,NULL,6,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
pet_audio_status_t pet_audio_status(void){
    pet_audio_status_t status={.recording=atomic_load(&recording),.powered=atomic_load(&powered),.fault=atomic_load(&faulted)};
    if(state_lock){xSemaphoreTake(state_lock,portMAX_DELAY);status.queued=queue->count;status.played_frames=played_frames;status.captured_frames=captured_frames;xSemaphoreGive(state_lock);}
    return status;
}
