#pragma once
#include "esp_err.h"
#include "pet_core.h"
#include "pet_config.h"
#include "pet_pcm_queue.h"
typedef struct {
    void (*event)(pet_event_t event,void *context);
    void (*capture)(uint32_t generation,uint32_t turn,const int16_t *pcm,size_t bytes,void *context);
    void (*stopped)(uint32_t generation,uint32_t turn,void *context);
    void (*credit)(uint32_t generation,uint32_t turn,void *context);
    void (*fault)(void *context);
    void *context;
} pet_audio_callbacks_t;
typedef struct {unsigned queued;uint32_t played_frames,captured_frames;bool recording,powered,fault;int mic_left_db,mic_right_db;} pet_audio_status_t;
esp_err_t pet_audio_start(const pet_config_t *config,pet_audio_callbacks_t callbacks);
void pet_audio_begin(uint32_t generation,uint32_t turn);
void pet_audio_finish(void);
void pet_audio_clear(uint32_t generation,uint32_t turn);
uint32_t pet_audio_receive_epoch(void);
int pet_audio_push(uint32_t generation,uint32_t turn,uint32_t receive_epoch,const void *pcm,size_t bytes);
void pet_audio_flush_playback(uint32_t generation,uint32_t turn);
void pet_audio_done(uint32_t generation,uint32_t turn);
void pet_audio_power(bool on);
void pet_audio_settings(const pet_config_t *config);
pet_audio_status_t pet_audio_status(void);
