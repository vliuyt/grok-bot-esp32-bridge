#pragma once
#include "pet_core.h"
#include "pet_config.h"
#include <stddef.h>
typedef struct {
    bool (*control)(const char *action,uint32_t generation,uint32_t turn,void *context);
    bool (*capture)(uint32_t generation,uint32_t turn,const void *pcm,size_t bytes,void *context);
    bool (*credit)(uint32_t generation,uint32_t turn,void *context);
    void (*reconnect)(void *context);
    void (*sleep)(bool asleep,void *context);
    void *context;
} pet_transport_t;
void pet_app_bind_transport(pet_transport_t transport);
bool pet_app_post(pet_event_t event);
pet_state_t pet_app_state(void);
pet_config_t pet_app_config(void);
void pet_app_caption(uint32_t generation,uint32_t turn,const char *text);
void pet_app_network(bool connected,const char *detail);
void pet_app_setup_status(const char *detail,int passkey);
int pet_app_status(char *out,size_t capacity,bool compact);
void pet_console_start(void);
unsigned pet_console_stack_free(void);
