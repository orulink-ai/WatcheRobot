cmake_minimum_required(VERSION 3.20)

project(animation_controller_core_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(ANIM_SERVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(EXISTS "${ANIM_SERVICE_DIR}/src/animation_controller_core.c")
    add_executable(animation_controller_core_host_tests
        "${ANIM_SERVICE_DIR}/src/animation_controller_core.c"
        "${ANIM_SERVICE_DIR}/src/animation_playback_policy.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/test_animation_controller_core.c"
    )

    target_include_directories(animation_controller_core_host_tests PRIVATE
        "${ANIM_SERVICE_DIR}/include"
        "${ANIM_SERVICE_DIR}/src"
    )

    if(MSVC)
        target_compile_options(animation_controller_core_host_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(animation_controller_core_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()

    add_test(NAME animation_controller_core_host_tests COMMAND animation_controller_core_host_tests)
endif()

add_executable(anim_player_lifecycle_host_tests
    "${ANIM_SERVICE_DIR}/src/anim_player_event_queue.c"
    "${ANIM_SERVICE_DIR}/src/anim_player_lifecycle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_anim_player_lifecycle.c"
)

target_include_directories(anim_player_lifecycle_host_tests PRIVATE
    "${ANIM_SERVICE_DIR}/include"
    "${ANIM_SERVICE_DIR}/src"
)

if(MSVC)
    target_compile_options(anim_player_lifecycle_host_tests PRIVATE /W4 /WX)
else()
    target_compile_options(anim_player_lifecycle_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME anim_player_lifecycle_host_tests COMMAND anim_player_lifecycle_host_tests)

add_executable(animation_service_sync_adapter_host_tests
    "${ANIM_SERVICE_DIR}/src/animation_controller_core.c"
    "${ANIM_SERVICE_DIR}/src/animation_playback_policy.c"
    "${ANIM_SERVICE_DIR}/src/animation_service_sync_adapter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_animation_service_sync_adapter.c"
)

target_include_directories(animation_service_sync_adapter_host_tests PRIVATE
    "${ANIM_SERVICE_DIR}/include"
    "${ANIM_SERVICE_DIR}/src"
)

if(MSVC)
    target_compile_options(animation_service_sync_adapter_host_tests PRIVATE /W4 /WX)
else()
    target_compile_options(animation_service_sync_adapter_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME animation_service_sync_adapter_host_tests COMMAND animation_service_sync_adapter_host_tests)

add_executable(animation_playback_policy_host_tests
    "${ANIM_SERVICE_DIR}/src/animation_playback_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_animation_playback_policy.c"
)

target_include_directories(animation_playback_policy_host_tests PRIVATE
    "${ANIM_SERVICE_DIR}/include"
    "${ANIM_SERVICE_DIR}/src"
)

if(MSVC)
    target_compile_options(animation_playback_policy_host_tests PRIVATE /W4 /WX)
else()
    target_compile_options(animation_playback_policy_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME animation_playback_policy_host_tests COMMAND animation_playback_policy_host_tests)

add_executable(anim_data_plane_host_tests
    "${ANIM_SERVICE_DIR}/src/anim_fault_injection.c"
    "${ANIM_SERVICE_DIR}/src/anim_frame_pool_core.c"
    "${ANIM_SERVICE_DIR}/src/anim_prepare_watchdog.c"
    "${ANIM_SERVICE_DIR}/src/anim_rgb565_fade.c"
    "${ANIM_SERVICE_DIR}/src/anim_worker_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_anim_data_plane.c"
)

target_include_directories(anim_data_plane_host_tests PRIVATE
    "${ANIM_SERVICE_DIR}/src"
)

file(REAL_PATH "${ANIM_SERVICE_DIR}/src/anim_player.c" ANIM_PLAYER_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/src/anim_storage.c" ANIM_STORAGE_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/src/animation_service_runtime.c" ANIM_RUNTIME_SOURCE)
get_filename_component(LEGACY_ANIM_PLAYER_HEADER "${ANIM_SERVICE_DIR}/include/anim_player.h" ABSOLUTE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/Kconfig" ANIM_KCONFIG_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/../../../main/app_main.c" APP_MAIN_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/../voice_service/src/voice_fsm.c" VOICE_FSM_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/../behavior_state_service/src/behavior_state_service.c" BEHAVIOR_SOURCE)
file(REAL_PATH "${ANIM_SERVICE_DIR}/../../hal/hal_display/src/hal_display.c" HAL_DISPLAY_SOURCE)

target_compile_definitions(anim_data_plane_host_tests PRIVATE
    ANIM_SERVICE_ENABLE_TEST_HOOKS=1
    ANIM_PLAYER_SOURCE="${ANIM_PLAYER_SOURCE}"
    ANIM_STORAGE_SOURCE="${ANIM_STORAGE_SOURCE}"
    ANIM_RUNTIME_SOURCE="${ANIM_RUNTIME_SOURCE}"
    LEGACY_ANIM_PLAYER_HEADER="${LEGACY_ANIM_PLAYER_HEADER}"
    ANIM_KCONFIG_SOURCE="${ANIM_KCONFIG_SOURCE}"
    APP_MAIN_SOURCE="${APP_MAIN_SOURCE}"
    VOICE_FSM_SOURCE="${VOICE_FSM_SOURCE}"
    BEHAVIOR_SOURCE="${BEHAVIOR_SOURCE}"
    HAL_DISPLAY_SOURCE="${HAL_DISPLAY_SOURCE}"
)

if(MSVC)
    target_compile_options(anim_data_plane_host_tests PRIVATE /W4 /WX)
else()
    target_compile_options(anim_data_plane_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME anim_data_plane_host_tests COMMAND anim_data_plane_host_tests)
