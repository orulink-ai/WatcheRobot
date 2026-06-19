idf_component_register(
    SRCS
        "app_main.c"
        "button_shutdown_low_power.c"
        "button_shutdown_sequence.c"
        "debug_cli.c"
        "debug_touch_guard.c"
        "mem_monitor.c"
        "stress_mode.c"
    INCLUDE_DIRS "."
    REQUIRES
        # Drivers
        bsp_watcher
        # HAL
        hal_audio
        hal_display
        hal_servo
        # Protocols
        ws_client
        discovery
        ble_service
        mcu_link
        # Services
        voice_service
        behavior_state_service
        anim_service
        mcu_motion_service
        mcu_led_service
        mcu_power_service
        mcu_sensor_service
        # camera_service
        control_ingress
        ota_service
        power_monitor_service
        vfs
        # Utils
        wifi_manager
        boot_anim
        # ESP-IDF
        sensecap-watcher
)
