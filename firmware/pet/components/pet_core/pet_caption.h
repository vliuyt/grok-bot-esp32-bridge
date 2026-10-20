#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "pet_core.h"
typedef enum {PET_CAPTION_FULL,PET_CAPTION_COMPACT,PET_CAPTION_OFF} pet_caption_mode_t;
bool pet_caption_mode_valid(pet_caption_mode_t mode);
/* Produces a valid UTF-8 prefix, never a partial code point. Invalid input ends
 * the prefix. capacity includes NUL; a zero capacity never dereferences out. */
size_t pet_caption_text(pet_caption_mode_t mode,const char *input,char *out,size_t capacity);

#define PET_CAPTION_BYTES 320u
typedef struct {char text[PET_CAPTION_BYTES+1];uint32_t generation,turn;bool valid;} pet_caption_buffer_t;
void pet_caption_reset(pet_caption_buffer_t *buffer);
bool pet_caption_receive(pet_caption_buffer_t *buffer,const pet_state_t *state,uint32_t generation,uint32_t turn,const char *text);
size_t pet_caption_current(pet_caption_buffer_t *buffer,const pet_state_t *state,pet_caption_mode_t mode,char *out,size_t capacity);
