cmake_minimum_required(VERSION 3.20)

project(sfx_service_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(SFX_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(DEFINED ENV{IDF_PATH})
    set(IDF_PATH "$ENV{IDF_PATH}")
else()
    set(IDF_PATH "C:/Espressif/frameworks/esp-idf-v5.2.1")
endif()

set(CJSON_DIR "${IDF_PATH}/components/json/cJSON")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "cJSON.c not found. Set IDF_PATH to an ESP-IDF checkout.")
endif()

set(SFX_TEST_DIR "sfx")
set(SFX_TEST_ABS_DIR "${CMAKE_CURRENT_BINARY_DIR}/${SFX_TEST_DIR}")
file(MAKE_DIRECTORY "${SFX_TEST_ABS_DIR}")
file(WRITE "${SFX_TEST_ABS_DIR}/manifest.json" "{ \"sounds\": { \"boot\": \"boot.pcm\", \"standby\": \"standby.pcm\" } }\n")
file(WRITE "${SFX_TEST_ABS_DIR}/boot.pcm" "boot-audio")
file(WRITE "${SFX_TEST_ABS_DIR}/standby.pcm" "standby-audio")

add_executable(sfx_schedule_host_tests
    "${SFX_COMPONENT_DIR}/src/sfx_schedule.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_sfx_schedule.c"
)

target_include_directories(sfx_schedule_host_tests PRIVATE
    "${SFX_COMPONENT_DIR}/include"
)

add_test(NAME sfx_schedule_host_tests COMMAND sfx_schedule_host_tests)

add_executable(sfx_startup_load_host_tests
    "${CJSON_DIR}/cJSON.c"
    "${SFX_COMPONENT_DIR}/src/sfx_schedule.c"
    "${SFX_COMPONENT_DIR}/src/sfx_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mem_monitor_stub.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_sfx_startup_load.c"
)

target_include_directories(sfx_startup_load_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${SFX_COMPONENT_DIR}/include"
    "${CJSON_DIR}"
)

target_compile_definitions(sfx_startup_load_host_tests PRIVATE
    SFX_ENABLE_TEST_API=1
    SFX_MANIFEST_PATH="${SFX_TEST_DIR}/manifest.json"
    SFX_DIR="${SFX_TEST_DIR}"
)

add_test(NAME sfx_startup_load_host_tests COMMAND sfx_startup_load_host_tests)
set_tests_properties(sfx_startup_load_host_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
