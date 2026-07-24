cmake_minimum_required(VERSION 3.20)

project(watcher_sdk_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(WATCHER_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(watcher_sdk_core_host_tests
    "${WATCHER_SDK_DIR}/src/watcher_sdk_core.c"
    "${WATCHER_SDK_DIR}/src/watcher_sdk_motion_tracker.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_watcher_sdk_core.c"
)

target_include_directories(watcher_sdk_core_host_tests PRIVATE
    "${WATCHER_SDK_DIR}/include"
)

add_test(NAME watcher_sdk_core_host_tests COMMAND watcher_sdk_core_host_tests)

set(WATCHER_IDF_PATH "" CACHE PATH "ESP-IDF checkout used by host protocol tests")
set(IDF_PATH_HINTS
    "${WATCHER_IDF_PATH}"
    "$ENV{IDF_PATH}"
    "$ENV{HOME}/esp/esp-idf"
    "C:/Espressif/frameworks/esp-idf-v5.2.1"
)
find_path(CJSON_DIR
    NAMES cJSON.c
    HINTS ${IDF_PATH_HINTS}
    PATH_SUFFIXES components/json/cJSON
)

if(CJSON_DIR AND EXISTS "${CJSON_DIR}/cJSON.c" AND EXISTS "${CJSON_DIR}/cJSON.h")
    add_executable(watcher_sdk_protocol_host_tests
        "${CJSON_DIR}/cJSON.c"
        "${WATCHER_SDK_DIR}/src/watcher_sdk_protocol.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/test_watcher_sdk_protocol.c"
    )

    target_include_directories(watcher_sdk_protocol_host_tests PRIVATE
        "${WATCHER_SDK_DIR}/include"
        "${CJSON_DIR}"
    )

    add_test(NAME watcher_sdk_protocol_host_tests COMMAND watcher_sdk_protocol_host_tests)
else()
    message(STATUS "Skipping watcher_sdk_protocol_host_tests: cJSON.c and cJSON.h were not found in an ESP-IDF checkout")
endif()
