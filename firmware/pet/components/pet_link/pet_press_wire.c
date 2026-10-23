#include "pet_press_wire.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
size_t pet_press_encode(bool companion_mood,uint32_t generation,uint32_t turn,
                       const pet_state_t *state,char *out,size_t capacity){
 if(!out||!capacity)return 0;
 if(!companion_mood){if(capacity<6)return 0;memcpy(out,"press",6);return 5;}
 if(!state||state->generation!=generation||state->turn!=turn||state->dialogue!=PET_DIALOGUE_LISTEN||
    !isfinite(state->turn_warmth)||state->turn_warmth<0||state->turn_warmth>100)return 0;
 int n=snprintf(out,capacity,"{\"op\":\"press\",\"generation\":%u,\"turn\":%u,\"mood\":\"%s\"}",
                (unsigned)generation,(unsigned)turn,state->turn_warmth>0?"warm":"neutral");
 return n>0&&(size_t)n<capacity?(size_t)n:0;
}
