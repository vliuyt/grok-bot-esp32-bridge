#include "board_port.h"
const muse_board_t *pet_board(void) { return muse_board_get(); }
esp_err_t pet_board_init(void) { return pet_board()->init(); }
lv_display_t *pet_board_display_start(lv_indev_t **touch) { return pet_board()->display_start(touch); }
esp_err_t pet_board_audio_init(esp_codec_dev_handle_t *speaker,esp_codec_dev_handle_t *microphone) {
    return pet_board()->audio_init(speaker,microphone);
}
esp_err_t pet_board_read_power(muse_power_t *power) { return pet_board()->read_power(power); }

esp_err_t muse_gpio_button_init(muse_gpio_button_t *button,gpio_num_t gpio) {
    *button=(muse_gpio_button_t){.gpio=gpio};
    gpio_config_t config={.pin_bit_mask=1ULL<<gpio,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    return gpio_config(&config);
}
unsigned muse_gpio_button_poll(muse_gpio_button_t *button) {
    bool pressed=gpio_get_level(button->gpio)==0;
    if(pressed==button->pressed){button->stable=0;return 0;}
    if(++button->stable<3)return 0;
    button->stable=0;button->pressed=pressed;
    return pressed?MUSE_BTN_TALK_PRESS:MUSE_BTN_TALK_RELEASE;
}
