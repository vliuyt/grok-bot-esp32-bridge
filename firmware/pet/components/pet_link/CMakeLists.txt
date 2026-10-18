idf_component_register(SRCS "pet_link.c" "pet_link_wire.c" INCLUDE_DIRS "." REQUIRES pet_core pet_store pet_network pet_audio PRIV_REQUIRES esp-tls esp_timer esp_system mdns cjson)
