#include "pet_json.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
cJSON *pet_json_parse(const char *text,size_t length){
 if(!text||!length||length>2047)return NULL;
 unsigned depth=0;bool quoted=false,escape=false;
 for(size_t i=0;i<length;i++){
  uint8_t c=(uint8_t)text[i];if(!c)return NULL;
  if(c>=128){unsigned more;uint32_t code,min;
   if(c>=0xc2&&c<=0xdf){more=1;code=c&31;min=0x80;}
   else if(c>=0xe0&&c<=0xef){more=2;code=c&15;min=0x800;}
   else if(c>=0xf0&&c<=0xf4){more=3;code=c&7;min=0x10000;}else return NULL;
   if(i+more>=length)return NULL;
   while(more--){uint8_t next=(uint8_t)text[++i];if((next&0xc0)!=0x80)return NULL;code=(code<<6)|(next&63);}
   if(code<min||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return NULL;
   continue;
  }
  if(quoted){if(escape){escape=false;}else if(c=='\\'){escape=true;}else if(c=='"'){quoted=false;}continue;}
  if(c=='"'){quoted=true;}else if(c=='{'||c=='['){if(++depth>8)return NULL;}else if(c=='}'||c==']'){if(!depth)return NULL;depth--;}
 }
 if(depth||quoted)return NULL;
 char copy[2048];memcpy(copy,text,length);copy[length]=0;
 cJSON *json=cJSON_ParseWithLengthOpts(copy,length+1,NULL,true);
 volatile char *p=copy;for(size_t i=0;i<=length;i++)p[i]=0;
 return json;
}
