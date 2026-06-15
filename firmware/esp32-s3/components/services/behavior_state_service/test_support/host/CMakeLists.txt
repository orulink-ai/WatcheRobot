cmake_minimum_required(VERSION 3.20)

project(behavior_state_service_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(BEHAVIOR_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(DEFINED ENV{IDF_PATH})
    set(IDF_PATH "$ENV{IDF_PATH}")
else()
    set(IDF_PATH "C:/Espressif/frameworks/esp-idf-v5.2.1")
endif()

set(CJSON_DIR "${IDF_PATH}/components/json/cJSON")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "cJSON.c not found. Set IDF_PATH to an ESP-IDF checkout.")
endif()

file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../spiffs/behavior/states.json" BEHAVIOR_PRODUCTION_STATES_JSON)

add_executable(behavior_parser_host_tests
    "${CJSON_DIR}/cJSON.c"
    "${BEHAVIOR_COMPONENT_DIR}/src/behavior_action_parser.c"
    "${BEHAVIOR_COMPONENT_DIR}/src/behavior_catalog_parser.c"
    "${BEHAVIOR_COMPONENT_DIR}/src/behavior_types.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_behavior_parsers.c"
)

target_include_directories(behavior_parser_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${BEHAVIOR_COMPONENT_DIR}/include"
    "${CJSON_DIR}"
)

target_compile_definitions(behavior_parser_host_tests PRIVATE
    BEHAVIOR_PRODUCTION_STATES_JSON="${BEHAVIOR_PRODUCTION_STATES_JSON}"
)

add_test(NAME behavior_parser_host_tests COMMAND behavior_parser_host_tests)

add_executable(behavior_scheduler_host_tests
    "${BEHAVIOR_COMPONENT_DIR}/src/behavior_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_behavior_scheduler.c"
)

target_include_directories(behavior_scheduler_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${BEHAVIOR_COMPONENT_DIR}/include"
)

add_test(NAME behavior_scheduler_host_tests COMMAND behavior_scheduler_host_tests)
