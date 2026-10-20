#include "pet_fonts.h"
#if !LV_USE_FONT_COMPRESSED
#error "Readable pet fonts require LV_USE_FONT_COMPRESSED"
#endif
const lv_font_t *pet_font(pet_font_role_t role){
    switch(role){
    case PET_FONT_HINT:return &pet_font_22;
    case PET_FONT_READER:return &pet_font_30;
    default:return &pet_font_28;
    }
}
