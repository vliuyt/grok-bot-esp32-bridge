#include "pet_store.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t lock;
static StaticSemaphore_t lock_storage;
static bool initialized;
static bool init(void) {
    if(initialized)return true;
    /* A damaged or incompatible NVS is an explicit recovery condition. Never erase it. */
    if(nvs_flash_init()!=ESP_OK)return false;
    lock=xSemaphoreCreateMutexStatic(&lock_storage);
    initialized=lock!=NULL;
    return initialized;
}
static void read_str(nvs_handle_t h,const char *key,char *out,size_t cap) {
    size_t n=cap;
    if(nvs_get_str(h,key,out,&n)!=ESP_OK)out[0]=0;
}
static void add_network(pet_legacy_t *old,const char *ssid,const char *pass,bool hidden) {
    if(!ssid||!ssid[0]||strlen(ssid)>32||strlen(pass?pass:"")>64||old->network_count>=PET_NETWORK_MAX)return;
    for(unsigned i=0;i<old->network_count;i++)if(!strcmp(old->networks[i].ssid,ssid))return;
    pet_network_config_t *n=&old->networks[old->network_count++];
    strcpy(n->ssid,ssid);strcpy(n->password,pass?pass:"");n->hidden=hidden;
}
static bool legacy(pet_config_t *out) {
    pet_legacy_t old={0};nvs_handle_t h;
    pet_config_t defaults;pet_config_defaults(&defaults);
    old.volume=defaults.volume;old.mic_gain=defaults.mic_gain;old.brightness=defaults.brightness;old.speaker=defaults.speaker;
    if(nvs_open("muse",NVS_READONLY,&h)==ESP_OK) {
        old.audio_present=true;
        nvs_get_u8(h,"volume",&old.volume);nvs_get_u8(h,"mic_gain",&old.mic_gain);
        nvs_get_u8(h,"bright",&old.brightness);nvs_get_u8(h,"speaker",&old.speaker);
        nvs_close(h);
    }
    if(nvs_open("homehub",NVS_READONLY,&h)==ESP_OK) {
        char ssid[33]={0},pass[65]={0},hidden[2]={0};
        read_str(h,"ssid",ssid,sizeof(ssid));read_str(h,"password",pass,sizeof(pass));read_str(h,"wifi_hidden",hidden,sizeof(hidden));
        add_network(&old,ssid,pass,hidden[0]=='1');pet_config_wipe(pass,sizeof(pass));
        char *text=calloc(1,1536);
        if(text) {
            read_str(h,"wifi_others",text,1536);
            cJSON *json=cJSON_Parse(text),*item;
            bool malformed=text[0] && !cJSON_IsArray(json);
            if(cJSON_IsArray(json))cJSON_ArrayForEach(item,json) {
                cJSON *s=cJSON_GetObjectItemCaseSensitive(item,"s"),*p=cJSON_GetObjectItemCaseSensitive(item,"p");
                if(cJSON_IsString(s))add_network(&old,s->valuestring,cJSON_IsString(p)?p->valuestring:"",cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item,"h")));
                if(cJSON_IsString(p))pet_config_wipe(p->valuestring,(unsigned)strlen(p->valuestring));
            }
            cJSON_Delete(json);pet_config_wipe(text,1536);free(text);
            if(malformed){nvs_close(h);pet_config_wipe(&old,sizeof(old));return false;}
        }else { nvs_close(h);pet_config_wipe(&old,sizeof(old));return false; }
        nvs_close(h);
    }
    if(nvs_open("grok_local",NVS_READONLY,&h)==ESP_OK) {
        size_t n=sizeof(old.host),k=sizeof(old.key);
        bool host=nvs_get_str(h,"host",old.host,&n)==ESP_OK;
        bool port=nvs_get_u16(h,"port",&old.port)==ESP_OK;
        bool key=nvs_get_blob(h,"key",old.key,&k)==ESP_OK&&k==sizeof(old.key);
        nvs_close(h);
        if(host||port||key) {
            if(!host||!port||!key){pet_config_wipe(&old,sizeof(old));return false;}
            old.bridge_present=true;
        }
    }
    bool ok=pet_config_migrate(out,&old);
    pet_config_wipe(&old,sizeof(old));pet_config_wipe(&defaults,sizeof(defaults));
    return ok;
}
static bool save(const pet_config_t *config) {
    nvs_handle_t h;
    if(nvs_open("grok_pet",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_blob(h,"settings",config,sizeof(*config));
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);return err==ESP_OK;
}
bool pet_store_load(pet_config_t *out) {
    if(!out||!init())return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    nvs_handle_t h;esp_err_t err=nvs_open("grok_pet",NVS_READONLY,&h);
    if(err==ESP_OK) {
        size_t size=sizeof(*out);err=nvs_get_blob(h,"settings",out,&size);nvs_close(h);
        if(err==ESP_OK) {
            bool ok=size==sizeof(*out)&&pet_config_valid(out);
            xSemaphoreGive(lock);return ok;
        }
    }
    bool ok=err==ESP_ERR_NVS_NOT_FOUND && legacy(out);
    if(ok)ok=save(out);
    xSemaphoreGive(lock);return ok;
}
bool pet_store_save(const pet_config_t *config) {
    if(!pet_config_valid(config)||!init())return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=save(config);
    xSemaphoreGive(lock);return ok;
}

bool pet_store_caption_mode_load(pet_caption_mode_t *out) {
    if(!out||!init())return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    nvs_handle_t h;uint8_t value=0;
    esp_err_t err=nvs_open("grok_pet",NVS_READONLY,&h);
    if(err==ESP_OK){err=nvs_get_u8(h,"caption_mode",&value);nvs_close(h);}
    xSemaphoreGive(lock);
    if(err==ESP_ERR_NVS_NOT_FOUND){
        pet_config_t old;bool ok=pet_store_load(&old);
        if(ok)*out=old.captions?PET_CAPTION_COMPACT:PET_CAPTION_OFF;
        pet_config_wipe(&old,sizeof(old));return ok;
    }
    if(err!=ESP_OK||!pet_caption_mode_valid((pet_caption_mode_t)value))return false;
    *out=(pet_caption_mode_t)value;return true;
}
bool pet_store_caption_mode_save(pet_caption_mode_t mode) {
    if(!pet_caption_mode_valid(mode)||!init())return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    nvs_handle_t h;esp_err_t err=nvs_open("grok_pet",NVS_READWRITE,&h);
    if(err==ESP_OK){
        err=nvs_set_u8(h,"caption_mode",(uint8_t)mode);
        if(err==ESP_OK)err=nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(lock);return err==ESP_OK;
}
