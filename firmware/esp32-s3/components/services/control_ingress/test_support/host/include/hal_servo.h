#ifndef HAL_SERVO_H
#define HAL_SERVO_H

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    SERVO_AXIS_X = 0,
    SERVO_AXIS_Y = 1,
} servo_axis_t;

typedef enum {
    HAL_SERVO_MOTION_SOURCE_UNKNOWN = 0,
    HAL_SERVO_MOTION_SOURCE_BEHAVIOR = 1,
    HAL_SERVO_MOTION_SOURCE_BLE = 2,
    HAL_SERVO_MOTION_SOURCE_WS = 3,
    HAL_SERVO_MOTION_SOURCE_RECOVERY = 4,
} hal_servo_motion_source_t;

static inline esp_err_t hal_servo_move_sync_with_source_and_seq(int x_deg, int y_deg, int duration_ms,
                                                                hal_servo_motion_source_t source,
                                                                uint32_t *out_seq) {
    (void)x_deg;
    (void)y_deg;
    (void)duration_ms;
    (void)source;
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    return ESP_OK;
}

static inline esp_err_t hal_servo_move_smooth_with_source_and_seq(servo_axis_t axis, int angle_deg, int duration_ms,
                                                                  hal_servo_motion_source_t source,
                                                                  uint32_t *out_seq) {
    (void)axis;
    (void)angle_deg;
    (void)duration_ms;
    (void)source;
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    return ESP_OK;
}

static inline esp_err_t hal_servo_jog_with_source_and_seq(servo_axis_t axis, int velocity_deg_per_sec, int timeout_ms,
                                                          hal_servo_motion_source_t source, uint32_t *out_seq) {
    (void)axis;
    (void)velocity_deg_per_sec;
    (void)timeout_ms;
    (void)source;
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    return ESP_OK;
}

static inline esp_err_t hal_servo_jog_vector_with_source_and_seq(int x_velocity_deg_per_sec,
                                                                 int y_velocity_deg_per_sec, int timeout_ms,
                                                                 hal_servo_motion_source_t source,
                                                                 uint32_t *out_seq) {
    (void)x_velocity_deg_per_sec;
    (void)y_velocity_deg_per_sec;
    (void)timeout_ms;
    (void)source;
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    return ESP_OK;
}

static inline esp_err_t hal_servo_cancel_all_with_source(hal_servo_motion_source_t source) {
    (void)source;
    return ESP_OK;
}

#endif /* HAL_SERVO_H */
