#ifndef HAL_SERVO_H
#define HAL_SERVO_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define HAL_SERVO_AXIS_MASK_X 0x01u
#define HAL_SERVO_AXIS_MASK_Y 0x02u
#define HAL_SERVO_TRAJECTORY_MAX_FRAMES 64u

typedef enum {
    HAL_SERVO_MOTION_SOURCE_UNKNOWN = 0,
    HAL_SERVO_MOTION_SOURCE_BEHAVIOR = 1,
} hal_servo_motion_source_t;

typedef enum {
    HAL_SERVO_MOTION_PROFILE_LINEAR = 0,
    HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT = 1,
} hal_servo_motion_profile_t;

typedef struct {
    uint8_t axis_mask;
    int16_t x_deg_x10;
    int16_t y_deg_x10;
    uint16_t duration_ms;
    hal_servo_motion_profile_t motion_profile;
} hal_servo_trajectory_frame_t;

esp_err_t hal_servo_play_trajectory(const hal_servo_trajectory_frame_t *frames, size_t frame_count,
                                    hal_servo_motion_source_t source);

#endif /* HAL_SERVO_H */
