idf_component_register(SRCS "pet_ui.c" "fonts/muse_font_cjk_16.c" INCLUDE_DIRS "."
    REQUIRES pet_core board_port PRIV_REQUIRES esp_timer mbedtls esp_driver_usb_serial_jtag)
