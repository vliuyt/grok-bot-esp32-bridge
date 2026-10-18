idf_component_register(SRCS "board_port.c" "vendor/board_waveshare_s3_143c.c" "vendor/muse_lcd_bands.c"
    INCLUDE_DIRS "." "vendor" REQUIRES espressif__esp_codec_dev espressif__esp_lvgl_adapter espressif__esp_lcd_co5300 lvgl__lvgl
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_usb_serial_jtag esp_adc esp_lcd esp_psram)
