#pragma once
#include "pet_network.h"
typedef struct {
 pet_config_t (*current)(void *ctx);
 void (*status)(const char *text,int passkey,void *ctx);
 void (*window)(bool open,void *ctx);
 void *context;
} pet_provision_callbacks_t;
esp_err_t pet_provision_start(pet_provision_callbacks_t callbacks);
void pet_provision_open(bool open);
/* USB writes require the same physical setup window as BLE. No raw credentials are echoed. */
bool pet_provision_usb(const char *json);
void pet_provision_result(const char *id,bool ok);
int pet_provision_status(char *out,size_t capacity);
