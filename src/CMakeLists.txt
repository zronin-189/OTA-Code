idf_component_register(SRCS "app_main.c" "wifi.c" "ota_http.c" "ota_update.c" "ota_crypto.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_client app_update esp_partition mbedtls cjson bt lwip bootloader_support)
