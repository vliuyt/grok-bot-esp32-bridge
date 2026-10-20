idf_component_register(SRCS "pet_ui.c" "pet_fonts.c" "fonts/muse_font_cjk_16.c"
    "fonts/pet_font_22.c" "fonts/pet_font_28.c" "fonts/pet_font_30.c" "fonts/pet_font_rare16.c" INCLUDE_DIRS "."
    REQUIRES pet_core board_port PRIV_REQUIRES esp_timer mbedtls esp_driver_usb_serial_jtag)
