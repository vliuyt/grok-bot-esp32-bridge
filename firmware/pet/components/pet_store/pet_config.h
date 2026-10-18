#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PET_CONFIG_VERSION 1u
#define PET_NETWORK_MAX 8u
typedef struct { char ssid[33],password[65]; uint8_t hidden; } pet_network_config_t;
typedef struct {
    uint32_t version;
    uint8_t volume,mic_gain,brightness,speaker,quiet,captions,network_count,bridge_paired;
    pet_network_config_t networks[PET_NETWORK_MAX];
    char host[64],bridge_id[40];
    uint16_t port;
    uint8_t key[32];
    uint32_t quota_day;
    uint8_t quota_count;
} pet_config_t;
typedef struct {
    bool audio_present,bridge_present;
    uint8_t volume,mic_gain,brightness,speaker,network_count;
    pet_network_config_t networks[PET_NETWORK_MAX];
    char host[64];
    uint16_t port;
    uint8_t key[32];
} pet_legacy_t;
void pet_config_defaults(pet_config_t *out);
bool pet_config_valid(const pet_config_t *config);
bool pet_config_migrate(pet_config_t *out,const pet_legacy_t *legacy);
void pet_config_wipe(void *data,unsigned length);
