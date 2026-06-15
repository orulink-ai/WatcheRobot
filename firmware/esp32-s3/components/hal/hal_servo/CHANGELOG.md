# Changelog

All notable changes to the hal_servo component will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Documented the startup angles used by `hal_servo_init()` and aligned the
  README/API docs with the current Y-axis soft-limit behavior.
- Switched the internal PWM mapping to the MS90 pulse model: `500..2500us`
  with `1500us` neutral while keeping public control angles in the existing
  `0..180` logical installation space (`90` remains neutral).
- Raised the documented Y-axis soft limit to `170°` and aligned the README with
  the current startup pose `X=90°`, `Y=120°`.
- Behavior state changes and external manual control now cancel queued servo
  motion so a new action does not have to wait for the previous loop to drain.

### Added

- Added `hal_servo_cancel_all()` to clear pending smooth-motion commands and
  abort the current interpolation segment on the next step boundary.
- Added `behavior_state_interrupt_action()` so BLE, WebSocket, and other manual
  control paths can stop the active action loop before taking over the servos.

## [2.0.0] - 2025-03-13

### Added

- **LEDC PWM Direct Drive**: Full implementation replacing UART-to-MCU bridge
  - 50Hz frequency, 14-bit resolution
  - GPIO 19 (X-axis) and GPIO 20 (Y-axis)
  - Duty cycle: 819 (0°) to 1638 (180°)

- **Smooth Movement**: Background FreeRTOS task with linear interpolation
  - Configurable step interval (default 10ms)
  - Command queue for non-blocking operation

- **Synchronized Motion**: `hal_servo_move_sync()` for coordinated dual-axis movement

- **Thread Safety**: Mutex-protected angle state for multi-threaded access

- **Mechanical Protection**: Automatic Y-axis clamping to configurable limits (100-140°)

- **Documentation**: README.md with full API reference and usage examples

### Changed

- **Breaking**: Replaced stub implementation with full LEDC PWM control
- **Breaking**: `hal_servo_init()` now creates FreeRTOS task and queue

### Removed

- Phase 1 stub code (ESP_OK returns without hardware control)

## [1.0.0] - 2025-01-XX

### Added

- Initial stub implementation for Phase 1
- Basic API definitions in header file
- Kconfig options for GPIO and limits
- Stub functions returning ESP_OK to allow boot without hardware
