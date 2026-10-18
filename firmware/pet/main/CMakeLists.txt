idf_component_register(SRCS "pet_app.c" "pet_console.c" INCLUDE_DIRS "."
    REQUIRES board_port pet_core pet_store pet_audio pet_ui pet_network pet_provision pet_link
    PRIV_REQUIRES esp_timer esp_driver_usb_serial_jtag esp_system)
