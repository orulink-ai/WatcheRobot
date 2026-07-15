cmake_minimum_required(VERSION 3.20)

project(button_shutdown_sequence_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(MAIN_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(button_shutdown_sequence_host_tests
    "${MAIN_COMPONENT_DIR}/button_shutdown_feedback.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_hold_detector.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_low_power.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_power_channel.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_sequence.c"
    "${MAIN_COMPONENT_DIR}/button_shutdown_soft_off.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_button_shutdown_controller.c"
)

target_include_directories(button_shutdown_sequence_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME button_shutdown_sequence_host_tests COMMAND button_shutdown_sequence_host_tests)

add_executable(watcher_app_runtime_host_tests
    "${MAIN_COMPONENT_DIR}/watcher_app_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_watcher_app_runtime.c"
)

target_include_directories(watcher_app_runtime_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
    "${MAIN_COMPONENT_DIR}/../components/services/watcher_input_router/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_test(NAME watcher_app_runtime_host_tests COMMAND watcher_app_runtime_host_tests)

add_executable(watcher_input_router_host_tests
    "${MAIN_COMPONENT_DIR}/../components/services/watcher_input_router/src/watcher_input_router.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_watcher_input_router.c"
)

target_compile_definitions(watcher_input_router_host_tests PRIVATE WATCHER_INPUT_ROUTER_HOST_TEST=1)
target_include_directories(watcher_input_router_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}/../components/services/watcher_input_router/include"
)

add_test(NAME watcher_input_router_host_tests COMMAND watcher_input_router_host_tests)

add_executable(agent_runtime_host_tests
    "${MAIN_COMPONENT_DIR}/agent_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_agent_runtime.c"
)

target_include_directories(agent_runtime_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_test(NAME agent_runtime_host_tests COMMAND agent_runtime_host_tests)

add_executable(app_ui_mode_core_host_tests
    "${MAIN_COMPONENT_DIR}/app_ui_mode_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_app_ui_mode_core.c"
)

target_include_directories(app_ui_mode_core_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME app_ui_mode_core_host_tests COMMAND app_ui_mode_core_host_tests)

add_executable(agent_animation_flow_core_host_tests
    "${MAIN_COMPONENT_DIR}/agent_animation_flow_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_agent_animation_flow_core.c"
)

target_include_directories(agent_animation_flow_core_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME agent_animation_flow_core_host_tests COMMAND agent_animation_flow_core_host_tests)

add_executable(launcher_home_model_host_tests
    "${MAIN_COMPONENT_DIR}/launcher_home_model.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_launcher_home_model.c"
)

target_include_directories(launcher_home_model_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME launcher_home_model_host_tests COMMAND launcher_home_model_host_tests)

add_executable(factory_settings_scroll_curve_host_tests
    "${MAIN_COMPONENT_DIR}/factory_settings_scroll_curve.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_factory_settings_scroll_curve.c"
)

target_include_directories(factory_settings_scroll_curve_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME factory_settings_scroll_curve_host_tests COMMAND factory_settings_scroll_curve_host_tests)

add_executable(factory_settings_perf_stats_host_tests
    "${MAIN_COMPONENT_DIR}/factory_settings_perf_stats.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_factory_settings_perf_stats.c"
)

target_include_directories(factory_settings_perf_stats_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME factory_settings_perf_stats_host_tests COMMAND factory_settings_perf_stats_host_tests)

add_executable(launcher_screen_cache_policy_host_tests
    "${MAIN_COMPONENT_DIR}/launcher_screen_cache_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_launcher_screen_cache_policy.c"
)

target_include_directories(launcher_screen_cache_policy_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME launcher_screen_cache_policy_host_tests COMMAND launcher_screen_cache_policy_host_tests)

add_executable(app_center_firmware_sources_host_tests
    "${MAIN_COMPONENT_DIR}/app_center_firmware_sources.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_app_center_firmware_sources.c"
)

target_include_directories(app_center_firmware_sources_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME app_center_firmware_sources_host_tests COMMAND app_center_firmware_sources_host_tests)

add_executable(app_center_exit_policy_host_tests
    "${MAIN_COMPONENT_DIR}/app_center_exit_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_app_center_exit_policy.c"
)

target_include_directories(app_center_exit_policy_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME app_center_exit_policy_host_tests COMMAND app_center_exit_policy_host_tests)

add_executable(app_center_manager_host_tests
    "${MAIN_COMPONENT_DIR}/app_center_manager.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_app_center_manager.c"
)

target_include_directories(app_center_manager_host_tests PRIVATE
    "${MAIN_COMPONENT_DIR}"
)

add_test(NAME app_center_manager_host_tests COMMAND app_center_manager_host_tests)

add_executable(factory_touch_parity_static_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_factory_touch_parity_static.c"
)

add_test(
    NAME factory_touch_parity_static_tests
    COMMAND factory_touch_parity_static_tests "${MAIN_COMPONENT_DIR}"
)

add_executable(resource_architecture_static_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_resource_architecture_static.c"
)

add_test(
    NAME resource_architecture_static_tests
    COMMAND resource_architecture_static_tests "${MAIN_COMPONENT_DIR}"
)

add_executable(voice_link_recovery_static_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_voice_link_recovery_static.c"
)

add_test(
    NAME voice_link_recovery_static_tests
    COMMAND voice_link_recovery_static_tests "${MAIN_COMPONENT_DIR}"
)

add_executable(provision_manager_adapter_static_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_provision_manager_adapter_static.c"
)

add_test(
    NAME provision_manager_adapter_static_tests
    COMMAND provision_manager_adapter_static_tests "${MAIN_COMPONENT_DIR}"
)
