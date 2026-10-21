#include "pet_reader_wire.h"
#include "pet_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool parse(const char *text,pet_reader_metadata_t *out){cJSON *j=pet_json_parse(text,strlen(text));if(!j)return false;bool ok=pet_reader_parse(j,out);cJSON_Delete(j);return ok;}
int main(void){
 pet_reader_metadata_t m;
 assert(parse("{\"op\":\"caption_catalog\",\"generation\":7,\"turn\":3,\"available\":true,\"responding\":false,\"truncated\":false}",&m)&&m.catalog&&m.available);
 assert(parse("{\"op\":\"caption_page\",\"generation\":7,\"turn\":3,\"request\":1,\"version\":4,\"page\":0,\"pages\":2,\"responding\":true,\"truncated\":false,\"text\":\"中文\\n\\t🙂\"}",&m));assert(!strcmp(m.page.text,"中文\n\t🙂"));
 assert(!parse("{\"op\":\"caption_page\",\"generation\":7,\"turn\":3,\"request\":1,\"version\":4,\"page\":0,\"pages\":2,\"responding\":true,\"truncated\":false,\"text\":\"bad\\u0001\"}",&m));
 assert(!parse("{\"op\":\"caption_page\",\"generation\":7,\"turn\":3,\"request\":1,\"version\":4,\"page\":65536,\"error\":\"stale\"}",&m));
 assert(parse("{\"op\":\"caption_page\",\"generation\":7,\"turn\":3,\"request\":1,\"version\":0,\"page\":0,\"error\":\"busy\"}",&m)&&m.page.error);
 char encoded[192];pet_reader_request_t r={7,3,0,1,0};assert(pet_reader_encode(&r,encoded,sizeof(encoded))>0);assert(strstr(encoded,"\"op\":\"caption_read\""));assert(!pet_reader_encode(&r,encoded,5));
 puts("PET_READER_WIRE_OK");
}
