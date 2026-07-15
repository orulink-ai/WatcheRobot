idf_component_register(
    SRCS "src/ota_service.c"
    INCLUDE_DIRS "include"
    REQUIRES "app_update" "esp_http_client" "esp_https_ota"
    PRIV_REQUIRES  "esp_timer" "mbedtls"
)
