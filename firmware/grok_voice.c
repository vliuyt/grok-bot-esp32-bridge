// Local Grok voice transport. Board, display, avatar and codec drivers are reused.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "muse_voice.h"
#include "muse_input.h"
#include "muse_audio.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_wifi.h"
#include "muse_chat_priv.h"
#include "grok_wire.h"

static QueueHandle_t events;
static atomic_bool online, recording, reconfigure, monitor, resting;
static atomic_uint turn, played_frames;
static atomic_int meter;
typedef struct { uint16_t bytes; uint8_t pcm[GROK_AUDIO_BYTES]; } audio_packet;
static audio_packet *playback;
static unsigned head, count;
static esp_tls_t *connection;
static int socket_fd;
static uint8_t *rx;
static size_t rx_size;
static bool ready, done, button_held;
static int64_t pressed_at, ping_at, received_at, connected_at;
static int16_t mic[MUSE_AUDIO_CHUNK], silence[MUSE_AUDIO_CHUNK];

static bool private_host(const char *host) {
    struct in_addr a;
    if (!inet_aton(host,&a)) return false;
    uint32_t n=ntohl(a.s_addr);
    return (n>>24)==10 || (n>>20)==0xac1 || (n>>16)==0xc0a8;
}
bool grok_console_command(const char *line) {
    if (!strcmp(line,"grok.status")) {
        printf("@grok {\"online\":%s,\"recording\":%s,\"turn\":%u,\"playedFrames\":%u}\n",atomic_load(&online)?"true":"false",atomic_load(&recording)?"true":"false",atomic_load(&turn),atomic_load(&played_frames)); fflush(stdout); return true;
    }
    if (!strcmp(line,"grok.ptt down") || !strcmp(line,"grok.ptt up")) {
        muse_input_event_t event={.type=strstr(line,"down")?MUSE_PTT_DOWN:MUSE_PTT_UP,.wake=false};
        bool ok=events && xQueueSend(events,&event,0)==pdTRUE;
        printf("@grok.ptt %s\n",ok?"ok":"error"); fflush(stdout); return true;
    }
    if (strncmp(line,"grok.configure ",15)) return false;
    char host[64],key_text[65],extra; uint8_t key[32]; unsigned port;
    bool valid=sscanf(line+15,"%63s %u %64s %c",host,&port,key_text,&extra)==3 && port>0 && port<=65535 && private_host(host) && grok_key(key_text,key);
    nvs_handle_t nvs;
    esp_err_t result=ESP_FAIL;
    if (valid && nvs_open("grok_local",NVS_READWRITE,&nvs)==ESP_OK) {
        result=nvs_set_str(nvs,"host",host);
        if(result==ESP_OK) result=nvs_set_u16(nvs,"port",(uint16_t)port);
        if(result==ESP_OK) result=nvs_set_blob(nvs,"key",key,sizeof(key));
        if(result==ESP_OK) result=nvs_commit(nvs);
        nvs_close(nvs);
    }
    memset(key,0,sizeof(key)); memset(key_text,0,sizeof(key_text));
    if(result==ESP_OK) atomic_store(&reconfigure,true);
    printf("@grok.configure %s\n",result==ESP_OK?"ok":"error"); fflush(stdout); return true;
}
static bool load_config(char host[64],uint16_t *port,uint8_t key[32]) {
    nvs_handle_t nvs; if(nvs_open("grok_local",NVS_READONLY,&nvs)!=ESP_OK) return false;
    size_t h=64,k=32;
    bool ok=nvs_get_str(nvs,"host",host,&h)==ESP_OK && nvs_get_u16(nvs,"port",port)==ESP_OK && nvs_get_blob(nvs,"key",key,&k)==ESP_OK && k==32;
    nvs_close(nvs); return ok && private_host(host) && *port>0;
}
static bool write_all(const uint8_t *data,size_t n) {
    int64_t deadline=esp_timer_get_time()+500000;
    while(n) {
        ssize_t wrote=esp_tls_conn_write(connection,data,n);
        if(wrote>0) { data+=wrote;n-=(size_t)wrote;continue; }
        if(wrote!=ESP_TLS_ERR_SSL_WANT_READ && wrote!=ESP_TLS_ERR_SSL_WANT_WRITE) return false;
        if(esp_timer_get_time()>deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    } return true;
}
static bool send_packet(uint8_t type,const void *data,size_t n) {
    if(n>GROK_AUDIO_BYTES) return false;
    uint8_t frame[9+GROK_AUDIO_BYTES]; grok_put32(frame,5+n);frame[4]=type;grok_put32(frame+5,atomic_load(&turn));
    if(n) memcpy(frame+9,data,n);
    return write_all(frame,n+9);
}
static bool control(const char *name) { return send_packet(2,name,strlen(name)); }
static void clear_playback(void) { head=count=0;done=false; }
static bool push_to_talk(muse_ptt_t type);
static bool receive_frame(uint8_t type,uint32_t generation,const uint8_t *data,size_t n) {
    if(type==3) {
        if(!grok_valid_audio(n)) return false;
        if(generation!=atomic_load(&turn) || atomic_load(&recording)) return true;
        if(count>=GROK_PLAY_SLOTS) return false;
        audio_packet *packet=&playback[(head+count)%GROK_PLAY_SLOTS]; packet->bytes=n;memcpy(packet->pcm,data,n);count++;return true;
    }
    if(n>32) return false;
    if(n==5 && !memcmp(data,"ready",5)) {
        ready=true;atomic_store(&online,true);muse_hatch_report(MUSE_HATCH_REACHABLE,"Grok voice");muse_state_set_caption("GROK CONNECTED");
        return !button_held || push_to_talk(MUSE_PTT_DOWN);
    }
    if(n==4 && !memcmp(data,"pong",4)) return true;
    if(generation!=atomic_load(&turn)) return true;
    if(n==5 && !memcmp(data,"clear",5)) {
        // Ordered TLS ensures all older audio precedes clear. Return credits
        // for packets discarded locally, as well as those already played.
        for(unsigned i=0;i<count;i++) if(!control("credit")) return false;
        clear_playback();return true;
    }
    if(n==4 && !memcmp(data,"sent",4)) return true;
    if(n==4 && !memcmp(data,"done",4)) { done=true;return true; }
    return false;
}
static bool receive_packets(void) {
    if(rx_size>=GROK_FRAME_MAX+4) return false;
    ssize_t n=esp_tls_conn_read(connection,rx+rx_size,GROK_FRAME_MAX+4-rx_size);
    if(n==ESP_TLS_ERR_SSL_WANT_READ || n==ESP_TLS_ERR_SSL_WANT_WRITE) return true;
    if(n<=0) return false;
    received_at=esp_timer_get_time();
    rx_size+=(size_t)n;
    size_t used=0;
    while(rx_size-used>=4) {
        uint32_t length=grok_u32(rx+used);
        if(length<5 || length>GROK_FRAME_MAX) return false;
        if(rx_size-used<length+4) break;
        const uint8_t *p=rx+used+4;
        if(!grok_valid_frame(length,p[0]) || !receive_frame(p[0],grok_u32(p+1),p+5,length-5)) return false;
        used+=length+4;
    }
    if(used) { memmove(rx,rx+used,rx_size-used);rx_size-=used; } return true;
}
static bool connect_bridge(void) {
    char host[64];uint16_t port;uint8_t key[32];
    if(!load_config(host,&port,key)) { muse_state_set_caption("PAIR GROK ON MAC");return false; }
    psk_hint_key_t psk={.key=key,.key_size=sizeof(key),.hint="grok-esp32"};
    esp_tls_cfg_t cfg={.timeout_ms=5000,.psk_hint_key=&psk};
    connection=esp_tls_init();
    if(!connection) return false;
    int result=esp_tls_conn_new_sync(host,strlen(host),port,&cfg,connection);
    memset(key,0,sizeof(key));
    if(result!=1 || esp_tls_get_conn_sockfd(connection,&socket_fd)!=ESP_OK || fcntl(socket_fd,F_SETFL,fcntl(socket_fd,F_GETFL,0)|O_NONBLOCK)<0) {
        esp_tls_conn_destroy(connection);connection=NULL;return false;
    }
    ready=false;rx_size=0;clear_playback();atomic_store(&turn,0);ping_at=received_at=connected_at=esp_timer_get_time();return true;
}
static void disconnect_bridge(void) {
    atomic_store(&online,false);atomic_store(&recording,false);ready=false;button_held=false;clear_playback();
    if(connection) esp_tls_conn_destroy(connection);
    connection=NULL;
    muse_hatch_report(MUSE_HATCH_UNREACHABLE,"Mac disconnected");
    muse_state_set_mode(MUSE_MODE_IDLE);muse_state_set_caption("CONNECT GROK ON MAC");
    // New connection starts a fresh call. No recording or task is replayed.
    muse_input_event_t event;while(xQueueReceive(events,&event,0)==pdTRUE) {}
}
static bool push_to_talk(muse_ptt_t type) {
    if(type==MUSE_PTT_DOWN && ready && !atomic_load(&recording)) {
        clear_playback();atomic_fetch_add(&turn,1);atomic_store(&recording,true);pressed_at=esp_timer_get_time();
        muse_state_poke();muse_state_set_mode(MUSE_MODE_LISTENING);muse_state_set_caption("LISTENING...");
        return control("press");
    }
    if(type==MUSE_PTT_UP && atomic_load(&recording)) {
        atomic_store(&recording,false);muse_state_set_mode(MUSE_MODE_THINKING);muse_state_set_caption("WAITING FOR GROK...");return control("release");
    } return true;
}
static void voice_task(void *unused) {
    (void)unused;
    for(;;) {
        if(muse_state_asleep() && muse_state_on_battery()) {
            atomic_store(&resting,true);muse_audio_power(false);muse_wifi_power(MUSE_WIFI_REST);
            muse_state_wait_awake(60000);
            muse_wifi_power(MUSE_WIFI_FULL);muse_audio_power(true);atomic_store(&resting,false);
            continue;
        }
        atomic_store(&reconfigure,false);
        if(!muse_wifi_connected() || !connect_bridge()) { vTaskDelay(pdMS_TO_TICKS(3000));continue; }
        bool ok=true;
        while(ok && muse_wifi_connected() && !atomic_load(&reconfigure)) {
            if(muse_state_asleep() && muse_state_on_battery() && !atomic_load(&recording) && !count) break;
            if(esp_timer_get_time()-received_at>20000000) break;
            if(!ready && esp_timer_get_time()-connected_at>30000000) break;
            muse_input_event_t event;
            while(ok && xQueueReceive(events,&event,0)==pdTRUE) {
                button_held=event.type==MUSE_PTT_DOWN;
                ok=push_to_talk(event.type);
            }
            if(!ok) break;
            if(atomic_load(&recording) && esp_timer_get_time()-pressed_at>55000000) ok=push_to_talk(MUSE_PTT_UP);
            if(!ok || !receive_packets()) break;
            if(muse_audio_read(mic,MUSE_AUDIO_CHUNK)!=ESP_OK) break;
            if(atomic_load(&monitor)) atomic_store(&meter,(int)(muse_audio_dbfs(mic,MUSE_AUDIO_CHUNK)*10));
            if(atomic_load(&recording)) {
                muse_state_set_level(muse_audio_level(mic,MUSE_AUDIO_CHUNK));ok=send_packet(3,mic,sizeof(mic));
            }
            if(count && !atomic_load(&recording)) {
                audio_packet *p=&playback[head];
                muse_state_set_mode(MUSE_MODE_SPEAKING);muse_state_set_caption("GROK SPEAKING...");
                muse_state_set_level(muse_audio_level((const int16_t*)p->pcm,p->bytes/2));
                if(muse_audio_write((const int16_t*)p->pcm,p->bytes/2)!=ESP_OK) break;
                atomic_fetch_add(&played_frames,p->bytes/2);head=(head+1)%GROK_PLAY_SLOTS;count--;ok=control("credit");
            } else {
                if(muse_audio_write(silence,MUSE_AUDIO_CHUNK)!=ESP_OK) break;
                if(done && !atomic_load(&recording)) { done=false;muse_state_set_level(0);muse_state_set_mode(MUSE_MODE_IDLE);muse_state_set_caption("GROK CONNECTED"); }
            }
            if(esp_timer_get_time()-ping_at>5000000) { ping_at=esp_timer_get_time();ok=control("ping"); }
        }
        disconnect_bridge();vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
esp_err_t muse_voice_start(QueueHandle_t queue) {
    events=queue;playback=heap_caps_calloc(GROK_PLAY_SLOTS,sizeof(audio_packet),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    rx=heap_caps_malloc(GROK_FRAME_MAX+4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!playback || !rx || muse_audio_init(muse_settings_volume(),muse_settings_mic_gain())!=ESP_OK) return ESP_FAIL;
    return xTaskCreate(voice_task,"grok_voice",8192,NULL,6,NULL)==pdPASS?ESP_OK:ESP_FAIL;
}
void muse_voice_set_monitor(bool on) { atomic_store(&monitor,on); }
float muse_voice_monitor_db(void) { return atomic_load(&meter)/10.0f; }
void muse_voice_request_chirp(void) {}
void muse_voice_request_loopback(void) {}
void muse_voice_request_mp3test(void) {}
bool muse_voice_resting(void) { return atomic_load(&resting); }
bool muse_voice_notes_waiting(void) { return false; }
