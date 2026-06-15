cmake_minimum_required(VERSION 3.20)

project(button_shutdown_sequence_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(MAIN_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(button_shutdown_sequence_host_tests
    "${MAIN_COMPONENT_DIR}/button_shutdown_low_power.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_sequence.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_button_shutdown_controller.c"
)

target_include_directories(button_shutdown_sequence_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME button_shutdown_sequence_host_tests COMMAND button_shutdown_sequence_host_tests)
