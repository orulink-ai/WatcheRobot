cmake_minimum_required(VERSION 3.20)

project(control_ingress_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(CONTROL_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(control_ingress_host_tests
    "${CONTROL_COMPONENT_DIR}/src/control_ingress.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_control_ingress.c"
)

target_compile_definitions(control_ingress_host_tests PRIVATE
    CONTROL_INGRESS_ENABLE_TEST_API
)

target_include_directories(control_ingress_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CONTROL_COMPONENT_DIR}/include"
)

add_test(NAME control_ingress_host_tests COMMAND control_ingress_host_tests)
