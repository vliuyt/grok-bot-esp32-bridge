#pragma once
#include "pet_config.h"
typedef struct {
 bool (*connect)(const pet_network_config_t *candidate,void *ctx);
 bool (*commit)(const pet_config_t *candidate,void *ctx);
 void (*restore)(void *ctx);
 void *context;
} pet_transaction_ops_t;
bool pet_network_transaction(const pet_config_t *candidate,pet_transaction_ops_t ops);
