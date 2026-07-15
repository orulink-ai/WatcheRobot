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

if(DEFINED ENV{IDF_PATH})
    set(IDF_PATH "$ENV{IDF_PATH}")
else()
    set(IDF_PATH "C:/Espressif/frameworks/esp-idf-v5.2.1")
endif()
set(CJSON_DIR "${IDF_PATH}/components/json/cJSON")

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
