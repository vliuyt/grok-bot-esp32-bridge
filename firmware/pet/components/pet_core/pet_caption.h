#pragma once
#include <stddef.h>
#include <stdbool.h>
typedef enum {PET_CAPTION_FULL,PET_CAPTION_COMPACT,PET_CAPTION_OFF} pet_caption_mode_t;
bool pet_caption_mode_valid(pet_caption_mode_t mode);
/* Produces a valid UTF-8 prefix, never a partial code point. Invalid input ends
 * the prefix. capacity includes NUL; a zero capacity never dereferences out. */
size_t pet_caption_text(pet_caption_mode_t mode,const char *input,char *out,size_t capacity);
