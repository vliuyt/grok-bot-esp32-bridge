#include "pet_link_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int frames;static uint32_t last_turn;static char payload[1025];
static bool accept(uint8_t type,uint32_t turn,const void *bytes,size_t n,void *ctx){(void)ctx;assert(type==2);frames++;last_turn=turn;memcpy(payload,bytes,n);payload[n]=0;return true;}
int main(void){assert(pet_link_retry_ms(0)==500&&pet_link_retry_ms(3)==4000&&pet_link_retry_ms(99)==30000);pet_link_rx_t rx={0};uint8_t bytes[1200];const char *json="{\"op\":\"caption\",\"text\":\"小糖你好\"}";size_t n=pet_link_encode(bytes,sizeof(bytes),2,17,json,strlen(json));assert(n>0);
 for(size_t i=0;i<n;i++)assert(pet_link_feed(&rx,bytes+i,1,accept,0));assert(frames==1&&last_turn==17&&!strcmp(payload,json));
 bytes[0]=0xff;assert(!pet_link_feed(&rx,bytes,4,accept,0));memset(&rx,0,sizeof(rx));
 assert(!pet_link_encode(bytes,sizeof(bytes),3,1,"x",1));
 assert(pet_link_should_send(3,2,7,2,7));assert(!pet_link_should_send(3,2,6,2,7));assert(!pet_link_should_send(2,1,7,2,7));
 assert(pet_link_should_send(2,2,7,2,7));assert(pet_link_should_send(2,2,6,2,7));puts("PET_LINK_OK");}
