#pragma once
#include "pet_config.h"
#include "esp_err.h"
typedef struct {
 pet_config_t (*current)(void *ctx);
 bool (*commit)(const pet_config_t *candidate,void *ctx);
 void (*status)(bool online,const char *text,void *ctx);
 void (*result)(const char *id,bool ok,void *ctx);
 void *context;
} pet_network_callbacks_t;
esp_err_t pet_network_start(pet_network_callbacks_t callbacks);
bool pet_network_try(const pet_config_t *candidate,const char *request);
bool pet_network_online(void);
unsigned pet_network_stack_free(void);
void pet_network_sleep(bool asleep);
bool pet_network_endpoint(const pet_config_t *config,char host[64],uint16_t *port);
void pet_bridge_identity(const uint8_t key[32],char id[33]);
