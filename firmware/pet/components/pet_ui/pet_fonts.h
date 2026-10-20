#pragma once
#include "lvgl.h"
typedef enum {PET_FONT_HINT,PET_FONT_BODY,PET_FONT_READER} pet_font_role_t;
extern const lv_font_t pet_font_22,pet_font_28,pet_font_30,pet_font_rare16;
const lv_font_t *pet_font(pet_font_role_t role);
