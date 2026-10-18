idf_component_register(SRCS "pet_network.c" "pet_transaction.c" INCLUDE_DIRS "." REQUIRES pet_store PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer mdns mbedtls)
