#include "pet_reader_wire.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool number(cJSON *j,const char *key,uint32_t *out){
 cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
 if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>UINT32_MAX||v->valuedouble!=(uint32_t)v->valuedouble)return false;
 *out=(uint32_t)v->valuedouble;return true;
}
static bool boolean(cJSON *j,const char *key,bool *out){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);if(!cJSON_IsBool(v))return false;*out=cJSON_IsTrue(v);return true;}
bool pet_reader_parse(cJSON *j,pet_reader_metadata_t *out){
 memset(out,0,sizeof(*out));pet_reader_page_t *p=&out->page;
 cJSON *op=cJSON_GetObjectItemCaseSensitive(j,"op");
 if(!cJSON_IsString(op)||!number(j,"generation",&p->generation)||!p->generation||!number(j,"turn",&p->turn))return false;
 if(!strcmp(op->valuestring,"caption_catalog")){
  out->catalog=true;return boolean(j,"available",&out->available)&&boolean(j,"responding",&p->responding)&&boolean(j,"truncated",&p->truncated);
 }
 if(strcmp(op->valuestring,"caption_page"))return false;
 uint32_t page,pages;
 if(!number(j,"request",&p->request)||!p->request||!number(j,"version",&p->version)||!number(j,"page",&page)||page>UINT16_MAX)return false;
 p->page=(uint16_t)page;cJSON *error=cJSON_GetObjectItemCaseSensitive(j,"error");
 if(error){p->error=true;return cJSON_IsString(error)&&(!strcmp(error->valuestring,"unavailable")||!strcmp(error->valuestring,"stale")||!strcmp(error->valuestring,"busy"));}
 cJSON *text=cJSON_GetObjectItemCaseSensitive(j,"text");
 if(!p->version||!number(j,"pages",&pages)||!pages||pages>UINT16_MAX||page>=pages||!cJSON_IsString(text)||strlen(text->valuestring)>300||!boolean(j,"responding",&p->responding)||!boolean(j,"truncated",&p->truncated))return false;
 for(const unsigned char *c=(const unsigned char *)text->valuestring;*c;c++)if((*c<32&&*c!='\n'&&*c!='\t')||*c==127)return false;
 p->pages=(uint16_t)pages;memcpy(p->text,text->valuestring,strlen(text->valuestring)+1);return true;
}
size_t pet_reader_encode(const pet_reader_request_t *r,char *out,size_t capacity){
 if(!r->generation||!r->request||(!r->version&&r->page))return 0;
 int n=snprintf(out,capacity,"{\"op\":\"caption_read\",\"generation\":%u,\"turn\":%u,\"request\":%u,\"version\":%u,\"page\":%u}",(unsigned)r->generation,(unsigned)r->turn,(unsigned)r->request,(unsigned)r->version,r->page);
 return n>0&&(size_t)n<capacity?(size_t)n:0;
}
