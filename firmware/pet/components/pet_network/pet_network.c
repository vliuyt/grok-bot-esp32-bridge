#include "pet_network.h"
#include "pet_transaction.h"
#include "pet_discovery_filter.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "psa/crypto.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static pet_network_callbacks_t cb;
static EventGroupHandle_t flags;
static QueueHandle_t candidates;
static TaskHandle_t network_handle;
unsigned pet_network_stack_free(void){return network_handle?(unsigned)uxTaskGetStackHighWaterMark(network_handle):0;}
static atomic_bool online,asleep;
typedef struct {pet_config_t config;char id[33];} candidate_t;
#define GOT_IP BIT0
#define FAILED BIT1
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data){
 (void)arg;(void)data;
 if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){atomic_store(&online,true);xEventGroupSetBits(flags,GOT_IP);}
 else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){atomic_store(&online,false);xEventGroupSetBits(flags,FAILED);}
}
void pet_bridge_identity(const uint8_t key[32],char id[33]){
 uint8_t bytes[52],digest[32];const char prefix[]="grokpet-bridge-id-v2";
 size_t n=sizeof(prefix)-1;memcpy(bytes,prefix,n);memcpy(bytes+n,key,32);
 size_t length=0;psa_crypto_init();psa_hash_compute(PSA_ALG_SHA_256,bytes,n+32,digest,sizeof(digest),&length);
 for(unsigned i=0;i<16;i++)snprintf(id+i*2,3,"%02x",digest[i]);
 pet_config_wipe(bytes,sizeof(bytes));pet_config_wipe(digest,sizeof(digest));
}
static bool connect_one(const pet_network_config_t *n){
 esp_wifi_disconnect();vTaskDelay(pdMS_TO_TICKS(100));
 xEventGroupClearBits(flags,GOT_IP|FAILED);
 wifi_config_t wifi={0};memcpy(wifi.sta.ssid,n->ssid,strlen(n->ssid));memcpy(wifi.sta.password,n->password,strlen(n->password));
 wifi.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;wifi.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
 wifi.sta.pmf_cfg.capable=true;wifi.sta.threshold.authmode=n->password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
 esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&wifi);pet_config_wipe(&wifi,sizeof(wifi));
 if(err!=ESP_OK||esp_wifi_connect()!=ESP_OK)return false;
 EventBits_t result=xEventGroupWaitBits(flags,GOT_IP|FAILED,pdTRUE,pdFALSE,pdMS_TO_TICKS(15000));
 return (result&GOT_IP)&&atomic_load(&online);
}
static bool connect_saved(const pet_config_t *c){
 /* Scan ranks visible saved networks. Hidden saved SSIDs are still tried explicitly. */
 wifi_scan_config_t scan={.show_hidden=true};wifi_ap_record_t records[24];uint16_t count=24;bool used[PET_NETWORK_MAX]={0};
 if(esp_wifi_scan_start(&scan,true)==ESP_OK&&esp_wifi_scan_get_ap_records(&count,records)==ESP_OK){
  for(unsigned j=0;j<count;j++)for(unsigned i=0;i<c->network_count;i++){
   if(!used[i]&&!strcmp(c->networks[i].ssid,(char *)records[j].ssid)){used[i]=true;if(connect_one(&c->networks[i]))return true;}
  }
 }
 for(unsigned i=0;i<c->network_count;i++)if(!used[i]&&connect_one(&c->networks[i]))return true;
 return false;
}
static bool candidate_connect(const pet_network_config_t *n,void *ctx){(void)ctx;return connect_one(n);}
static bool candidate_commit(const pet_config_t *c,void *ctx){(void)ctx;return cb.commit(c,cb.context);}
static void candidate_restore(void *ctx){(void)ctx;pet_config_t old=cb.current(cb.context);connect_saved(&old);pet_config_wipe(&old,sizeof(old));}
static void network_task(void *unused){
 (void)unused;unsigned retry=0;
 for(;;){
  candidate_t next;
  if(xQueueReceive(candidates,&next,pdMS_TO_TICKS(500))==pdTRUE){
   cb.status(false,"正在验证新网络",cb.context);
   bool ok=pet_network_transaction(&next.config,(pet_transaction_ops_t){.connect=candidate_connect,.commit=candidate_commit,.restore=candidate_restore});
   cb.result(next.id,ok,cb.context);pet_config_wipe(&next,sizeof(next));retry=0;
  }
  if(atomic_load(&asleep)){if(atomic_load(&online))esp_wifi_disconnect();continue;}
  if(!atomic_load(&online)){
   cb.status(false,"Wi-Fi 连接中，离线互动可用",cb.context);
   pet_config_t c=cb.current(cb.context);bool ok=connect_saved(&c);pet_config_wipe(&c,sizeof(c));
   if(ok){retry=0;cb.status(true,"Wi-Fi 已连接",cb.context);}
   else {cb.status(false,"Wi-Fi 未连接，离线互动可用",cb.context);retry++;}
   /* Interruptible backoff: provisioning and wake stay responsive. */
   for(unsigned i=0;i<(retry>6?60:retry*5)&&!atomic_load(&online)&&uxQueueMessagesWaiting(candidates)==0;i++)vTaskDelay(pdMS_TO_TICKS(100));
  }else {cb.status(true,"Wi-Fi 已连接",cb.context);}
 }
}
esp_err_t pet_network_start(pet_network_callbacks_t callbacks){
 cb=callbacks;flags=xEventGroupCreate();candidates=xQueueCreate(1,sizeof(candidate_t));
 if(!flags||!candidates)return ESP_ERR_NO_MEM;
 esp_err_t err=esp_netif_init();if(err!=ESP_OK)return err;
 err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return err;
 if(!esp_netif_create_default_wifi_sta())return ESP_FAIL;
 wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();if((err=esp_wifi_init(&cfg))!=ESP_OK)return err;
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
 if((err=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK)return err;
 if((err=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK)return err;
 if((err=esp_wifi_start())!=ESP_OK)return err;
 esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
 mdns_init();uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char name[32];snprintf(name,sizeof(name),"grok-pet-%02x%02x%02x",mac[3],mac[4],mac[5]);mdns_hostname_set(name);
 return xTaskCreate(network_task,"pet_network",10240,NULL,4,&network_handle)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
bool pet_network_try(const pet_config_t *c,const char *id){
 if(!candidates||!pet_config_valid(c)||!c->network_count||strlen(id)>32)return false;
 candidate_t next={.config=*c};snprintf(next.id,sizeof(next.id),"%s",id);
 bool ok=xQueueSend(candidates,&next,0)==pdTRUE;pet_config_wipe(&next,sizeof(next));return ok;
}
bool pet_network_online(void){return atomic_load(&online);}
void pet_network_sleep(bool value){atomic_store(&asleep,value);}
bool pet_network_endpoint(const pet_config_t *c,char host[64],uint16_t *port){
 if(!c->bridge_paired)return false;
 char expected[33];pet_bridge_identity(c->key,expected);
 mdns_result_t *results=NULL;
 if(mdns_query_ptr("_grokpet","_tcp",2000,8,&results)==ESP_OK){
  for(mdns_result_t *r=results;r;r=r->next){
   bool matches=false,version=false;
   for(size_t i=0;i<r->txt_count;i++){
    if(pet_discovery_txt(r->txt[i].key,r->txt[i].value,"id",expected))matches=true;
    if(pet_discovery_txt(r->txt[i].key,r->txt[i].value,"v","2"))version=true;
   }
   if(matches&&version&&r->hostname&&strlen(r->hostname)<57&&r->port>=1024){
    snprintf(host,64,"%s.local",r->hostname);*port=r->port;mdns_query_results_free(results);return true;
   }
  }
  mdns_query_results_free(results);
 }
 snprintf(host,64,"%s",c->host);*port=c->port;return true;
}
