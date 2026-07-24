idf_component_register(
    SRCS
        "src/voice_fsm.c"
        "src/voice_listening_ui_policy.c"
        "src/hal_wake_word.c"
        "src/voice_remote_control_core.c"
        "src/voice_upload_guard.c"
        "src/voice_wake_lifecycle.c"
    INCLUDE_DIRS "include"
    REQUIRES "hal_audio" "freertos"
    PRIV_REQUIRES
        "sensecap-watcher"
        "ws_client"
        "behavior_state_service"
        "anim_service"
        "sfx_service"

)
