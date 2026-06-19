idf_component_register(
    SRCS "src/server_pairing.c"
    INCLUDE_DIRS "include"
    REQUIRES "json" "nvs_flash" "mbedtls" "esp_hw_support"
)
