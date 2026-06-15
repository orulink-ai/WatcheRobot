idf_component_register(
    SRCS
        "src/behavior_action_parser.c"
        "src/behavior_catalog_parser.c"
        "src/behavior_executor.c"
        "src/behavior_scheduler.c"
        "src/behavior_state_service.c"
        "src/behavior_types.c"
    INCLUDE_DIRS "include"
    REQUIRES
        "anim_service"
        "freertos"
        "hal_servo"
        "json"
        "sfx_service"
    PRIV_REQUIRES
        "esp_timer"
)
