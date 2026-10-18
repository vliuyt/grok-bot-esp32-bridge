#include "pet_json.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){const char *text="{\"text\":\"小糖\\\"你好\"}";cJSON *j=pet_json_parse(text,strlen(text));assert(j);cJSON_Delete(j);
 assert(!pet_json_parse("{}garbage",9));assert(!pet_json_parse("{}\0{}",5));assert(!pet_json_parse("{\"x\":\"\xc0\x80\"}",10));
 const char *deep="[[[[[[[[[0]]]]]]]]]";assert(!pet_json_parse(deep,strlen(deep)));puts("PET_JSON_OK");}
