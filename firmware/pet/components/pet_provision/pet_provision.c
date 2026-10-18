#include "pet_provision.h"
#include "pet_setup_guard.h"
#include "cJSON.h"
#include "pet_json.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static pet_provision_callbacks_t cb;
static pet_setup_guard_t guard;
static SemaphoreHandle_t lock;
static atomic_bool synced,window;
static atomic_uint connection=BLE_HS_CONN_HANDLE_NONE;
static uint8_t address_type;
static atomic_ullong deadline;
/* Public service IDs; pairing is ephemeral and never touches Muse's bond namespace. */
static const ble_uuid128_t service=BLE_UUID128_INIT(0x10,0x58,0x8a,0x9a,0x86,0xe6,0x42,0x90,0x8c,0x41,0xd4,0x51,0x00,0x70,0x72,0x67);
static const ble_uuid128_t request=BLE_UUID128_INIT(0x10,0x58,0x8a,0x9a,0x86,0xe6,0x42,0x90,0x8c,0x41,0xd4,0x51,0x01,0x70,0x72,0x67);
static const ble_uuid128_t result=BLE_UUID128_INIT(0x10,0x58,0x8a,0x9a,0x86,0xe6,0x42,0x90,0x8c,0x41,0xd4,0x51,0x02,0x70,0x72,0x67);
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
static const char *string(cJSON *json,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool copy(char *out,size_t cap,const char *in){if(!in||strlen(in)>=cap)return false;memcpy(out,in,strlen(in)+1);return true;}
static bool hexkey(uint8_t key[32],const char *text){
 if(!text||strlen(text)!=64)return false;
 for(unsigned i=0;i<32;i++){unsigned byte;char end;if(sscanf(text+i*2,"%2x%c",&byte,&end)<1)return false;
  for(unsigned j=0;j<2;j++){char c=text[i*2+j];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;}key[i]=(uint8_t)byte;}
 return true;
}
static bool submit_locked(const char *text){
 cJSON *json=pet_json_parse(text,strlen(text));if(!json)return false;
 const char *id=string(json,"id"),*op=string(json,"op");
 bool ok=false;pet_config_t candidate=cb.current(cb.context);
 cJSON *wifi=cJSON_GetObjectItemCaseSensitive(json,"wifi"),*bridge=cJSON_GetObjectItemCaseSensitive(json,"bridge");
 if(!id||!op||strcmp(op,"configure")||(!wifi&&!bridge))goto done;
 if(!strcmp(id,guard.request)){ok=true;goto done;}
 if(wifi){
  pet_network_config_t network={0};const char *ssid=string(wifi,"ssid"),*password=string(wifi,"password");
  if(!copy(network.ssid,sizeof(network.ssid),ssid)||!copy(network.password,sizeof(network.password),password))goto done;
  network.hidden=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(wifi,"hidden"));
  pet_network_config_t saved[PET_NETWORK_MAX];memcpy(saved,candidate.networks,sizeof(saved));
  unsigned count=1;candidate.networks[0]=network;
  for(unsigned i=0;i<candidate.network_count&&count<PET_NETWORK_MAX;i++)if(strcmp(saved[i].ssid,network.ssid))candidate.networks[count++]=saved[i];
  candidate.network_count=(uint8_t)count;pet_config_wipe(saved,sizeof(saved));pet_config_wipe(&network,sizeof(network));
 }
 if(bridge){
  cJSON *port=cJSON_GetObjectItemCaseSensitive(bridge,"port");
  if(!copy(candidate.host,sizeof(candidate.host),string(bridge,"host"))||!cJSON_IsNumber(port)||port->valuedouble!=port->valueint||port->valueint<1024||port->valueint>65535||!hexkey(candidate.key,string(bridge,"key")))goto done;
  candidate.port=(uint16_t)port->valueint;candidate.bridge_paired=1;pet_bridge_identity(candidate.key,candidate.bridge_id);
 }
 if(!pet_config_valid(&candidate)||!candidate.network_count||!pet_setup_request(&guard,id))goto done;
 ok=pet_network_try(&candidate,id);if(!ok)pet_setup_result(&guard,id,false);
 if(ok)cb.status("正在验证网络，原设置暂时保留",0,cb.context);
 done:pet_config_wipe(&candidate,sizeof(candidate));
 /* cJSON allocations contain input secrets; wipe every string before freeing. */
 cJSON *stack[64];unsigned count=0;stack[count++]=json;
 while(count){cJSON *p=stack[--count];for(cJSON *child=p->child;child;child=child->next)if(count<64)stack[count++]=child;
  if(p->valuestring)pet_config_wipe(p->valuestring,strlen(p->valuestring));}
 cJSON_Delete(json);return ok;
}
int pet_provision_status(char *out,size_t cap){
 xSemaphoreTake(lock,portMAX_DELAY);
 int n=snprintf(out,cap,"{\"open\":%s,\"id\":\"%s\",\"state\":\"%s\"}",atomic_load(&window)?"true":"false",guard.request,
 guard.pending?"testing":guard.result>0?"saved":guard.result<0?"failed":"idle");
 xSemaphoreGive(lock);return n;
}
bool pet_provision_usb(const char *text){
 if(!lock||strlen(text)>=2047)return false;
 xSemaphoreTake(lock,portMAX_DELAY);
 bool ok=atomic_load(&window)&&now_ms()<guard.deadline&&submit_locked(text);
 xSemaphoreGive(lock);return ok;
}
void pet_provision_result(const char *id,bool ok){
 xSemaphoreTake(lock,portMAX_DELAY);pet_setup_result(&guard,id,ok);xSemaphoreGive(lock);
 cb.status(ok?"网络已保存，可连接小糖":"验证失败，已恢复原网络",0,cb.context);
 /* Keep a successful connection available briefly for the client to read its result. */
 if(ok){deadline=now_ms()+10000;}
}
static int access_cb(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg){
 (void)attr;(void)arg;struct ble_gap_conn_desc desc;
 if(!atomic_load(&window)||ble_gap_conn_find(conn,&desc)||!desc.sec_state.encrypted||!desc.sec_state.authenticated)return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
 if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR){char status[160];pet_provision_status(status,sizeof(status));return os_mbuf_append(ctx->om,status,strlen(status))?BLE_ATT_ERR_INSUFFICIENT_RES:0;}
 if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
 char bytes[256];uint16_t n=0;
 if(OS_MBUF_PKTLEN(ctx->om)>sizeof(bytes)||ble_hs_mbuf_to_flat(ctx->om,bytes,sizeof(bytes),&n))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
 xSemaphoreTake(lock,portMAX_DELAY);int state=pet_setup_chunk(&guard,now_ms(),true,bytes,n);
 bool ok=state==PET_SETUP_READY?submit_locked(guard.bytes):state==PET_SETUP_MORE;
 if(state!=PET_SETUP_MORE)pet_setup_reset(&guard);
 xSemaphoreGive(lock);pet_config_wipe(bytes,sizeof(bytes));
 return ok?0:BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[]={
 {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,.characteristics=(struct ble_gatt_chr_def[]){
 {.uuid=&request.u,.access_cb=access_cb,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
 {.uuid=&result.u,.access_cb=access_cb,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN},
 {0}}},{0}};
static void advertise(void);
static int gap(struct ble_gap_event *event,void *arg){
 (void)arg;
 if(event->type==BLE_GAP_EVENT_CONNECT){if(!event->connect.status){atomic_store(&connection,event->connect.conn_handle);ble_gap_security_initiate(event->connect.conn_handle);}else advertise();}
 else if(event->type==BLE_GAP_EVENT_DISCONNECT){atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);xSemaphoreTake(lock,portMAX_DELAY);pet_setup_reset(&guard);xSemaphoreGive(lock);advertise();}
 else if(event->type==BLE_GAP_EVENT_ADV_COMPLETE)advertise();
 else if(event->type==BLE_GAP_EVENT_PASSKEY_ACTION){
  if(!atomic_load(&window))return BLE_HS_EAPP;
  if(event->passkey.params.action!=BLE_SM_IOACT_DISP)return BLE_HS_EAPP;
  struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=100000+esp_random()%900000};
  cb.status("在电脑的蓝牙配对提示中输入此号码",io.passkey,cb.context);return ble_sm_inject_io(event->passkey.conn_handle,&io);
 }
 return 0;
}
static void advertise(void){
 if(!atomic_load(&window)||!atomic_load(&synced)||ble_gap_adv_active()||atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE)return;
 struct ble_hs_adv_fields fields={0};fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 fields.uuids128=(ble_uuid128_t *)&service;fields.num_uuids128=1;fields.uuids128_is_complete=1;
 ble_gap_adv_set_fields(&fields);
 const char *name=ble_svc_gap_device_name();struct ble_hs_adv_fields scan={.name=(uint8_t *)name,.name_len=strlen(name),.name_is_complete=1};ble_gap_adv_rsp_set_fields(&scan);
 struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
 ble_gap_adv_start(address_type,NULL,1000,&p,gap,NULL);
}
static void sync_cb(void){if(!ble_hs_id_infer_auto(0,&address_type))atomic_store(&synced,true);}
static void host_task(void *unused){(void)unused;nimble_port_run();nimble_port_freertos_deinit();}
void pet_provision_open(bool open){
 if(!lock)return;
 xSemaphoreTake(lock,portMAX_DELAY);deadline=open?now_ms()+300000:0;pet_setup_open(&guard,now_ms(),open?300000:0);atomic_store(&window,open);xSemaphoreGive(lock);
 if(!open){ble_gap_adv_stop();unsigned conn=atomic_load(&connection);if(conn!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);}
 cb.window(open,cb.context);cb.status(open?"用 Chrome 打开 Mac 配网页面，选择此设备":"配网窗口已关闭",0,cb.context);
}
static void timer_task(void *unused){(void)unused;for(;;){if(atomic_load(&window)){if(now_ms()>=deadline)pet_provision_open(false);else advertise();}vTaskDelay(pdMS_TO_TICKS(250));}}
esp_err_t pet_provision_start(pet_provision_callbacks_t callbacks){
 cb=callbacks;lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
 if(nimble_port_init()!=ESP_OK)return ESP_FAIL;
 ble_hs_cfg.sync_cb=sync_cb;ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
 ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Grok Pet 1.43C");
 if(ble_gatts_count_cfg(services)||ble_gatts_add_svcs(services))return ESP_FAIL;
 nimble_port_freertos_init(host_task);
 return xTaskCreate(timer_task,"pet_setup",4096,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
