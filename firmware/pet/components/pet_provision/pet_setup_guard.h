#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum {PET_SETUP_DENIED=-2,PET_SETUP_INVALID=-1,PET_SETUP_MORE=0,PET_SETUP_READY=1};
typedef struct {uint64_t deadline,started;size_t length;char bytes[2048],request[33];bool pending;int result;} pet_setup_guard_t;
void pet_setup_open(pet_setup_guard_t *s,uint64_t now,uint64_t duration);
void pet_setup_reset(pet_setup_guard_t *s);
int pet_setup_chunk(pet_setup_guard_t *s,uint64_t now,bool authenticated,const void *bytes,size_t n);
bool pet_setup_request(pet_setup_guard_t *s,const char *id);
void pet_setup_result(pet_setup_guard_t *s,const char *id,bool ok);
