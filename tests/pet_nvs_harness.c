#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet_store.h"
#include "nvs.h"
static int mode,writes,commits;
static bool saved;
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
    assert(h==1&&!strcmp(key,"settings")&&n==sizeof(pending));memcpy(&pending,value,n);return ESP_OK;
}
int nvs_commit(nvs_handle_t h){
    assert(h==1);if(mode==2)return ESP_FAIL;
    durable=pending;saved=true;commits++;return ESP_OK;
}
int main(int argc,char **argv){
    assert(argc==2);mode=atoi(argv[1]);pet_config_t c;
    if(mode){assert(!pet_store_load(&c));assert(!saved&&!commits);if(mode!=2)assert(!writes);}
    else {
        assert(pet_store_load(&c));assert(c.network_count==2&&c.volume==89&&c.bridge_paired&&commits==1);
        c.volume=100;assert(pet_store_save(&c));assert(commits==2);
        memset(&c,0,sizeof(c));assert(pet_store_load(&c)&&c.volume==100&&commits==2);
        c.volume=101;assert(!pet_store_save(&c)&&commits==2);
    }
    puts("PET_NVS_OK");
}
