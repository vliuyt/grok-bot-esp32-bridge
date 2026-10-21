#include "pet_link.h"
#include "pet_link_wire.h"
#include "pet_network.h"
#include "pet_audio.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "pet_json.h"
#include "mdns.h"
#include "lwip/inet.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static pet_link_callbacks_t cb;
static QueueHandle_t outgoing;
static StaticQueue_t queue_control;
static esp_tls_t *tls;
static atomic_uint generation,current_turn;
static atomic_bool suspended,reset_requested,connected;
static atomic_bool hello,native_ready;
static bool turn_status_capable,turn_progress_capable;
static pet_link_rx_t receiver;
static char bridge_id[33];
static uint64_t last_received,last_ping,connected_at;
typedef struct {uint32_t generation,turn;uint16_t bytes;uint8_t type;uint8_t data[640];} packet_t;
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
static bool post(pet_event_kind_t kind,int value){return cb.event((pet_event_t){.kind=kind,.now_ms=now_ms(),.generation=atomic_load(&generation),.turn=atomic_load(&current_turn),.value=value},cb.context);}
static bool write_all(const void *bytes,size_t n){
 const uint8_t *p=bytes;uint64_t deadline=now_ms()+500;
 while(n){ssize_t sent=esp_tls_conn_write(tls,p,n);if(sent>0){p+=sent;n-=(size_t)sent;continue;}
  if((sent!=ESP_TLS_ERR_SSL_WANT_READ&&sent!=ESP_TLS_ERR_SSL_WANT_WRITE)||now_ms()>deadline)return false;
  vTaskDelay(pet_delay_ticks(1,configTICK_RATE_HZ));}
 return true;
}
static bool send(uint8_t type,uint32_t turn,const void *data,size_t n){uint8_t bytes[1033];size_t length=pet_link_encode(bytes,sizeof(bytes),type,turn,data,n);return length&&write_all(bytes,length);}
static bool enqueue(uint8_t type,uint32_t g,uint32_t t,const void *data,size_t n){
 if(!outgoing||!atomic_load(&connected)||g!=atomic_load(&generation)||n>640)return false;
 packet_t p={.generation=g,.turn=t,.bytes=(uint16_t)n,.type=type};memcpy(p.data,data,n);
 bool ok=xQueueSend(outgoing,&p,0)==pdTRUE;
 if(!ok){atomic_store(&reset_requested,true);}
 return ok;
}
bool pet_link_control(const char *action,uint32_t g,uint32_t t,void *ctx){
 (void)ctx;if(strcmp(action,"press")&&strcmp(action,"release")&&strcmp(action,"cancel"))return false;
 if(g!=atomic_load(&generation))return false;
 if(!strcmp(action,"press"))atomic_store(&current_turn,t);
 return enqueue(2,g,t,action,strlen(action));
}
bool pet_link_capture(uint32_t g,uint32_t t,const void *pcm,size_t n,void *ctx){(void)ctx;return n>0&&n<=640&&n%2==0&&enqueue(3,g,t,pcm,n);}
bool pet_link_credit(uint32_t g,uint32_t t,void *ctx){(void)ctx;if(g!=atomic_load(&generation)||t!=atomic_load(&current_turn))return true;return enqueue(2,g,t,"credit",6);}
void pet_link_reconnect(void *ctx){(void)ctx;atomic_store(&suspended,false);if(!atomic_load(&connected))atomic_store(&reset_requested,true);}
void pet_link_sleep(bool asleep,void *ctx){(void)ctx;atomic_store(&suspended,asleep);}
static const char *string(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool number(cJSON *j,const char *name,uint32_t *out){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,name);if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>4294967295.0||v->valuedouble!=(uint32_t)v->valuedouble)return false;*out=(uint32_t)v->valuedouble;return true;}
static bool metadata(const void *bytes,size_t n){
 char text[1025];if(n>1024)return false;memcpy(text,bytes,n);text[n]=0;
 cJSON *j=pet_json_parse(text,n);if(!j)return false;
 bool ok=true;uint32_t g;const char *op=string(j,"op");
 if(!op||!number(j,"generation",&g)){ok=false;goto done;}
 if(g!=atomic_load(&generation))goto done;
 if(!strcmp(op,"hello")){
  const char *id=string(j,"bridgeId");uint32_t version;
  if(hello||!id||strcmp(id,bridge_id)||!number(j,"v",&version)||version!=2){ok=false;goto done;}
  cJSON *caps=cJSON_GetObjectItemCaseSensitive(j,"capabilities");
  if(caps){
   if(!cJSON_IsArray(caps)||cJSON_GetArraySize(caps)>8){ok=false;goto done;}
   cJSON *cap; cJSON_ArrayForEach(cap,caps){if(!cJSON_IsString(cap)||strlen(cap->valuestring)>32){ok=false;goto done;}if(!strcmp(cap->valuestring,"turn_status"))turn_status_capable=true;if(!strcmp(cap->valuestring,"turn_progress"))turn_progress_capable=true;}
  }
  hello=true;if(native_ready)post(PET_EV_LINK,PET_CONN_READY);
 }else if(!hello){ok=false;}
 else if(!strcmp(op,"link")){
  const char *state=string(j,"state");if(!state){ok=false;goto done;}
  native_ready=!strcmp(state,"ready");post(PET_EV_LINK,native_ready?PET_CONN_READY:!strcmp(state,"connecting")?PET_CONN_CONNECTING:PET_CONN_UNAVAILABLE);
 }else if(!strcmp(op,"task_channel")){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"available");if(!cJSON_IsBool(v)){ok=false;goto done;}post(PET_EV_TASK_CHANNEL,cJSON_IsTrue(v));
 }else if(!strcmp(op,"caption")){
  uint32_t turn;const char *caption=string(j,"text");
  if(!caption||strlen(caption)>320||!number(j,"turn",&turn)){ok=false;goto done;}
  if(turn==atomic_load(&current_turn))cb.caption(g,turn,caption,cb.context);
 }else if(!strcmp(op,"turn_status")){
  uint32_t turn;pet_notice_t notice=pet_notice_from_code(string(j,"code"));
  if(!turn_status_capable||notice==PET_NOTICE_NONE||!number(j,"turn",&turn)){ok=false;goto done;}
  if(notice==PET_NOTICE_PROGRESS&&!turn_progress_capable){ok=false;goto done;}
  if(turn==atomic_load(&current_turn))cb.event((pet_event_t){.kind=PET_EV_TURN_STATUS,.now_ms=now_ms(),.generation=g,.turn=turn,.value=notice},cb.context);
 }else if(!strcmp(op,"clock")){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"epochMs");
  if(!cJSON_IsNumber(v)||v->valuedouble<1700000000000.0||v->valuedouble>4102444800000.0){ok=false;goto done;}
  time_t seconds=(time_t)(v->valuedouble/1000)+8*3600;struct tm shanghai;gmtime_r(&seconds,&shanghai);
  cb.event((pet_event_t){.kind=PET_EV_CLOCK,.now_ms=now_ms(),.day=(uint32_t)(shanghai.tm_year+1900)*10000+(uint32_t)(shanghai.tm_mon+1)*100+(uint32_t)shanghai.tm_mday,.value=shanghai.tm_hour*60+shanghai.tm_min},cb.context);
 }else ok=false;
 done:cJSON_Delete(j);return ok;
}
static bool receive(uint8_t type,uint32_t turn,const void *bytes,size_t n,void *ctx){
 (void)ctx;uint32_t g=atomic_load(&generation),current=atomic_load(&current_turn);
 if(type==3){
  if(turn!=current||!hello)return true;
  uint32_t receive_epoch=pet_audio_receive_epoch();
  pet_state_t state=cb.state(cb.context);
  if(!pet_response_audio_allowed(&state,g,turn)){return enqueue(2,g,turn,"credit",6);}
  int result=pet_audio_push(g,turn,receive_epoch,bytes,n);
  if(result==0)return enqueue(2,g,turn,"credit",6);
  return result>0;
 }
 if(n&&((const char *)bytes)[0]=='{')return metadata(bytes,n);
 if(n==4&&!memcmp(bytes,"pong",4))return true;
 if(n==5&&!memcmp(bytes,"ready",5)){native_ready=true;if(hello)post(PET_EV_LINK,PET_CONN_READY);return true;}
 if(turn!=current)return true;
 if(n==5&&!memcmp(bytes,"clear",5)){pet_audio_flush_playback(g,turn);return true;}
 if(n==4&&!memcmp(bytes,"done",4)){pet_audio_done(g,turn);return true;}
 return n==4&&!memcmp(bytes,"sent",4);
}
static bool private_address(const char *host){unsigned a,b,c,d;char tail;
 return sscanf(host,"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)==4&&a<256&&b<256&&c<256&&d<256&&(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168));}
static bool connect_bridge(void){
 pet_config_t c=cb.config(cb.context);char host[64];uint16_t port;bool ok=false;
 if(!pet_network_endpoint(&c,host,&port))goto done;
 size_t len=strlen(host);if(len>6&&!strcmp(host+len-6,".local")){
  host[len-6]=0;esp_ip4_addr_t addr;
  if(mdns_query_a(host,2000,&addr)!=ESP_OK)goto done;
  snprintf(host,sizeof(host),IPSTR,IP2STR(&addr));
 }
 if(!private_address(host))goto done;
 pet_bridge_identity(c.key,bridge_id);
 psk_hint_key_t psk={.key=c.key,.key_size=32,.hint="grok-esp32"};esp_tls_cfg_t cfg={.timeout_ms=5000,.psk_hint_key=&psk};
 tls=esp_tls_init();if(!tls)goto done;
 int fd;if(esp_tls_conn_new_sync(host,strlen(host),port,&cfg,tls)!=1||esp_tls_get_conn_sockfd(tls,&fd)!=ESP_OK||fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0)goto done;
 char greeting[160];snprintf(greeting,sizeof(greeting),"{\"op\":\"hello\",\"v\":2,\"generation\":%u,\"capabilities\":[\"turn_status\",\"turn_progress\"]}",(unsigned)atomic_load(&generation));
 ok=send(2,0,greeting,strlen(greeting));
 done:pet_config_wipe(&c,sizeof(c));return ok;
}
static void disconnect_bridge(void){
 atomic_store(&connected,false);if(tls){esp_tls_conn_destroy(tls);tls=NULL;}
 post(PET_EV_LINK,PET_CONN_OFFLINE);cb.status(false,cb.context);
 packet_t p;while(xQueueReceive(outgoing,&p,0)==pdTRUE)pet_config_wipe(&p,sizeof(p));
 memset(&receiver,0,sizeof(receiver));hello=native_ready=false;turn_status_capable=turn_progress_capable=false;
}
static void link_task(void *unused){
 (void)unused;unsigned failures=0;
 for(;;){
  if(atomic_load(&suspended)||!pet_network_online()||cb.state(cb.context).setup){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  atomic_fetch_add(&generation,1);atomic_store(&current_turn,0);atomic_store(&reset_requested,false);post(PET_EV_LINK,PET_CONN_CONNECTING);
  if(!connect_bridge()){disconnect_bridge();if(failures<6)failures++;}
  else{
   atomic_store(&connected,true);cb.status(true,cb.context);last_received=last_ping=connected_at=now_ms();
   bool ok=true;
   while(ok&&!atomic_load(&reset_requested)&&!atomic_load(&suspended)&&pet_network_online()&&!cb.state(cb.context).setup){
    uint8_t input[1024];ssize_t n=esp_tls_conn_read(tls,input,sizeof(input));
    if(n>0){last_received=now_ms();ok=pet_link_feed(&receiver,input,(size_t)n,receive,NULL);}
    else if(n!=ESP_TLS_ERR_SSL_WANT_READ&&n!=ESP_TLS_ERR_SSL_WANT_WRITE)break;
    packet_t packet;
    for(unsigned i=0;i<12&&ok&&xQueueReceive(outgoing,&packet,0)==pdTRUE;i++){
     if(pet_link_should_send(packet.type,packet.generation,packet.turn,atomic_load(&generation),atomic_load(&current_turn)))ok=send(packet.type,packet.turn,packet.data,packet.bytes);
     pet_config_wipe(&packet,sizeof(packet));
    }
    uint64_t now=now_ms();if(now-last_ping>=5000){ok=ok&&send(2,atomic_load(&current_turn),"ping",4);last_ping=now;}
    if(now-last_received>20000||(!native_ready&&now-connected_at>45000))break;
    vTaskDelay(pet_delay_ticks(2,configTICK_RATE_HZ));
   }
   bool was_ready=hello&&native_ready;disconnect_bridge();
   if(was_ready)failures=0;else if(failures<6)failures++;
  }
  unsigned ticks=pet_link_retry_ms(failures)/100;
  for(unsigned i=0;i<ticks&&!atomic_load(&reset_requested);i++)vTaskDelay(pdMS_TO_TICKS(100));
 }
}
esp_err_t pet_link_start(pet_link_callbacks_t callbacks){
 cb=callbacks;uint8_t *buffer=heap_caps_calloc(64,sizeof(packet_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!buffer)return ESP_ERR_NO_MEM;
 outgoing=xQueueCreateStatic(64,sizeof(packet_t),buffer,&queue_control);if(!outgoing)return ESP_ERR_NO_MEM;
 return xTaskCreate(link_task,"pet_link",12288,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}

unsigned pet_link_flags(void){return (atomic_load(&connected)?1u:0)|(atomic_load(&hello)?2u:0)|(atomic_load(&native_ready)?4u:0);}
