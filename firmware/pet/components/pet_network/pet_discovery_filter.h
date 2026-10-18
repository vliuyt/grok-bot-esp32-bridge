#pragma once
#include <stdbool.h>
#include <string.h>
static inline bool pet_discovery_txt(const char *key,const char *value,const char *wanted_key,const char *wanted_value){
 return key&&value&&wanted_key&&wanted_value&&!strcmp(key,wanted_key)&&!strcmp(value,wanted_value);
}
