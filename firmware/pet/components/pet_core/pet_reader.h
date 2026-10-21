#pragma once
#include "pet_caption.h"

typedef enum {PET_READER_OPEN,PET_READER_PREV,PET_READER_NEXT,PET_READER_RETRY} pet_reader_intent_t;
typedef struct {uint32_t generation,turn,version,request;uint16_t page;} pet_reader_request_t;
typedef struct {
 uint32_t generation,turn,version,request;uint16_t page,pages;
 bool responding,truncated,error;
 char text[321];
} pet_reader_page_t;
typedef struct {
 uint32_t generation,turn,version,request,sequence;uint16_t page,pages,target;
 uint64_t sent_ms,last_request_ms;
 bool enabled,capable,available,active,pending,failed,responding,truncated,requested;
 char text[321];
} pet_reader_t;
void pet_reader_sync(pet_reader_t *reader,const pet_state_t *state,pet_caption_mode_t mode,uint64_t now);
void pet_reader_catalog(pet_reader_t *reader,uint32_t generation,uint32_t turn,bool available,bool responding,bool truncated);
bool pet_reader_begin(pet_reader_t *reader,pet_reader_intent_t intent,uint64_t now,pet_reader_request_t *request);
bool pet_reader_receive(pet_reader_t *reader,const pet_reader_page_t *page);
void pet_reader_close(pet_reader_t *reader);
