idf_component_register(
    SRCS "src/hal_display.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        "hal_button"
        "sensecap-watcher"
        "lvgl"
        "esp_lvgl_port"
)
