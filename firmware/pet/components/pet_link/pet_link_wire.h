#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define PET_LINK_FRAME_MAX 1029u
typedef struct {uint8_t bytes[PET_LINK_FRAME_MAX+4];size_t used;} pet_link_rx_t;
typedef bool (*pet_link_frame_cb)(uint8_t type,uint32_t turn,const void *bytes,size_t n,void *ctx);
size_t pet_link_encode(void *out,size_t capacity,uint8_t type,uint32_t turn,const void *bytes,size_t n);
bool pet_link_feed(pet_link_rx_t *rx,const void *bytes,size_t n,pet_link_frame_cb callback,void *ctx);
bool pet_link_should_send(uint8_t type,uint32_t packet_generation,uint32_t packet_turn,uint32_t generation,uint32_t turn);

unsigned pet_link_retry_ms(unsigned failures);
