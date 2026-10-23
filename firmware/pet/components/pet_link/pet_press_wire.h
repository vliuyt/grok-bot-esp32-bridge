#pragma once
#include "pet_core.h"
#include <stddef.h>

/* Only the frozen state of this recording may accompany a negotiated press. */
size_t pet_press_encode(bool companion_mood,uint32_t generation,uint32_t turn,
                       const pet_state_t *state,char *out,size_t capacity);
