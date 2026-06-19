cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_PROJECT_NAME watcheRobot_STM32)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

option(WATCHER_BUILD_FIRMWARE "Build the STM32 firmware target." ON)
option(WATCHER_BUILD_HOST_TESTS "Build the native host protocol tests." OFF)
option(WATCHER_STRESS_BUILD "Build STM32 firmware with the coprocessor stress simulator enabled." OFF)

if(WATCHER_STRESS_BUILD AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "WATCHER_STRESS_BUILD must stay OFF for Release firmware builds.")
endif()

set(WATCHER_ARM_MCU_FLAGS
    -mcpu=cortex-m3
    -mthumb
)

function(watcher_apply_common_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /wd4100)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            -Wno-missing-field-initializers
        )
    endif()
endfunction()

function(watcher_apply_arm_gcc_options target_name)
    if(CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(watcher_link_specs)
        if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-specs=nano\\.specs( |$)")
            list(APPEND watcher_link_specs -specs=nano.specs)
        endif()
        if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-specs=nosys\\.specs( |$)")
            list(APPEND watcher_link_specs -specs=nosys.specs)
        endif()

        target_compile_options(${target_name} PRIVATE
            ${WATCHER_ARM_MCU_FLAGS}
            $<$<CONFIG:Debug>:-Og>
            $<$<CONFIG:Release>:-Os>
            $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
            $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
            $<$<COMPILE_LANGUAGE:ASM>:-x>
            $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
        )

        get_target_property(watcher_target_type ${target_name} TYPE)
        if(NOT watcher_target_type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target_name} PRIVATE
                ${WATCHER_ARM_MCU_FLAGS}
                ${watcher_link_specs}
            )
        endif()
    endif()
endfunction()

set(PROTOCOL_SOURCES
    User/Protocol/coproc_protocol_types.c
    User/Protocol/coproc_ring_buffer.c
    User/Protocol/coproc_framing.c
    User/Protocol/coproc_cobs.c
    User/Protocol/coproc_crc16.c
    User/Protocol/coproc_frame_codec.c
    User/Protocol/coproc_tx_builder.c
    User/Protocol/coproc_dispatch.c
    User/Protocol/coproc_protocol.c
    User/Protocol/coproc_runtime.c
)

add_library(coproc_protocol_core STATIC
    ${PROTOCOL_SOURCES}
)

target_include_directories(coproc_protocol_core PUBLIC
    User/Config
    User/Protocol
)

if(WATCHER_STRESS_BUILD OR WATCHER_BUILD_HOST_TESTS)
    target_sources(coproc_protocol_core PRIVATE
        User/Protocol/coproc_stress_sim.c
    )
endif()

watcher_apply_common_warnings(coproc_protocol_core)
watcher_apply_arm_gcc_options(coproc_protocol_core)

if(WATCHER_BUILD_FIRMWARE)
    file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
        Core/Src/*.c
    )
    file(GLOB HAL_SOURCES CONFIGURE_DEPENDS
        Drivers/STM32F1xx_HAL_Driver/Src/*.c
    )

    add_executable(${CMAKE_PROJECT_NAME}
        ${CORE_SOURCES}
        ${HAL_SOURCES}
        startup_stm32f103xb.s
        User/App/app.c
        User/App/cli.c
        User/Device/bottom_ir/bottom_ir.c
        User/Device/servo/servo.c
        User/Device/bmi160/bmi160.c
        User/Device/qmc6309/qmc6309.c
        User/Device/ip5306/ip5306_irq.c
        User/Device/ip5306/ip5306_key.c
        User/Device/sensors/sensors.c
        User/Device/touch_sensor/touch_sensor.c
        User/Device/ws2812/ws2812.c
        User/Platform/stm32/platform_coproc_uart.c
        User/Platform/stm32/platform_uart.c
        User/Platform/stm32/platform_i2c.c
        User/Platform/stm32/platform_time.c
        User/Platform/stm32/platform_pwm.c
    )

    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        Core/Inc
        Drivers/CMSIS/Include
        Drivers/CMSIS/Device/ST/STM32F1xx/Include
        Drivers/STM32F1xx_HAL_Driver/Inc
        Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
        User/App
        User/Board
        User/Config
        User/Device
        User/Device/bottom_ir
        User/Device/bmi160
        User/Device/qmc6309
        User/Device/ip5306
        User/Device/sensors
        User/Device/servo
        User/Device/touch_sensor
        User/Device/ws2812
        User/Platform/stm32
        User/Protocol
    )

    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        USE_HAL_DRIVER
        STM32F103xB
    )
    if(WATCHER_STRESS_BUILD)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE WATCHER_STRESS_BUILD=1)
    endif()

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE coproc_protocol_core)
    watcher_apply_common_warnings(${CMAKE_PROJECT_NAME})
    watcher_apply_arm_gcc_options(${CMAKE_PROJECT_NAME})

    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -T${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH.ld
        -Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map,--cref
        -Wl,--gc-sections
        -Wl,--print-memory-usage
    )

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    )
endif()

if(WATCHER_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(coproc_host_tests
        User/TestHost/test_main.c
    )

    target_link_libraries(coproc_host_tests PRIVATE coproc_protocol_core)
    target_include_directories(coproc_host_tests PRIVATE User/Protocol)
    watcher_apply_common_warnings(coproc_host_tests)

    add_test(NAME coproc_host_tests COMMAND coproc_host_tests)

    add_executable(cli_host_tests
        User/TestHost/cli_host_test.c
        User/App/cli.c
    )

    target_include_directories(cli_host_tests PRIVATE
        User/TestHost/stubs
        User/App
        User/Config
        User/Device/bmi160
        User/Device/qmc6309
        User/Device/ip5306
        User/Device/sensors
        User/Device/servo
        User/Device/touch_sensor
        User/Platform/stm32
    )
    watcher_apply_common_warnings(cli_host_tests)

    add_test(NAME cli_host_tests COMMAND cli_host_tests)

    add_executable(servo_motion_tests
        User/TestHost/servo_motion_test.c
        User/Device/servo/servo.c
    )

    target_include_directories(servo_motion_tests PRIVATE
        User/TestHost/servo_driver_stubs
        User/Device/servo
    )
    watcher_apply_common_warnings(servo_motion_tests)

    add_test(NAME servo_motion_tests COMMAND servo_motion_tests)
endif()
