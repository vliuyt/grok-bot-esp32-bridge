#include "pet_link_wire.h"
#include <string.h>
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put32(uint8_t *p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static bool valid(uint8_t type,size_t n){return (type==2&&n<=1024)||(type==3&&n>0&&n<=640&&n%2==0);}
size_t pet_link_encode(void *out,size_t cap,uint8_t type,uint32_t turn,const void *bytes,size_t n){
 if(cap<n+9||!valid(type,n)||(!bytes&&n))return 0;
 uint8_t *p=out;put32(p,(uint32_t)n+5);p[4]=type;put32(p+5,turn);if(n)memcpy(p+9,bytes,n);return n+9;
}
bool pet_link_feed(pet_link_rx_t *rx,const void *data,size_t n,pet_link_frame_cb cb,void *ctx){
 const uint8_t *bytes=data;
 while(n){
  size_t copy=sizeof(rx->bytes)-rx->used;if(copy>n)copy=n;if(!copy)return false;
  memcpy(rx->bytes+rx->used,bytes,copy);rx->used+=copy;bytes+=copy;n-=copy;
  while(rx->used>=4){uint32_t length=read32(rx->bytes);if(length<5||length>PET_LINK_FRAME_MAX)return false;
   if(rx->used<length+4){break;}
   uint8_t type=rx->bytes[4];
   if(!valid(type,length-5)||!cb(type,read32(rx->bytes+5),rx->bytes+9,length-5,ctx))return false;
   size_t remain=rx->used-length-4;memmove(rx->bytes,rx->bytes+length+4,remain);rx->used=remain;
  }
 }
 return true;
}
bool pet_link_should_send(uint8_t type,uint32_t pg,uint32_t pt,uint32_t g,uint32_t t){return pg==g&&(type==2||(type==3&&pt==t));}

unsigned pet_link_retry_ms(unsigned failures){return failures>=6?30000:500u<<failures;}
