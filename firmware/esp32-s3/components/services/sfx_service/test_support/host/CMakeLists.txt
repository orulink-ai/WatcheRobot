cmake_minimum_required(VERSION 3.20)

project(sfx_service_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(SFX_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(sfx_schedule_host_tests
    "${SFX_COMPONENT_DIR}/src/sfx_schedule.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_sfx_schedule.c"
)

target_include_directories(sfx_schedule_host_tests PRIVATE
    "${SFX_COMPONENT_DIR}/include"
)

add_test(NAME sfx_schedule_host_tests COMMAND sfx_schedule_host_tests)
