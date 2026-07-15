/**
 * @file hal_servo.h
 * @brief Servo compatibility facade backed by the STM32 coprocessor link.
 *
 * GPIO19/GPIO20 are no longer owned by a local LEDC PWM backend. They are
 * reserved for the runtime UART link to the STM32 coprocessor, and this HAL
 * now serves as a compatibility layer that forwards servo requests to
 * `mcu_motion_service`.
 */

#ifndef HAL_SERVO_H
#define HAL_SERVO_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_SERVO_AXIS_MASK_X 0x01u
#define HAL_SERVO_AXIS_MASK_Y 0x02u
#define HAL_SERVO_TRAJECTORY_MAX_FRAMES 64u

/** Servo axis selector */
typedef enum {
    SERVO_AXIS_X = 0, /*!< X axis (GPIO 19, left/right pan) */
    SERVO_AXIS_Y = 1, /*!< Y axis (GPIO 20, up/down tilt) */
} servo_axis_t;

/**
 * @brief Motion source tags kept for coprocessor motion attribution.
 */
typedef enum {
    HAL_SERVO_MOTION_SOURCE_UNKNOWN = 0,
    HAL_SERVO_MOTION_SOURCE_BEHAVIOR = 1,
    HAL_SERVO_MOTION_SOURCE_BLE = 2,
    HAL_SERVO_MOTION_SOURCE_WS = 3,
    HAL_SERVO_MOTION_SOURCE_RECOVERY = 4,
    HAL_SERVO_MOTION_SOURCE_RELAY_W1 = 5,
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

typedef struct {
    uint8_t axis_mask;
    uint16_t x_raw;
    int16_t x_angle_x10;
    uint16_t y_raw;
    int16_t y_angle_x10;
} hal_servo_feedback_t;

typedef void (*hal_servo_feedback_cb_t)(const hal_servo_feedback_t *feedback, void *ctx);

/**
 * @brief Initialize servo compatibility facade.
 *
 * The facade keeps local validation, source tagging, and last-command cache,
 * but no longer owns GPIO19/GPIO20 or a local PWM task. `hal_servo_get_angle()`
 * returns the last commanded target angle until STM32 feedback is integrated.
 *
 * @note Must be called before any hal_servo_set_angle() calls.
 * @note GPIO19/GPIO20 are reserved for `mcu_link` runtime UART.
 */
esp_err_t hal_servo_init(void);

/**
 * @brief Set servo angle with an immediate coprocessor command.
 *
 * @param axis    Servo axis (X or Y)
 * @param angle   Target logical angle in degrees (0–180, neutral at 90)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if angle out of range
 */
esp_err_t hal_servo_set_angle(servo_axis_t axis, int angle_deg);
esp_err_t hal_servo_set_direct_with_source_and_seq(servo_axis_t axis, int angle_deg, hal_servo_motion_source_t source,
                                                   uint32_t *out_seq);
esp_err_t hal_servo_set_direct_sync_with_source_and_seq(int x_deg, int y_deg, hal_servo_motion_source_t source,
                                                        uint32_t *out_seq);

/**
 * @brief Move servo to angle with smooth interpolation.
 *
 * Forwards a smooth move command to `mcu_motion_service`.
 *
 * @param axis        Servo axis (X or Y)
 * @param angle_deg   Target logical angle (0–180, neutral at 90)
 * @param duration_ms Movement duration in milliseconds
 * @return ESP_OK on success
 */
esp_err_t hal_servo_move_smooth(servo_axis_t axis, int angle_deg, int duration_ms);
esp_err_t hal_servo_move_smooth_with_source(servo_axis_t axis, int angle_deg, int duration_ms,
                                            hal_servo_motion_source_t source);
esp_err_t hal_servo_move_smooth_with_source_and_seq(servo_axis_t axis, int angle_deg, int duration_ms,
                                                    hal_servo_motion_source_t source, uint32_t *out_seq);

/**
 * @brief Move both axes simultaneously.
 *
 * Forwards a synchronized dual-axis command to `mcu_motion_service`.
 *
 * @param x_deg       Target logical X angle (0–180, neutral at 90)
 * @param y_deg       Target logical Y angle (0–180, neutral at 90)
 * @param duration_ms Movement duration in milliseconds
 * @return ESP_OK on success
 */
esp_err_t hal_servo_move_sync(int x_deg, int y_deg, int duration_ms);
esp_err_t hal_servo_move_sync_with_source(int x_deg, int y_deg, int duration_ms, hal_servo_motion_source_t source);
esp_err_t hal_servo_move_sync_with_source_and_seq(int x_deg, int y_deg, int duration_ms,
                                                  hal_servo_motion_source_t source, uint32_t *out_seq);
esp_err_t hal_servo_move_sync_with_profile_and_seq(int x_deg, int y_deg, int duration_ms,
                                                   hal_servo_motion_source_t source, hal_servo_motion_profile_t profile,
                                                   uint32_t *out_seq);

esp_err_t hal_servo_jog_with_source_and_seq(servo_axis_t axis, int velocity_deg_per_sec, int timeout_ms,
                                            hal_servo_motion_source_t source, uint32_t *out_seq);
esp_err_t hal_servo_jog_with_source(servo_axis_t axis, int velocity_deg_per_sec, int timeout_ms,
                                    hal_servo_motion_source_t source);
esp_err_t hal_servo_jog_vector_with_source_and_seq(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec,
                                                   int timeout_ms, hal_servo_motion_source_t source, uint32_t *out_seq);
esp_err_t hal_servo_jog_vector_with_source(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec, int timeout_ms,
                                           hal_servo_motion_source_t source);

/**
 * @brief Send servo command by axis name string (for WebSocket handler).
 *
 * Convenience wrapper for on_servo_handler: maps id string "X"/"Y" to
 * hal_servo_move_smooth() call.
 *
 * @param id          Axis identifier ("X" or "Y", case-insensitive)
 * @param angle_deg   Target logical angle (0–180, neutral at 90)
 * @param duration_ms Movement duration in milliseconds
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if id is unknown
 */
esp_err_t hal_servo_send_cmd(const char *id, int angle_deg, int duration_ms);

/**
 * @brief Cancel in-flight coprocessor servo motions.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if servo HAL is not ready
 */
esp_err_t hal_servo_cancel_all(void);
esp_err_t hal_servo_cancel_all_with_source(hal_servo_motion_source_t source);
esp_err_t hal_servo_pwm_unlock(uint8_t axis_mask);
esp_err_t hal_servo_pwm_lock(uint8_t axis_mask);
esp_err_t hal_servo_play_trajectory(const hal_servo_trajectory_frame_t *frames, size_t frame_count,
                                    hal_servo_motion_source_t source);
esp_err_t hal_servo_set_feedback_callback(hal_servo_feedback_cb_t cb, void *ctx);

/**
 * @brief Get the last commanded servo angle.
 *
 * @param axis Servo axis
 * @return Last commanded logical angle in degrees, or -1 if not initialized
 */
int hal_servo_get_angle(servo_axis_t axis);

#endif /* HAL_SERVO_H */
