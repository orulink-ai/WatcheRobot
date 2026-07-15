idf_component_register(
    SRCS
        "src/realtime_client.c"
        "src/realtime_frame_assembler.c"
        "src/realtime_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES "json" "esp_event"
    PRIV_REQUIRES
        "freertos"
        "esp_websocket_client"
)
