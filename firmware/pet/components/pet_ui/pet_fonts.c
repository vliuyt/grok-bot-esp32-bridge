#include "pet_fonts.h"
const lv_font_t *pet_font(pet_font_role_t role){
    switch(role){
    case PET_FONT_HINT:return &pet_font_22;
    case PET_FONT_READER:return &pet_font_30;
    default:return &pet_font_28;
    }
}
