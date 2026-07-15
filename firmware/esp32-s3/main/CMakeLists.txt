set(WATCHER_MAIN_SRCS
        "app_center_manager.c"
        "agent_animation_flow_core.c"
        "agent_runtime.c"
        "app_ui_mode_core.c"
        "app_main.c"
        "button_shutdown_feedback.c"
        "button_shutdown_hold_detector.c"
        "button_shutdown_low_power.c"
        "button_shutdown_power_channel.c"
        "button_shutdown_sequence.c"
        "button_shutdown_soft_off.c"
        "client_app.c"
        "debug_cli.c"
        "debug_touch_guard.c"
        "factory_settings_factory_events.c"
        "factory_settings_perf_stats.c"
        "factory_settings_scroll_curve.c"
        "factory_settings_ui.c"
        "factory_settings_original_ui/screens/ui_Page_About.c"
        "factory_settings_original_ui/screens/ui_Page_Connect.c"
        "factory_settings_original_ui/screens/ui_Page_Network.c"
        "factory_settings_original_ui/screens/ui_Page_Set.c"
        "factory_settings_original_ui/screens/ui_Page_Sleep.c"
        "factory_settings_original_ui/screens/ui_Page_Slider.c"
        "factory_settings_original_ui/screens/ui_Page_Swipe.c"
        "factory_home_ui/fonts/ui_font_Font12.c"
        "factory_home_ui/fonts/ui_font_font_bold.c"
        "factory_home_ui/fonts/ui_font_semibold28.c"
        "factory_home_ui/fonts/ui_font_semibold42.c"
        "factory_home_ui/images/ui_img_battery_charging_png.c"
        "factory_home_ui/images/ui_img_battery_frame_png.c"
        "factory_home_ui/images/ui_img_battery_warn_png.c"
        "factory_home_ui/images/ui_img_ble_png.c"
        "factory_home_ui/images/ui_img_1158392298.c"
        "factory_home_ui/images/ui_img_1415499115.c"
        "factory_home_ui/images/ui_img_1722191473.c"
        "factory_home_ui/images/ui_img_agent_robot_png.c"
        "factory_home_ui/images/ui_img_agent_robot_focused_png.c"
        "factory_home_ui/images/ui_img_currenttask_png.c"
        "factory_home_ui/images/ui_img_no_wifi_png.c"
        "factory_home_ui/images/ui_img_page_main_png.c"
        "factory_home_ui/images/ui_img_phone_control_png.c"
        "factory_home_ui/images/ui_img_phone_control_focused_png.c"
        "factory_home_ui/images/ui_img_python_sdk_png.c"
        "factory_home_ui/images/ui_img_python_sdk_focused_png.c"
        "factory_home_ui/images/ui_img_setback_png.c"
        "factory_home_ui/images/ui_img_setting_f_png.c"
        "factory_home_ui/images/ui_img_setting_png.c"
        "factory_home_ui/images/ui_img_volicon_png.c"
        "factory_home_ui/images/ui_img_brighticon_png.c"
        "factory_home_ui/images/ui_img_button_cancel_png.c"
        "factory_home_ui/images/ui_img_arrow_png.c"
        "factory_home_ui/images/ui_img_wifi_3_png.c"
        "factory_home_ui/images/ui_img_swipe_png.c"
        "factory_home_ui/images/ui_img_task_template_png.c"
        "launcher_factory_home.c"
        "launcher_screen_cache_policy.c"
        "launcher_home_model.c"
        "mem_monitor.c"
        "sdk_control_app.c"
        "sdk_control_ui.c"
        "stress_mode.c"
        "watcher_app_runtime.c"
)

if(CONFIG_WATCHER_APP_CENTER_ENABLE)
    list(APPEND WATCHER_MAIN_SRCS
        "app_center.c"
        "app_center_exit_policy.c"
        "app_center_firmware_sources.c"
    )
else()
    list(APPEND WATCHER_MAIN_SRCS "app_center_disabled.c")
endif()

idf_component_register(
    SRCS
        ${WATCHER_MAIN_SRCS}
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
        realtime_client
        discovery
        ble_service
        mcu_link
        # Services
        agent_audio_player
        voice_service
        behavior_state_service
        anim_service
        mcu_motion_service
        mcu_led_service
        mcu_power_service
        mcu_sensor_service
        control_ingress
        watcher_sdk
        watcher_input_router
        ota_service
        power_monitor_service
        provision_manager
        vfs
        # Utils
        wifi_manager
        boot_anim
        time_sync_service
        # ESP-IDF
        json
        esp_http_client
        nvs_flash
        lvgl
        esp_lvgl_port
        sensecap-watcher
)

idf_build_get_property(watcher_build_python PYTHON)
set(WATCHER_BUILD_INFO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/watcher_build_info.h")
add_custom_target(watcher_build_info_gen
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${watcher_build_python}
            "${CMAKE_SOURCE_DIR}/tools/generate_build_info.py"
            --repo-root "${CMAKE_SOURCE_DIR}/../.."
            --output "${WATCHER_BUILD_INFO_HEADER}"
    BYPRODUCTS "${WATCHER_BUILD_INFO_HEADER}"
    COMMENT "Generating WatcheRobot ESP32 build metadata")
add_dependencies(${COMPONENT_LIB} watcher_build_info_gen)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(DEFINED APP_CENTER_FIRMWARE_APP)
    string(TOUPPER "${APP_CENTER_FIRMWARE_APP}" APP_CENTER_FIRMWARE_APP_UPPER)
    if(APP_CENTER_FIRMWARE_APP_UPPER STREQUAL "PHONE_CONTROL")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE WATCHER_PHONE_CONTROL_FIRMWARE_APP=1)
    elseif(NOT APP_CENTER_FIRMWARE_APP_UPPER STREQUAL "OFF")
        message(FATAL_ERROR "APP_CENTER_FIRMWARE_APP must be PHONE_CONTROL or OFF")
    endif()
endif()
