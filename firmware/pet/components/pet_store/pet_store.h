#pragma once
#include "pet_config.h"
bool pet_store_load(pet_config_t *out);
bool pet_store_save(const pet_config_t *config);
