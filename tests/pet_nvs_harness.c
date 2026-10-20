#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet_store.h"
#include "nvs.h"
#include "pet_caption.h"
static int mode,writes,commits;
static bool saved,caption_saved,blob_dirty,caption_dirty,fail_caption_commit,fail_caption_set,caption_read_error;
static uint8_t caption_durable,caption_pending;
static pet_config_t durable,pending;
int nvs_flash_init(void){return mode==3?ESP_FAIL:ESP_OK;}
int nvs_open(const char *name,int access,nvs_handle_t *h){
    *h=!strcmp(name,"grok_pet")?1:!strcmp(name,"muse")?2:!strcmp(name,"homehub")?3:!strcmp(name,"grok_local")?4:0;
    assert(*h);
    if(access==NVS_READWRITE){assert(*h==1);writes++;}
    if(*h==1&&!saved&&access==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){
    const char *value=NULL;
    if(h==3&&!strcmp(key,"ssid"))value="fixture-network";
    if(h==3&&!strcmp(key,"password"))value="fixture-password";
    if(h==3&&!strcmp(key,"wifi_others"))value=mode==1?"{invalid":"[{\"s\":\"second-network\",\"p\":\"fixture-pass\"}]";
    if(h==4&&!strcmp(key,"host"))value="192.168.1.9";
    if(!value||strlen(value)+1>*n)return ESP_ERR_NVS_NOT_FOUND;
    *n=strlen(value)+1;strcpy(out,value);return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){
    if(h==1&&!strcmp(key,"caption_mode")){
        if(caption_read_error)return ESP_FAIL;
        if(!caption_saved)return ESP_ERR_NVS_NOT_FOUND;
        *out=caption_durable;return ESP_OK;
    }
    if(h!=2)return ESP_ERR_NVS_NOT_FOUND;
    if(!strcmp(key,"volume"))*out=89;else if(!strcmp(key,"mic_gain"))*out=30;
    else if(!strcmp(key,"bright"))*out=100;else if(!strcmp(key,"speaker"))*out=1;
    else return ESP_ERR_NVS_NOT_FOUND;
    return ESP_OK;
}
int nvs_get_u16(nvs_handle_t h,const char *key,uint16_t *out){
    if(h!=4||strcmp(key,"port"))return ESP_ERR_NVS_NOT_FOUND;
    *out=8790;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){
    if(h==4&&!strcmp(key,"key")&&*n>=32){memset(out,0x55,32);*n=32;return ESP_OK;}
    if(h==1&&saved&&*n>=sizeof(durable)){memcpy(out,&durable,sizeof(durable));*n=sizeof(durable);return ESP_OK;}
    return ESP_ERR_NVS_NOT_FOUND;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t n){
    assert(h==1&&!strcmp(key,"settings")&&n==sizeof(pending));memcpy(&pending,value,n);blob_dirty=true;return ESP_OK;
}
int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){
    assert(h==1&&!strcmp(key,"caption_mode"));if(fail_caption_set)return ESP_FAIL;
    caption_pending=value;caption_dirty=true;return ESP_OK;
}
int nvs_commit(nvs_handle_t h){
    assert(h==1);if(mode==2||fail_caption_commit){blob_dirty=caption_dirty=false;return ESP_FAIL;}
    if(blob_dirty){durable=pending;saved=true;blob_dirty=false;}
    if(caption_dirty){caption_durable=caption_pending;caption_saved=true;caption_dirty=false;}
    commits++;return ESP_OK;
}
int main(int argc,char **argv){
    assert(argc==2);mode=atoi(argv[1]);pet_config_t c;
    if(mode){assert(!pet_store_load(&c));assert(!saved&&!commits);if(mode!=2)assert(!writes);}
    else {
        assert(pet_store_load(&c));assert(c.network_count==2&&c.volume==89&&c.bridge_paired&&commits==1);
        c.volume=100;assert(pet_store_save(&c));assert(commits==2);
        memset(&c,0,sizeof(c));assert(pet_store_load(&c)&&c.volume==100&&commits==2);
        c.volume=101;assert(!pet_store_save(&c)&&commits==2);
        pet_caption_mode_t caption=PET_CAPTION_OFF;
        assert(pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_COMPACT&&commits==2);
        c.volume=100;c.captions=0;assert(pet_store_save(&c));
        assert(pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_OFF);
        pet_config_t original=durable;
        assert(pet_store_caption_mode_save(PET_CAPTION_FULL));
        caption=PET_CAPTION_OFF;assert(pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_FULL);
        assert(!memcmp(&durable,&original,sizeof(durable)));
        int before=commits;
        assert(!pet_store_caption_mode_save((pet_caption_mode_t)99)&&commits==before);
        caption_durable=99;caption=PET_CAPTION_OFF;
        assert(!pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_OFF);
        caption_durable=PET_CAPTION_FULL;caption_read_error=true;
        assert(!pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_OFF);caption_read_error=false;
        fail_caption_set=true;assert(!pet_store_caption_mode_save(PET_CAPTION_COMPACT));fail_caption_set=false;
        fail_caption_commit=true;assert(!pet_store_caption_mode_save(PET_CAPTION_COMPACT));fail_caption_commit=false;
        assert(pet_store_caption_mode_load(&caption)&&caption==PET_CAPTION_FULL);
        assert(!memcmp(&durable,&original,sizeof(durable))&&commits==before);
    }
    puts("PET_NVS_OK");
}
