#pragma once
#include "pet_core.h"
#include "pet_config.h"
#include "esp_err.h"
#include <stddef.h>
typedef struct {
 pet_config_t (*config)(void *context);
 pet_state_t (*state)(void *context);
 bool (*event)(pet_event_t event,void *context);
 void (*caption)(uint32_t generation,uint32_t turn,const char *text,void *context);
 void (*status)(bool mac_connected,void *context);
 void *context;
} pet_link_callbacks_t;
esp_err_t pet_link_start(pet_link_callbacks_t callbacks);
bool pet_link_control(const char *action,uint32_t generation,uint32_t turn,void *context);
bool pet_link_capture(uint32_t generation,uint32_t turn,const void *pcm,size_t bytes,void *context);
bool pet_link_credit(uint32_t generation,uint32_t turn,void *context);
void pet_link_reconnect(void *context);
void pet_link_sleep(bool asleep,void *context);

unsigned pet_link_flags(void);
