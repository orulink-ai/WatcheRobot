# HAL Servo Component

Servo HAL for LEDC PWM direct drive on GPIO 19 (X-axis) and GPIO 20 (Y-axis).

The HAL keeps the existing installation-space angle contract used by the rest of
the firmware:

- public control angles stay in the `0-180°` range
- `90°` remains the installed neutral position
- the HAL internally maps logical `90°` to the MS90 neutral pulse of `1500us`

On startup, `hal_servo_init()` itself applies the default angles `X=90°` and
`Y=120°`. Some behavior states later move Y to other poses; that comes from the
behavior resources, not from the HAL defaults.

## Overview

This component provides the hardware abstraction layer for dual-axis servo
control on the WatcheRobot ESP32-S3 platform. It replaces the UART-to-MCU servo
bridge used in v1.x with direct LEDC PWM control and encapsulates the MS90
physical pulse model behind the existing firmware-facing logical angle model.

## Features

- **LEDC PWM Direct Drive**: 50Hz, 14-bit resolution PWM signals
- **Dual-Axis Control**: X-axis (pan, 0-180°) and Y-axis (tilt, configurable limits)
- **Smooth Movement**: Background FreeRTOS task with linear interpolation
- **Synchronized Motion**: Simultaneous dual-axis movement
- **Mechanical Protection**: Y-axis limits prevent hardware damage
- **Thread-Safe**: Mutex-protected angle access
- **Motion Cancel**: Can discard queued motions and abort the current smooth
  segment at the next interpolation step

## GPIO Mapping

| Axis | GPIO | LEDC Channel | Logical Range |
|------|------|--------------|---------------|
| X (pan) | 19 | LEDC_TIMER_0, CH0 | 0-180°, neutral at 90° |
| Y (tilt) | 20 | LEDC_TIMER_0, CH1 | 90-170° soft limit, neutral at 90° |

## API Reference

### Initialization

```c
esp_err_t hal_servo_init(void);
```

Initialize LEDC timer/channels, apply the default startup angles, and start the
smooth-move background task. Must be called before any other servo functions.

### Startup Position

Immediately after `hal_servo_init()`, the current logical angles are:

- `SERVO_AXIS_X`: `90°`
- `SERVO_AXIS_Y`: `120°`

Those values are also what the PWM outputs are configured to at boot, so
`hal_servo_get_angle()` returns the expected current position right after
initialization.

After startup, the behavior layer may still command poses such as `Y=95°`
through `states.json` or `spiffs/actions/*.json`. Those resources remain in the
same logical angle space and are not rewritten by the HAL.

### Immediate Movement

```c
esp_err_t hal_servo_set_angle(servo_axis_t axis, int angle_deg);
```

Set servo angle immediately without smoothing.

- `axis`: `SERVO_AXIS_X` or `SERVO_AXIS_Y`
- `angle_deg`: Target logical angle (0-180°, neutral at 90°)
- Returns: `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if angle out of range

### Smooth Movement

```c
esp_err_t hal_servo_move_smooth(servo_axis_t axis, int angle_deg, int duration_ms);
```

Move servo to angle with linear interpolation over specified duration.

- `axis`: `SERVO_AXIS_X` or `SERVO_AXIS_Y`
- `angle_deg`: Target logical angle (0-180°, neutral at 90°)
- `duration_ms`: Movement duration in milliseconds
- Returns: `ESP_OK` on success, `ESP_ERR_TIMEOUT` if queue is full

### Synchronized Movement

```c
esp_err_t hal_servo_move_sync(int x_deg, int y_deg, int duration_ms);
```

Move both axes simultaneously with coordinated timing.

- `x_deg`: Logical X target angle (0-180°, neutral at 90°)
- `y_deg`: Logical Y target angle (0-180°, neutral at 90°)
- `duration_ms`: Movement duration in milliseconds
- Returns: `ESP_OK` on success

### String-Based Command

```c
esp_err_t hal_servo_send_cmd(const char *id, int angle_deg, int duration_ms);
```

Convenience wrapper for WebSocket handlers. Maps "X"/"Y" strings to axis.

- `id`: Axis identifier ("X" or "Y", case-insensitive)
- `angle_deg`: Target logical angle
- `duration_ms`: Movement duration
- Returns: `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if id unknown

### Query Current Position

```c
int hal_servo_get_angle(servo_axis_t axis);
```

Get current logical servo angle.

- Returns: Current logical angle in degrees, or -1 if not initialized

### Cancel Queued Motion

```c
esp_err_t hal_servo_cancel_all(void);
```

Discard queued smooth-move commands and request the currently executing smooth
segment to stop on its next interpolation step. This is intended for behavior
state changes and external manual control handoff.

- Returns: `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if HAL not initialized

## Configuration (Kconfig)

| Option | Default | Description |
|--------|---------|-------------|
| `WATCHER_SERVO_X_GPIO` | 19 | X-axis PWM output GPIO |
| `WATCHER_SERVO_Y_GPIO` | 20 | Y-axis PWM output GPIO |
| `WATCHER_SERVO_Y_MIN_DEG` | 100 | Y-axis mechanical minimum |
| `WATCHER_SERVO_Y_MAX_DEG` | 140 | Y-axis mechanical maximum |
| `WATCHER_SERVO_SMOOTH_STEP_MS` | 10 | Interpolation step interval |

## PWM Timing

- **Frequency**: 50Hz (20ms period)
- **Resolution**: 14-bit (16384 levels)
- **MS90 Pulse Width**: 500us (logical 0°) to 2500us (logical 180°)
- **Neutral Pulse**: 1500us at logical 90°
- **Duty Cycle**: about 410 (500us) to 2048 (2500us)

## Logical vs Physical Angle

- The firmware-facing API keeps using installation-space logical angles
- Logical `90°` means "installed neutral pose"
- The HAL converts that pose to the MS90 physical neutral pulse of `1500us`
- Logical `0°` maps to `500us`
- Logical `180°` maps to `2500us`
- Logical `120°` maps to about `1833us`

## Usage Example

```c
#include "hal_servo.h"

void app_main(void)
{
    // Initialize servo HAL; startup defaults are X=90, Y=120
    ESP_ERROR_CHECK(hal_servo_init());

    // Smooth pan over 1 second
    hal_servo_move_smooth(SERVO_AXIS_X, 45, 1000);

    // Synchronized dual-axis move
    hal_servo_move_sync(90, 100, 500);

    // String-based command (for WebSocket handler)
    hal_servo_send_cmd("X", 135, 800);

    // Abort queued smooth movement before manual handoff
    hal_servo_cancel_all();

    // Query current position
    int x_angle = hal_servo_get_angle(SERVO_AXIS_X);
}
```

## Dependencies

- `driver` (ESP-IDF LEDC driver)
- `freertos` (FreeRTOS for task and queue management)

## Thread Safety

- All public APIs are thread-safe
- Internal angle state protected by mutex
- Commands processed sequentially via FreeRTOS queue

## Mechanical Limits

The Y-axis has mechanical limits (default 90-170°) to prevent hardware damage.
The HAL startup angle is 120°, and behavior states/actions later move the
Y-axis inside that same logical angle space. These limits are enforced
automatically:

- `hal_servo_set_angle()`: Clamps Y-axis to limits
- `hal_servo_move_smooth()`: Clamps Y-axis target
- `hal_servo_move_sync()`: Clamps Y-axis target

Adjust limits via Kconfig if your hardware has different constraints.
