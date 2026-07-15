idf_component_register(
    SRCS
        "src/power_monitor_logic.c"
        "src/power_monitor_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        "behavior_state_service"
        "esp_timer"
        "sensecap-watcher"
)
