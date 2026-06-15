
set(requires "esp_mm" "esp_psram" "esp_lcd" "fatfs" "esp_timer" "driver" "spiffs" "lvgl" "esp_io_expander_pca95xx_16bit" "esp_lvgl_port" "sscma_client")

idf_component_register(
    SRCS "sensecap-watcher.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "${IDF_PATH}/components/fatfs/diskio"
        "${IDF_PATH}/components/fatfs/src"
        "${IDF_PATH}/components/sdmmc"
    REQUIRES ${requires}
)
