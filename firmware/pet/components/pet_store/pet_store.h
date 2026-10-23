#pragma once
#include "pet_config.h"
#include "pet_caption.h"
bool pet_store_load(pet_config_t *out);
bool pet_store_save(const pet_config_t *config);

bool pet_store_caption_mode_load(pet_caption_mode_t *out);
bool pet_store_caption_mode_save(pet_caption_mode_t mode);
