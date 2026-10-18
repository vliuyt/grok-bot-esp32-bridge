#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PET_PCM_CAPACITY 24u
#define PET_PCM_FRAMES 320u
typedef struct {
    uint32_t generation,turn,epoch;
    uint16_t bytes;
    int16_t pcm[PET_PCM_FRAMES];
} pet_pcm_packet_t;
typedef struct {
    pet_pcm_packet_t packets[PET_PCM_CAPACITY];
    uint32_t generation,turn;
    unsigned head,count;
    bool done;
} pet_pcm_queue_t;
/* Caller owns synchronization. Reset returns discarded packet credits for the OLD turn. */
unsigned pet_pcm_reset(pet_pcm_queue_t *queue,uint32_t generation,uint32_t turn);
int pet_pcm_push(pet_pcm_queue_t *queue,uint32_t generation,uint32_t turn,const void *pcm,size_t bytes);
bool pet_pcm_pop(pet_pcm_queue_t *queue,pet_pcm_packet_t *packet);
bool pet_pcm_done(pet_pcm_queue_t *queue,uint32_t generation,uint32_t turn);
bool pet_pcm_drained(const pet_pcm_queue_t *queue);
