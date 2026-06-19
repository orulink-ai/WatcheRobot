cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "V2.4.1")

# Extra component directories (nested structure)
# ESP-IDF only scans components/ directly, so we need to specify subdirectories
set(EXTRA_COMPONENT_DIRS
    # Drivers
    "components/drivers/bsp_watcher"
    # HAL
    "components/hal/hal_audio"
    "components/hal/hal_button"
    "components/hal/hal_camera"
    "components/hal/hal_display"
    "components/hal/hal_servo"
    # Protocols
    "components/protocols/ble_service"
    "components/protocols/discovery"
    "components/protocols/mcu_link"
    "components/protocols/server_pairing"
    "components/protocols/ws_client"
    # Services
    "components/services/anim_service"
    "components/services/behavior_state_service"
    "components/services/camera_service"
    "components/services/control_ingress"
    "components/services/mcu_led_service"
    "components/services/mcu_motion_service"
    "components/services/mcu_power_service"
    "components/services/mcu_sensor_service"
    "components/services/ota_service"
    "components/services/power_monitor_service"
    "components/services/sfx_service"
    "components/services/voice_service"
    # Utils
    "components/utils/boot_anim"
    "components/utils/wifi_manager"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(CMAKE_BINARY_DIR MATCHES "build-s3-c-stress")
    add_compile_definitions(WATCHER_STRESS_BUILD=1)
endif()
project(WatcheRobot-S3)
# Keep custom generators on the same interpreter as the active ESP-IDF build.
idf_build_get_property(project_python PYTHON)

if(DEFINED CONFIG_WATCHER_ANIM_FPS)
    set(ANIM_DEFAULT_FPS ${CONFIG_WATCHER_ANIM_FPS})
else()
    set(ANIM_DEFAULT_FPS 10)
endif()

set(ANIM_GIF_DIR ${CMAKE_SOURCE_DIR}/assets/gif)
set(ANIM_LEGACY_DIR ${CMAKE_SOURCE_DIR}/assets/watcher0327png)
set(ANIM_IMPORT_DIR ${ANIM_LEGACY_DIR})
if(EXISTS "${ANIM_GIF_DIR}")
    file(GLOB_RECURSE ANIM_GIF_SOURCES CONFIGURE_DEPENDS "${ANIM_GIF_DIR}/*.gif")
    if(ANIM_GIF_SOURCES)
        set(ANIM_IMPORT_DIR ${ANIM_GIF_DIR})
    endif()
endif()

set(ANIM_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/release/${PROJECT_VER}/sdcard/anim)
set(ANIM_GENERATOR_ARGS
    --output-dir ${ANIM_OUTPUT_DIR}
    --input-dir ${ANIM_IMPORT_DIR}
    --fps ${ANIM_DEFAULT_FPS}
)

if(DEFINED CONFIG_LV_COLOR_16_SWAP AND CONFIG_LV_COLOR_16_SWAP)
    list(APPEND ANIM_GENERATOR_ARGS --lv-color-16-swap)
endif()

add_custom_target(anim_assets_gen
    COMMAND ${project_python} ${CMAKE_SOURCE_DIR}/tools/generate_anim_assets.py
            ${ANIM_GENERATOR_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    BYPRODUCTS
        ${ANIM_OUTPUT_DIR}/anim_manifest.bin
    COMMENT "Generating SD-card animpack assets and manifest")

# Create SPIFFS partition image from spiffs/ directory
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT DEPENDS anim_assets_gen)
