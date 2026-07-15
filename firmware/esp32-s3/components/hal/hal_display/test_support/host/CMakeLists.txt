cmake_minimum_required(VERSION 3.20)

project(hal_display_boundary_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../src/hal_display.c" HAL_DISPLAY_SOURCE)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt" HAL_DISPLAY_CMAKE)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../services/anim_service/src/display_ui.c" DISPLAY_UI_SOURCE)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../services/anim_service/include/display_ui.h" DISPLAY_UI_HEADER)

add_executable(hal_display_boundary_host_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_hal_display_boundaries.c"
)

target_compile_definitions(hal_display_boundary_host_tests PRIVATE
    HAL_DISPLAY_SOURCE="${HAL_DISPLAY_SOURCE}"
    HAL_DISPLAY_CMAKE="${HAL_DISPLAY_CMAKE}"
    DISPLAY_UI_SOURCE="${DISPLAY_UI_SOURCE}"
    DISPLAY_UI_HEADER="${DISPLAY_UI_HEADER}"
)

if(MSVC)
    target_compile_options(hal_display_boundary_host_tests PRIVATE /W4 /WX)
else()
    target_compile_options(hal_display_boundary_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME hal_display_boundary_host_tests COMMAND hal_display_boundary_host_tests)
