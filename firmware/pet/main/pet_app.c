#include "pet_app.h"
#include "board_port.h"
#include "pet_store.h"
#include "pet_audio.h"
#include "pet_ui.h"
#include "pet_network.h"
#include "pet_link.h"
#include "pet_provision.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pet_state_t model;
static pet_config_t config;
static pet_ui_view_t ui;
static pet_transport_t transport;
static QueueHandle_t urgent,events,actions;
static SemaphoreHandle_t model_lock,config_lock,metadata_lock;
static atomic_bool emergency;
static atomic_uint phase,emergency_reasons,event_drops;
static bool dirty,network_connected,mac_connected;
static uint64_t dirty_at,last_power;
static pet_caption_buffer_t captions;
static pet_caption_mode_t caption_mode=PET_CAPTION_COMPACT;
static bool caption_save_failed;
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
pet_state_t pet_app_state(void){pet_state_t s;xSemaphoreTake(model_lock,portMAX_DELAY);s=model;xSemaphoreGive(model_lock);return s;}
pet_config_t pet_app_config(void){pet_config_t c;xSemaphoreTake(config_lock,portMAX_DELAY);c=config;xSemaphoreGive(config_lock);return c;}
void pet_app_bind_transport(pet_transport_t ops){transport=ops;}
bool pet_app_post(pet_event_t event){
    if(!events)return false;
    if(!event.now_ms)event.now_ms=now_ms();
    bool critical=pet_event_urgent(event.kind);
    bool ok=xQueueSend(critical?urgent:events,&event,0)==pdTRUE;
    if(!ok&&event.kind!=PET_EV_PLAYING){atomic_fetch_add(&event_drops,1);atomic_fetch_or(&emergency_reasons,1);atomic_store(&emergency,true);}
    return ok;
}
static bool control(const char *name,uint32_t g,uint32_t t){
    if(!transport.control)return false;
    bool ok=transport.control(name,g,t,transport.context);
    if(!ok){atomic_fetch_or(&emergency_reasons,2);atomic_store(&emergency,true);}
    return ok;
}
static void effects(uint32_t fx,pet_event_kind_t kind){
    pet_state_t s=pet_app_state();atomic_store(&phase,10);
    if(fx&(PET_FX_START|PET_FX_CANCEL)){
        xSemaphoreTake(metadata_lock,portMAX_DELAY);pet_caption_reset(&captions);ui.caption[0]=0;xSemaphoreGive(metadata_lock);
    }
    if(fx&PET_FX_START){
        pet_ui_home();pet_audio_clear(s.generation,s.turn);
        if(control("press",s.generation,s.turn))pet_audio_begin(s.generation,s.turn);
    }else if(fx&PET_FX_SUBMIT){pet_audio_finish();}
    else if(fx&PET_FX_CANCEL){
        atomic_store(&phase,11);pet_audio_clear(s.generation,s.turn);atomic_store(&phase,12);
        if(kind!=PET_EV_LINK&&s.connection==PET_CONN_READY&&transport.control){control("cancel",s.generation,s.turn);}
    }
    if(fx&PET_FX_WAKE){
        pet_network_sleep(false);
        if(transport.sleep){transport.sleep(false,transport.context);}
        pet_audio_power(true);
    }
    if((fx&PET_FX_CONNECT)&&transport.reconnect){transport.reconnect(transport.context);}
    if(fx&PET_FX_SAVE_QUOTA){
        xSemaphoreTake(config_lock,portMAX_DELAY);config.quota_day=s.day;config.quota_count=(uint8_t)s.greetings_today;xSemaphoreGive(config_lock);
        dirty=true;dirty_at=now_ms();
    }
}
static void reduce(pet_event_t event){
    if(event.kind==PET_EV_PRESS&&pet_app_state().setup)pet_provision_open(false);
    xSemaphoreTake(model_lock,portMAX_DELAY);uint32_t fx=pet_step(&model,event);xSemaphoreGive(model_lock);
    effects(fx,event.kind);atomic_store(&phase,13);
}
static void audio_event(pet_event_t event,void *ctx){(void)ctx;pet_app_post(event);}
static void capture(uint32_t g,uint32_t t,const int16_t *pcm,size_t n,void *ctx){
    (void)ctx;if(!transport.capture||!transport.capture(g,t,pcm,n,transport.context)){atomic_fetch_or(&emergency_reasons,4);atomic_store(&emergency,true);}
}
static void stopped(uint32_t g,uint32_t t,void *ctx){
    (void)ctx;pet_state_t s=pet_app_state();
    if(s.generation==g&&s.turn==t&&s.dialogue==PET_DIALOGUE_WAIT){control("release",g,t);}
}
static void credit(uint32_t g,uint32_t t,void *ctx){
    (void)ctx;if(transport.credit&&!transport.credit(g,t,transport.context)){atomic_fetch_or(&emergency_reasons,8);atomic_store(&emergency,true);}
}
static void audio_fault(void *ctx){(void)ctx;atomic_store(&emergency,true);}
static void ui_action(pet_ui_action_t action,void *ctx){
    (void)ctx;if(xQueueSend(actions,&action,0)!=pdTRUE){atomic_store(&emergency,true);}
}
void pet_app_caption(uint32_t g,uint32_t t,const char *text){
    pet_state_t state=pet_app_state();
    xSemaphoreTake(metadata_lock,portMAX_DELAY);
    pet_caption_receive(&captions,&state,g,t,text);
    xSemaphoreGive(metadata_lock);
}
void pet_app_network(bool connected,const char *detail){
    xSemaphoreTake(metadata_lock,portMAX_DELAY);network_connected=connected;
    snprintf(ui.wifi,sizeof(ui.wifi),"%s",detail);xSemaphoreGive(metadata_lock);
}
void pet_app_setup_status(const char *detail,int passkey){
    xSemaphoreTake(metadata_lock,portMAX_DELAY);snprintf(ui.setup,sizeof(ui.setup),"%s",detail);ui.passkey=passkey;xSemaphoreGive(metadata_lock);
}
static void handle_action(pet_ui_action_t action){
    uint64_t now=now_ms();
    if(action.kind==PET_UI_PET||action.kind==PET_UI_TOUCH_TAP||action.kind==PET_UI_WAKE){
        if(action.kind==PET_UI_PET||action.kind==PET_UI_TOUCH_TAP){
            reduce((pet_event_t){.kind=action.kind==PET_UI_PET?PET_EV_PET:PET_EV_TOUCH_TAP,.now_ms=now});
        }
        else {
            xSemaphoreTake(model_lock,portMAX_DELAY);model.last_input_ms=now;model.life=PET_LIFE_AWAKE;xSemaphoreGive(model_lock);
            pet_network_sleep(false);
        if(transport.sleep){transport.sleep(false,transport.context);}
            pet_audio_power(true);
        }
        return;
    }
    if(action.kind==PET_UI_SETUP_OPEN||action.kind==PET_UI_SETUP_CLOSE){
        bool open=action.kind==PET_UI_SETUP_OPEN;
        reduce((pet_event_t){.kind=PET_EV_SETUP,.now_ms=now,.value=open});
        pet_provision_open(open);
        return;
    }
    if(action.kind==PET_UI_CAPTIONS){
        pet_caption_mode_t next=(pet_caption_mode_t)action.value;
        if(pet_caption_mode_valid(next)&&pet_store_caption_mode_save(next)){
            caption_mode=next;caption_save_failed=false;
        }else caption_save_failed=true;
        return;
    }
    xSemaphoreTake(config_lock,portMAX_DELAY);
    switch(action.kind){
    case PET_UI_VOLUME:if(action.value>=0&&action.value<=100){config.volume=(uint8_t)action.value;}break;
    case PET_UI_BRIGHTNESS:if(action.value>=10&&action.value<=100){config.brightness=(uint8_t)action.value;}break;
    case PET_UI_QUIET:config.quiet=action.value!=0;break;
    case PET_UI_SPEAKER:config.speaker=action.value!=0;break;
    default:break;
    }
    pet_config_t current=config;xSemaphoreGive(config_lock);
    pet_audio_settings(&current);
    reduce((pet_event_t){.kind=PET_EV_QUIET,.now_ms=now_ms(),.value=current.quiet});
    reduce((pet_event_t){.kind=PET_EV_MUTED,.now_ms=now_ms(),.value=!current.speaker});
    pet_config_wipe(&current,sizeof(current));dirty=true;dirty_at=now;
}
static void refresh(void){
    uint64_t now=now_ms();pet_state_t s=pet_app_state();pet_config_t c=pet_app_config();
    xSemaphoreTake(metadata_lock,portMAX_DELAY);
    pet_caption_current(&captions,&s,PET_CAPTION_FULL,ui.caption,sizeof(ui.caption));
    ui.caption_mode=caption_mode;ui.caption_save_failed=caption_save_failed;
    ui.pose=pet_pose(&s,now);ui.dialogue=s.dialogue;ui.connection=s.connection;
    ui.volume=c.volume;ui.brightness=c.brightness;ui.quiet=c.quiet;ui.speaker=c.speaker;
    ui.wifi_online=network_connected;ui.mac_online=mac_connected;ui.setup_open=s.setup;
    ui.task_channel=s.task_channel;ui.waiting_long=s.dialogue==PET_DIALOGUE_WAIT&&now-s.entered_ms>=20000;
    snprintf(ui.mac,sizeof(ui.mac),"%s",mac_connected?"Mac 桥接已连接":network_connected?"正在寻找已配对 Mac":"等待网络连接");
    snprintf(ui.voice,sizeof(ui.voice),"%s",s.connection==PET_CONN_READY?(s.task_channel?"小糖原生语音就绪":"语音可用，任务通道恢复中"):"小糖原生语音尚未就绪");
    pet_ui_view_t current=ui;xSemaphoreGive(metadata_lock);
    pet_ui_update(&current);pet_config_wipe(&c,sizeof(c));
}
int pet_app_status(char *out,size_t cap,bool compact){
    pet_state_t s=pet_app_state();pet_audio_status_t a=pet_audio_status();pet_config_t c=pet_app_config();int n;
    if(compact){
        n=snprintf(out,cap,"{\"online\":%s,\"recording\":%s,\"turn\":%u,\"playedFrames\":%u,\"generation\":%u,\"queued\":%u}",
            s.connection==PET_CONN_READY?"true":"false",a.recording?"true":"false",(unsigned)s.turn,(unsigned)a.played_frames,(unsigned)s.generation,a.queued);
    }else {
        xSemaphoreTake(metadata_lock,portMAX_DELAY);bool wifi=network_connected;xSemaphoreGive(metadata_lock);
        n=snprintf(out,cap,"{\"board\":\"Waveshare ESP32-S3-Touch-AMOLED-1.43C\",\"app\":\"grok-pet\",\"device\":{\"volume\":%u,\"speaker\":%s,\"mic_gain\":%u,\"wifi\":{\"state\":\"%s\"},\"link\":{\"paired\":%s,\"state\":\"%s\"},\"hatch\":{\"state\":\"%s\"}},\"pet\":{\"life\":%d,\"dialogue\":%d,\"generation\":%u,\"turn\":%u,\"heap\":%u,\"psram\":%u,\"audioFault\":%s,\"uptimeMs\":%llu,\"connection\":%d,\"linkFlags\":%u,\"loopMs\":%llu,\"phase\":%u,\"urgentDepth\":%u,\"emergencyReasons\":%u,\"eventDrops\":%u,\"stackConsole\":%u,\"stackNetwork\":%u}}",
            c.volume,c.speaker?"true":"false",c.mic_gain,wifi?"connected":"disconnected",c.bridge_paired?"true":"false",
            s.connection==PET_CONN_READY?"Online":"Connecting",s.connection==PET_CONN_READY?"Connected":"Waiting",
            s.life,s.dialogue,(unsigned)s.generation,(unsigned)s.turn,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),a.fault?"true":"false",(unsigned long long)now_ms(),s.connection,pet_link_flags(),(unsigned long long)s.now_ms,atomic_load(&phase),(unsigned)uxQueueMessagesWaiting(urgent),atomic_load(&emergency_reasons),atomic_load(&event_drops),pet_console_stack_free(),pet_network_stack_free());
    }
    pet_config_wipe(&c,sizeof(c));return n;
}
static pet_config_t current_config(void *ctx){(void)ctx;return pet_app_config();}
static bool commit_network(const pet_config_t *candidate,void *ctx){
    (void)ctx;xSemaphoreTake(config_lock,portMAX_DELAY);
    pet_config_t next=config;next.network_count=candidate->network_count;
    memcpy(next.networks,candidate->networks,sizeof(next.networks));
    next.bridge_paired=candidate->bridge_paired;next.port=candidate->port;
    memcpy(next.host,candidate->host,sizeof(next.host));memcpy(next.key,candidate->key,sizeof(next.key));
    memcpy(next.bridge_id,candidate->bridge_id,sizeof(next.bridge_id));
    bool ok=pet_store_save(&next);if(ok)config=next;
    pet_config_wipe(&next,sizeof(next));xSemaphoreGive(config_lock);return ok;
}
static void network_status(bool online,const char *text,void *ctx){(void)ctx;pet_app_network(online,text);}
static void network_result(const char *id,bool ok,void *ctx){(void)ctx;pet_provision_result(id,ok);if(ok&&transport.reconnect)transport.reconnect(transport.context);}
static void setup_status(const char *text,int passkey,void *ctx){(void)ctx;pet_app_setup_status(text,passkey);}
static void setup_window(bool open,void *ctx){(void)ctx;pet_app_post((pet_event_t){.kind=PET_EV_SETUP,.value=open});}
static pet_state_t link_state(void *ctx){(void)ctx;return pet_app_state();}
static bool link_event(pet_event_t e,void *ctx){(void)ctx;return pet_app_post(e);}
static void link_caption(uint32_t g,uint32_t t,const char *text,void *ctx){(void)ctx;pet_app_caption(g,t,text);}
static void link_status(bool online,void *ctx){(void)ctx;xSemaphoreTake(metadata_lock,portMAX_DELAY);mac_connected=online;xSemaphoreGive(metadata_lock);}
void app_main(void){
    model_lock=xSemaphoreCreateMutex();config_lock=xSemaphoreCreateMutex();metadata_lock=xSemaphoreCreateMutex();
    urgent=xQueueCreate(16,sizeof(pet_event_t));events=xQueueCreate(32,sizeof(pet_event_t));actions=xQueueCreate(24,sizeof(pet_ui_action_t));
    if(!model_lock||!config_lock||!metadata_lock||!urgent||!events||!actions){ESP_LOGE("pet","QUEUE_START_FAILED");return;}
    if(!pet_store_load(&config)){ESP_LOGE("pet","CONFIGURATION_RECOVERY_REQUIRED");return;}
    caption_mode=config.captions?PET_CAPTION_COMPACT:PET_CAPTION_OFF;
    caption_save_failed=!pet_store_caption_mode_load(&caption_mode);
    if(pet_board_init()!=ESP_OK){ESP_LOGE("pet","BOARD_START_FAILED");return;}
    pet_init(&model,now_ms());model.seed=esp_random()|1u;model.quiet=config.quiet;model.muted=!config.speaker;
    model.day=config.quota_day;model.greetings_today=config.quota_count;
    ui.battery=-1;snprintf(ui.wifi,sizeof(ui.wifi),"Wi-Fi 正在准备");snprintf(ui.setup,sizeof(ui.setup),"可从此处开启蓝牙配网");
    pet_console_start();
    if(pet_ui_start(ui_action,NULL)!=ESP_OK){ESP_LOGE("pet","UI_START_FAILED");return;}
    pet_audio_callbacks_t audio_cb={.event=audio_event,.capture=capture,.stopped=stopped,.credit=credit,.fault=audio_fault};
    if(pet_audio_start(&config,audio_cb)!=ESP_OK){ESP_LOGE("pet","AUDIO_START_FAILED");}
    pet_provision_callbacks_t provisioning={.current=current_config,.status=setup_status,.window=setup_window};
    if(pet_provision_start(provisioning)!=ESP_OK)pet_app_setup_status("蓝牙不可用，可用 USB 配网",0);
    pet_network_callbacks_t networking={.current=current_config,.commit=commit_network,.status=network_status,.result=network_result};
    if(pet_network_start(networking)!=ESP_OK)pet_app_network(false,"Wi-Fi 启动失败，离线互动可用");
    pet_app_bind_transport((pet_transport_t){.control=pet_link_control,.capture=pet_link_capture,.credit=pet_link_credit,.reconnect=pet_link_reconnect,.sleep=pet_link_sleep});
    if(pet_link_start((pet_link_callbacks_t){.config=current_config,.state=link_state,.event=link_event,.caption=link_caption,.status=link_status})!=ESP_OK)pet_app_setup_status("语音连接启动失败",0);
    bool asleep=false,network_asleep=false;
    for(;;){
        uint64_t now=now_ms();
        if(atomic_exchange(&emergency,false)){reduce((pet_event_t){.kind=PET_EV_CANCEL,.now_ms=now});}
        unsigned buttons=pet_board()->poll_buttons();
        if(buttons&MUSE_BTN_TALK_PRESS){reduce((pet_event_t){.kind=PET_EV_PRESS,.now_ms=now});}
        if(buttons&MUSE_BTN_TALK_RELEASE){reduce((pet_event_t){.kind=PET_EV_RELEASE,.now_ms=now});}
        pet_event_t event;while(xQueueReceive(urgent,&event,0)==pdTRUE){reduce(event);}
        pet_ui_action_t action;while(xQueueReceive(actions,&action,0)==pdTRUE){handle_action(action);}
        for(unsigned i=0;i<32&&xQueueReceive(events,&event,0)==pdTRUE;i++){reduce(event);}
        if(now-last_power>=2000){
            muse_power_t p={.battery_pct=-1};
            atomic_store(&phase,20);if(pet_board_read_power(&p)==ESP_OK){
                reduce((pet_event_t){.kind=PET_EV_USB,.now_ms=now_ms(),.value=p.usb||p.charging});
                xSemaphoreTake(metadata_lock,portMAX_DELAY);ui.battery=p.battery_pct;xSemaphoreGive(metadata_lock);
            }
            last_power=now;
        }
        reduce((pet_event_t){.kind=PET_EV_TICK,.now_ms=now_ms()});pet_state_t s=pet_app_state();
        bool sleep=s.life==PET_LIFE_SLEEP;
        if(sleep!=asleep){
            asleep=sleep;
            if(sleep){pet_audio_clear(s.generation,s.turn);}

            pet_audio_power(!sleep);
        }
        bool network_sleep=pet_network_should_sleep(&s);
        if(network_sleep!=network_asleep){
            network_asleep=network_sleep;
            pet_network_sleep(network_sleep);
            if(transport.sleep)transport.sleep(network_sleep,transport.context);
        }
        if(dirty&&now-dirty_at>=1500&&s.dialogue==PET_DIALOGUE_IDLE){
            xSemaphoreTake(config_lock,portMAX_DELAY);pet_config_t c=config;
            if(pet_store_save(&c)){dirty=false;}else {dirty_at=now;pet_app_setup_status("设置保存失败，可重试",0);}
            pet_config_wipe(&c,sizeof(c));xSemaphoreGive(config_lock);
        }
        atomic_store(&phase,30);refresh();atomic_store(&phase,31);vTaskDelay(pdMS_TO_TICKS(10));
    }
}
