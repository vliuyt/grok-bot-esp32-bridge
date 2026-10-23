#include "pet_link_wire.h"
#include "pet_press_wire.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int frames;static uint32_t last_turn;static char payload[1025];
static bool accept(uint8_t type,uint32_t turn,const void *bytes,size_t n,void *ctx){(void)ctx;assert(type==2);frames++;last_turn=turn;memcpy(payload,bytes,n);payload[n]=0;return true;}
int main(void){assert(pet_link_retry_ms(0)==500&&pet_link_retry_ms(3)==4000&&pet_link_retry_ms(99)==30000);pet_link_rx_t rx={0};uint8_t bytes[1200];const char *json="{\"op\":\"caption\",\"text\":\"小糖你好\"}";size_t n=pet_link_encode(bytes,sizeof(bytes),2,17,json,strlen(json));assert(n>0);
 for(size_t i=0;i<n;i++)assert(pet_link_feed(&rx,bytes+i,1,accept,0));assert(frames==1&&last_turn==17&&!strcmp(payload,json));
 bytes[0]=0xff;assert(!pet_link_feed(&rx,bytes,4,accept,0));memset(&rx,0,sizeof(rx));
 assert(!pet_link_encode(bytes,sizeof(bytes),3,1,"x",1));
 assert(pet_link_should_send(3,2,7,2,7));assert(!pet_link_should_send(3,2,6,2,7));assert(!pet_link_should_send(2,1,7,2,7));
 assert(pet_link_should_send(2,2,7,2,7));assert(pet_link_should_send(2,2,6,2,7));
 char control[128];
 pet_state_t state={.generation=2,.turn=17,.dialogue=PET_DIALOGUE_LISTEN,.turn_warmth=60};
 n=pet_press_encode(false,2,17,NULL,control,sizeof(control));
 assert(n==5&&!strcmp(control,"press"));
 n=pet_press_encode(true,2,17,&state,control,sizeof(control));
 assert(n==strlen(control)&&!strcmp(control,"{\"op\":\"press\",\"generation\":2,\"turn\":17,\"mood\":\"warm\"}"));
 memset(&rx,0,sizeof(rx));size_t framed=pet_link_encode(bytes,sizeof(bytes),2,17,control,n);
 assert(framed&&pet_link_feed(&rx,bytes,framed,accept,NULL));
 assert(last_turn==17&&!strcmp(payload,control));
 state.turn_warmth=0;n=pet_press_encode(true,2,17,&state,control,sizeof(control));
 assert(n&&!strcmp(control,"{\"op\":\"press\",\"generation\":2,\"turn\":17,\"mood\":\"neutral\"}"));
 state.turn_warmth=0.01f;assert(pet_press_encode(true,2,17,&state,control,sizeof(control))&&strstr(control,"\"warm\""));
 state.turn_warmth=100;assert(pet_press_encode(true,2,17,&state,control,sizeof(control))&&strstr(control,"\"warm\""));
 assert(!pet_press_encode(true,3,17,&state,control,sizeof(control)));
 assert(!pet_press_encode(true,2,18,&state,control,sizeof(control)));
 state.dialogue=PET_DIALOGUE_WAIT;assert(!pet_press_encode(true,2,17,&state,control,sizeof(control)));
 state.dialogue=PET_DIALOGUE_LISTEN;state.turn_warmth=-1;assert(!pet_press_encode(true,2,17,&state,control,sizeof(control)));
 state.turn_warmth=101;assert(!pet_press_encode(true,2,17,&state,control,sizeof(control)));
 state.turn_warmth=NAN;assert(!pet_press_encode(true,2,17,&state,control,sizeof(control)));
 state.turn_warmth=INFINITY;assert(!pet_press_encode(true,2,17,&state,control,sizeof(control)));
 state.turn_warmth=0;assert(!pet_press_encode(true,2,17,NULL,control,sizeof(control)));
 assert(!pet_press_encode(true,2,17,&state,control,16));
 assert(!pet_press_encode(false,2,17,NULL,control,5));
 assert(!pet_press_encode(false,2,17,NULL,NULL,0));
 state.generation=UINT32_MAX;state.turn=UINT32_MAX;
 n=pet_press_encode(true,UINT32_MAX,UINT32_MAX,&state,control,sizeof(control));
 assert(n&&!strcmp(control,"{\"op\":\"press\",\"generation\":4294967295,\"turn\":4294967295,\"mood\":\"neutral\"}"));
 puts("PET_LINK_OK");}
