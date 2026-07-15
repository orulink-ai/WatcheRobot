cmake_minimum_required(VERSION 3.20)

project(voice_service_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(VOICE_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(voice_wake_lifecycle_host_tests
    "${VOICE_COMPONENT_DIR}/src/voice_wake_lifecycle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_voice_wake_lifecycle.c"
)

target_include_directories(voice_wake_lifecycle_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${VOICE_COMPONENT_DIR}/src"
)

add_test(NAME voice_wake_lifecycle_host_tests COMMAND voice_wake_lifecycle_host_tests)

add_executable(voice_remote_control_core_host_tests
    "${VOICE_COMPONENT_DIR}/src/voice_remote_control_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_voice_remote_control_core.c"
)

target_include_directories(voice_remote_control_core_host_tests PRIVATE
    "${VOICE_COMPONENT_DIR}/src"
)

add_test(NAME voice_remote_control_core_host_tests COMMAND voice_remote_control_core_host_tests)

add_executable(voice_upload_guard_host_tests
    "${VOICE_COMPONENT_DIR}/src/voice_upload_guard.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_voice_upload_guard.c"
)

target_include_directories(voice_upload_guard_host_tests PRIVATE
    "${VOICE_COMPONENT_DIR}/src"
)

add_test(NAME voice_upload_guard_host_tests COMMAND voice_upload_guard_host_tests)
