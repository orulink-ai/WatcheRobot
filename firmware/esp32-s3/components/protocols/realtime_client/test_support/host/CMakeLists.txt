cmake_minimum_required(VERSION 3.16)
project(realtime_client_host_tests C)

set(REALTIME_CLIENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(realtime_client_protocol_host_tests
    "${REALTIME_CLIENT_DIR}/src/realtime_protocol.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/test_realtime_protocol.c"
)

target_include_directories(realtime_client_protocol_host_tests PRIVATE
    "${REALTIME_CLIENT_DIR}/include"
    "${REALTIME_CLIENT_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

enable_testing()
add_test(NAME realtime_client_protocol_host_tests COMMAND realtime_client_protocol_host_tests)

add_executable(realtime_frame_assembler_host_tests
    "${REALTIME_CLIENT_DIR}/src/realtime_frame_assembler.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/test_realtime_frame_assembler.c"
)

target_include_directories(realtime_frame_assembler_host_tests PRIVATE
    "${REALTIME_CLIENT_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

add_test(NAME realtime_frame_assembler_host_tests COMMAND realtime_frame_assembler_host_tests)
