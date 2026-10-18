#pragma once
#include "muse_board.h"
esp_err_t pet_board_init(void);
lv_display_t *pet_board_display_start(lv_indev_t **touch);
const muse_board_t *pet_board(void);
esp_err_t pet_board_audio_init(esp_codec_dev_handle_t *speaker,esp_codec_dev_handle_t *microphone);
esp_err_t pet_board_read_power(muse_power_t *power);
