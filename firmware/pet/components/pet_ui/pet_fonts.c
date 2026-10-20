#include "pet_fonts.h"
#if !LV_USE_FONT_COMPRESSED
#error "Readable pet fonts require LV_USE_FONT_COMPRESSED"
#endif
#if LV_DRAW_SW_DRAW_UNIT_CNT != 1
/* Pinned LVGL's global RLE decoder is not reentrant across draw workers. */
#error "Readable pet fonts require one software draw unit"
#endif
const lv_font_t *pet_font(pet_font_role_t role){
    switch(role){
    case PET_FONT_HINT:return &pet_font_22;
    case PET_FONT_READER:return &pet_font_30;
    default:return &pet_font_28;
    }
}
