idf_component_register(SRCS "pet_config.c" "pet_json.c" "pet_store.c" INCLUDE_DIRS "." REQUIRES nvs_flash espressif__cjson PRIV_REQUIRES freertos)
