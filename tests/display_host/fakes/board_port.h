#pragma once
#include "lvgl.h"
#include "esp_err.h"
typedef struct {
    void (*panel_sleep)(bool);
    void (*set_brightness)(int);
    bool (*display_lock)(int);
    void (*display_unlock)(void);
} muse_board_t;
lv_display_t *pet_board_display_start(lv_indev_t **touch);
const muse_board_t *pet_board(void);
