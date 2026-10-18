idf_component_register(SRCS "pet_audio.c" "pet_pcm_queue.c" INCLUDE_DIRS "."
    REQUIRES pet_core pet_store board_port PRIV_REQUIRES esp_timer esp_psram)
