idf_component_register(
    SRCS
        "src/ws_client.c"
        "src/ws_event_ui_policy.c"
        "src/ws_audio_uplink_policy.c"
        "src/ws_audio_codec_negotiation.c"
        "src/ws_tts_buffer_policy.c"
        "src/ws_tts_stream_policy.c"
        "src/ws_router.c"
        "src/ws_handlers.c"
    INCLUDE_DIRS "include"
    REQUIRES "json" "esp_event"
    PRIV_REQUIRES
        "camera_service"
        "control_ingress"
        "esp_wifi"
        "freertos"
        "hal_audio"
        "hal_servo"
        "mcu_led_service"
        "ota_service"
        "behavior_state_service"
        "sfx_service"
        "server_pairing"
        "voice_service"
        "anim_service"

)
