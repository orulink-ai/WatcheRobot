cmake_minimum_required(VERSION 3.20)

project(provision_manager_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(PROVISION_MANAGER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(provision_manager_host_tests
    "${PROVISION_MANAGER_DIR}/src/provision_manager.c"
    "${PROVISION_MANAGER_DIR}/src/provision_state_machine.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_provision_manager.c"
)

target_include_directories(provision_manager_host_tests PRIVATE
    "${PROVISION_MANAGER_DIR}/include"
    "${PROVISION_MANAGER_DIR}/src"
)

add_test(NAME provision_manager_host_tests COMMAND provision_manager_host_tests)
