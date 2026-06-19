cmake_minimum_required(VERSION 3.20)

project(power_monitor_service_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(POWER_MONITOR_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(power_monitor_logic_host_tests
    "${POWER_MONITOR_COMPONENT_DIR}/src/power_monitor_logic.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_power_monitor_logic.c"
)

target_include_directories(power_monitor_logic_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${POWER_MONITOR_COMPONENT_DIR}/include"
)

add_test(NAME power_monitor_logic_host_tests COMMAND power_monitor_logic_host_tests)
