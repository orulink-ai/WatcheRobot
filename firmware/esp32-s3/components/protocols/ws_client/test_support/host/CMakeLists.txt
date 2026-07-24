cmake_minimum_required(VERSION 3.20)

project(ws_client_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(WS_CLIENT_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(ws_tts_buffer_policy_host_tests
    "${WS_CLIENT_COMPONENT_DIR}/src/ws_tts_buffer_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_ws_tts_buffer_policy.c"
)

target_include_directories(ws_tts_buffer_policy_host_tests PRIVATE
    "${WS_CLIENT_COMPONENT_DIR}/src"
)

add_test(NAME ws_tts_buffer_policy_host_tests COMMAND ws_tts_buffer_policy_host_tests)

add_executable(ws_audio_uplink_policy_host_tests
    "${WS_CLIENT_COMPONENT_DIR}/src/ws_audio_uplink_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_ws_audio_uplink_policy.c"
)

target_include_directories(ws_audio_uplink_policy_host_tests PRIVATE
    "${WS_CLIENT_COMPONENT_DIR}/src"
)

add_test(NAME ws_audio_uplink_policy_host_tests COMMAND ws_audio_uplink_policy_host_tests)

add_executable(ws_audio_codec_negotiation_host_tests
    src/test_ws_audio_codec_negotiation.c
    ../../src/ws_audio_codec_negotiation.c
)
target_include_directories(ws_audio_codec_negotiation_host_tests PRIVATE ../../src)
add_test(NAME ws_audio_codec_negotiation_host_tests COMMAND ws_audio_codec_negotiation_host_tests)

add_executable(ws_event_ui_policy_host_tests
    "${WS_CLIENT_COMPONENT_DIR}/src/ws_event_ui_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_ws_event_ui_policy.c"
)

target_include_directories(ws_event_ui_policy_host_tests PRIVATE
    "${WS_CLIENT_COMPONENT_DIR}/src"
)

add_test(NAME ws_event_ui_policy_host_tests COMMAND ws_event_ui_policy_host_tests)

add_executable(ws_tts_stream_policy_host_tests
    "${WS_CLIENT_COMPONENT_DIR}/src/ws_tts_stream_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_ws_tts_stream_policy.c"
)

target_include_directories(ws_tts_stream_policy_host_tests PRIVATE
    "${WS_CLIENT_COMPONENT_DIR}/src"
)

add_test(NAME ws_tts_stream_policy_host_tests COMMAND ws_tts_stream_policy_host_tests)
