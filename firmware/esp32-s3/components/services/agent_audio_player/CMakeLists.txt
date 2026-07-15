idf_component_register(
    SRCS "src/agent_audio_player.c" "src/agent_audio_processing.c"
    INCLUDE_DIRS "include"
    REQUIRES "hal_audio" "freertos"
)
