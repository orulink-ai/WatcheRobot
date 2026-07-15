cmake_minimum_required(VERSION 3.16)
project(agent_audio_player_host_tests C)

set(AGENT_AUDIO_PLAYER_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(agent_audio_processing_host_tests
    "${AGENT_AUDIO_PLAYER_DIR}/src/agent_audio_processing.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/test_agent_audio_processing.c"
)

target_include_directories(agent_audio_processing_host_tests PRIVATE
    "${AGENT_AUDIO_PLAYER_DIR}/src"
)

enable_testing()
add_test(NAME agent_audio_processing_host_tests COMMAND agent_audio_processing_host_tests)
