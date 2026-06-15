/**
 * @file hal_servo.c
 * @brief Servo compatibility facade that routes motions to the STM32 coprocessor.
 */

#include "hal_servo.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mcu_motion_service.h"

#include <ctype.h>
#include <stdint.h>

#define TAG "HAL_SERVO"

#define SERVO_X_DEFAULT_DEG 90
#define SERVO_Y_DEFAULT_DEG 120
#define SERVO_IMMEDIATE_DURATION_MS 1

static bool s_initialized = false;
static SemaphoreHandle_t s_angle_mutex = NULL;
static int s_angle[2] = {SERVO_X_DEFAULT_DEG, SERVO_Y_DEFAULT_DEG};

static hal_servo_motion_profile_t servo_select_motion_profile(hal_servo_motion_source_t source, int duration_ms) {
    if (source == HAL_SERVO_MOTION_SOURCE_BEHAVIOR && duration_ms > SERVO_IMMEDIATE_DURATION_MS) {
        return HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT;
    }

    return HAL_SERVO_MOTION_PROFILE_LINEAR;
}

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
static mcu_motion_source_t servo_source_to_mcu(hal_servo_motion_source_t source) {
    switch (source) {
    case HAL_SERVO_MOTION_SOURCE_BEHAVIOR:
        return MCU_MOTION_SOURCE_BEHAVIOR;
    case HAL_SERVO_MOTION_SOURCE_BLE:
        return MCU_MOTION_SOURCE_BLE;
    case HAL_SERVO_MOTION_SOURCE_WS:
        return MCU_MOTION_SOURCE_WS;
    case HAL_SERVO_MOTION_SOURCE_RECOVERY:
        return MCU_MOTION_SOURCE_RECOVERY;
    case HAL_SERVO_MOTION_SOURCE_UNKNOWN:
    default:
        return MCU_MOTION_SOURCE_UNKNOWN;
    }
}

static uint8_t servo_profile_to_mcu(hal_servo_motion_profile_t profile) {
    switch (profile) {
    case HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT:
        return MCU_MOTION_PROFILE_EASE_IN_OUT;
    case HAL_SERVO_MOTION_PROFILE_LINEAR:
    default:
        return MCU_MOTION_PROFILE_LINEAR;
    }
}
#endif

static int servo_clamp_angle(servo_axis_t axis, int angle_deg) {
    if (angle_deg < 0) {
        angle_deg = 0;
    }
    if (angle_deg > 180) {
        angle_deg = 180;
    }

    if (axis == SERVO_AXIS_Y) {
        if (angle_deg < CONFIG_WATCHER_SERVO_Y_MIN_DEG) {
            angle_deg = CONFIG_WATCHER_SERVO_Y_MIN_DEG;
        }
        if (angle_deg > CONFIG_WATCHER_SERVO_Y_MAX_DEG) {
            angle_deg = CONFIG_WATCHER_SERVO_Y_MAX_DEG;
        }
    }

    return angle_deg;
}

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
static esp_err_t servo_build_motion_request(uint8_t axis_mask, int x_deg, int y_deg, int duration_ms,
                                            hal_servo_motion_source_t source, hal_servo_motion_profile_t profile,
                                            mcu_motion_request_t *out_request) {
    if (out_request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (axis_mask == 0U || (axis_mask & ~(MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y)) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (x_deg < 0 || x_deg > 180 || y_deg < 0 || y_deg > 180) {
        return ESP_ERR_INVALID_ARG;
    }

    out_request->axis_mask = axis_mask;
    out_request->x_deg_x10 = (axis_mask & MCU_MOTION_AXIS_X) != 0U ? (int16_t)(x_deg * 10) : 0;
    out_request->y_deg_x10 = (axis_mask & MCU_MOTION_AXIS_Y) != 0U ? (int16_t)(y_deg * 10) : 0;
    out_request->duration_ms = (uint16_t)((duration_ms <= 0) ? SERVO_IMMEDIATE_DURATION_MS
                                                             : (duration_ms > UINT16_MAX ? UINT16_MAX : duration_ms));
    out_request->motion_profile = servo_profile_to_mcu(profile);
    out_request->source = servo_source_to_mcu(source);
    return ESP_OK;
}
#endif

static void servo_cache_angle(servo_axis_t axis, int angle_deg) {
    if (s_angle_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_angle_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_angle[axis] = angle_deg;
        xSemaphoreGive(s_angle_mutex);
    }
}

static void servo_cache_sync_angles(int x_deg, int y_deg) {
    if (s_angle_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_angle_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_angle[SERVO_AXIS_X] = x_deg;
        s_angle[SERVO_AXIS_Y] = y_deg;
        xSemaphoreGive(s_angle_mutex);
    }
}

esp_err_t hal_servo_init(void) {
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    esp_err_t ret;
#endif

    if (s_initialized) {
        return ESP_OK;
    }

    s_angle_mutex = xSemaphoreCreateMutex();
    if (s_angle_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create angle mutex");
        return ESP_FAIL;
    }

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    ret = mcu_motion_service_init();
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_angle_mutex);
        s_angle_mutex = NULL;
        ESP_LOGE(TAG, "MCU motion service init failed: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

    s_initialized = true;
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    ESP_LOGI(TAG, "Servo HAL initialized in coprocessor facade mode; GPIO19/GPIO20 are reserved for mcu_link UART");
#else
    ESP_LOGI(TAG, "Servo HAL initialized with motion output disabled");
#endif
    return ESP_OK;
}

esp_err_t hal_servo_set_angle(servo_axis_t axis, int angle_deg) {
    return hal_servo_move_smooth_with_source(axis, angle_deg, SERVO_IMMEDIATE_DURATION_MS,
                                             HAL_SERVO_MOTION_SOURCE_UNKNOWN);
}

esp_err_t hal_servo_move_smooth(servo_axis_t axis, int angle_deg, int duration_ms) {
    return hal_servo_move_smooth_with_source(axis, angle_deg, duration_ms, HAL_SERVO_MOTION_SOURCE_UNKNOWN);
}

esp_err_t hal_servo_move_smooth_with_source(servo_axis_t axis, int angle_deg, int duration_ms,
                                            hal_servo_motion_source_t source) {
    return hal_servo_move_smooth_with_source_and_seq(axis, angle_deg, duration_ms, source, NULL);
}

esp_err_t hal_servo_move_smooth_with_source_and_seq(servo_axis_t axis, int angle_deg, int duration_ms,
                                                    hal_servo_motion_source_t source, uint32_t *out_seq) {
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    mcu_motion_request_t request;
    esp_err_t ret;
#endif
    int clamped_angle;

    (void)duration_ms;
    (void)source;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (axis != SERVO_AXIS_X && axis != SERVO_AXIS_Y) {
        return ESP_ERR_INVALID_ARG;
    }

    if (angle_deg < 0 || angle_deg > 180) {
        return ESP_ERR_INVALID_ARG;
    }

    clamped_angle = servo_clamp_angle(axis, angle_deg);
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    ret = servo_build_motion_request(axis == SERVO_AXIS_X ? MCU_MOTION_AXIS_X : MCU_MOTION_AXIS_Y,
                                     axis == SERVO_AXIS_X ? clamped_angle : 0, axis == SERVO_AXIS_Y ? clamped_angle : 0,
                                     duration_ms, source, servo_select_motion_profile(source, duration_ms), &request);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mcu_motion_submit_with_seq(&request, out_seq);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to submit servo motion: axis=%s angle=%d duration_ms=%d err=%s",
                 axis == SERVO_AXIS_X ? "X" : "Y", clamped_angle, duration_ms, esp_err_to_name(ret));
        return ret;
    }

    servo_cache_angle(axis, clamped_angle);
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued servo motion via coprocessor: axis=%s angle=%d duration_ms=%u profile=%u source=%d",
             axis == SERVO_AXIS_X ? "X" : "Y", clamped_angle, (unsigned)request.duration_ms,
             (unsigned)request.motion_profile, (int)source);
#endif
#else
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    servo_cache_angle(axis, clamped_angle);
    ESP_LOGD(TAG, "Servo motion disabled; cached axis=%s angle=%d", axis == SERVO_AXIS_X ? "X" : "Y", clamped_angle);
#endif
    return ESP_OK;
}

esp_err_t hal_servo_move_sync(int x_deg, int y_deg, int duration_ms) {
    return hal_servo_move_sync_with_source(x_deg, y_deg, duration_ms, HAL_SERVO_MOTION_SOURCE_UNKNOWN);
}

esp_err_t hal_servo_move_sync_with_source(int x_deg, int y_deg, int duration_ms, hal_servo_motion_source_t source) {
    return hal_servo_move_sync_with_source_and_seq(x_deg, y_deg, duration_ms, source, NULL);
}

esp_err_t hal_servo_move_sync_with_source_and_seq(int x_deg, int y_deg, int duration_ms,
                                                  hal_servo_motion_source_t source, uint32_t *out_seq) {
    return hal_servo_move_sync_with_profile_and_seq(x_deg, y_deg, duration_ms, source,
                                                    servo_select_motion_profile(source, duration_ms), out_seq);
}

esp_err_t hal_servo_move_sync_with_profile_and_seq(int x_deg, int y_deg, int duration_ms,
                                                   hal_servo_motion_source_t source,
                                                   hal_servo_motion_profile_t profile, uint32_t *out_seq) {
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    mcu_motion_request_t request;
    esp_err_t ret;
#endif
    int clamped_x;
    int clamped_y;

    (void)duration_ms;
    (void)source;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x_deg < 0 || x_deg > 180 || y_deg < 0 || y_deg > 180) {
        return ESP_ERR_INVALID_ARG;
    }

    clamped_x = servo_clamp_angle(SERVO_AXIS_X, x_deg);
    clamped_y = servo_clamp_angle(SERVO_AXIS_Y, y_deg);

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    ret = servo_build_motion_request(MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y, clamped_x, clamped_y, duration_ms, source,
                                     profile, &request);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mcu_motion_submit_with_seq(&request, out_seq);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to submit sync servo motion: x=%d y=%d duration_ms=%d err=%s", clamped_x, clamped_y,
                 duration_ms, esp_err_to_name(ret));
        return ret;
    }

    servo_cache_sync_angles(clamped_x, clamped_y);
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued sync servo motion via coprocessor: x=%d y=%d duration_ms=%u profile=%u source=%d",
             clamped_x, clamped_y, (unsigned)request.duration_ms, (unsigned)request.motion_profile, (int)source);
#endif
#else
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    servo_cache_sync_angles(clamped_x, clamped_y);
    ESP_LOGD(TAG, "Servo motion disabled; cached x=%d y=%d", clamped_x, clamped_y);
#endif
    return ESP_OK;
}

esp_err_t hal_servo_jog_with_source(servo_axis_t axis, int velocity_deg_per_sec, int timeout_ms,
                                    hal_servo_motion_source_t source) {
    return hal_servo_jog_with_source_and_seq(axis, velocity_deg_per_sec, timeout_ms, source, NULL);
}

esp_err_t hal_servo_jog_with_source_and_seq(servo_axis_t axis, int velocity_deg_per_sec, int timeout_ms,
                                            hal_servo_motion_source_t source, uint32_t *out_seq) {
    if (axis != SERVO_AXIS_X && axis != SERVO_AXIS_Y) {
        return ESP_ERR_INVALID_ARG;
    }
    return hal_servo_jog_vector_with_source_and_seq(axis == SERVO_AXIS_X ? velocity_deg_per_sec : 0,
                                                    axis == SERVO_AXIS_Y ? velocity_deg_per_sec : 0, timeout_ms, source,
                                                    out_seq);
}

esp_err_t hal_servo_jog_vector_with_source(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec, int timeout_ms,
                                           hal_servo_motion_source_t source) {
    return hal_servo_jog_vector_with_source_and_seq(x_velocity_deg_per_sec, y_velocity_deg_per_sec, timeout_ms, source,
                                                    NULL);
}

esp_err_t hal_servo_jog_vector_with_source_and_seq(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec,
                                                   int timeout_ms, hal_servo_motion_source_t source,
                                                   uint32_t *out_seq) {
#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    mcu_motion_jog_request_t request = {0};
    esp_err_t ret;
#endif

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((x_velocity_deg_per_sec == 0 && y_velocity_deg_per_sec == 0) || x_velocity_deg_per_sec < -180 ||
        x_velocity_deg_per_sec > 180 || y_velocity_deg_per_sec < -180 || y_velocity_deg_per_sec > 180 ||
        timeout_ms <= 0 ||
        timeout_ms > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    request.axis_mask = 0;
    if (x_velocity_deg_per_sec != 0) {
        request.axis_mask |= MCU_MOTION_AXIS_X;
    }
    if (y_velocity_deg_per_sec != 0) {
        request.axis_mask |= MCU_MOTION_AXIS_Y;
    }
    request.x_velocity_deg_x10_per_sec = (int16_t)(x_velocity_deg_per_sec * 10);
    request.y_velocity_deg_x10_per_sec = (int16_t)(y_velocity_deg_per_sec * 10);
    request.timeout_ms = (uint16_t)timeout_ms;
    request.source = servo_source_to_mcu(source);
    request.x_min_deg = 0;
    request.x_max_deg = 180;
    request.y_min_deg = CONFIG_WATCHER_SERVO_Y_MIN_DEG;
    request.y_max_deg = CONFIG_WATCHER_SERVO_Y_MAX_DEG;

    ret = mcu_motion_jog_with_seq(&request, out_seq);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to submit servo jog: x_velocity=%d y_velocity=%d timeout_ms=%d err=%s",
                 x_velocity_deg_per_sec, y_velocity_deg_per_sec, timeout_ms, esp_err_to_name(ret));
        return ret;
    }
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued servo jog via coprocessor: x_velocity=%d y_velocity=%d timeout_ms=%d source=%d",
             x_velocity_deg_per_sec, y_velocity_deg_per_sec, timeout_ms, (int)source);
#endif
#else
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    (void)source;
#endif
    return ESP_OK;
}

esp_err_t hal_servo_send_cmd(const char *id, int angle_deg, int duration_ms) {
    servo_axis_t axis;
    char upper;

    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    upper = (char)toupper((unsigned char)id[0]);
    if (upper == 'X') {
        axis = SERVO_AXIS_X;
    } else if (upper == 'Y') {
        axis = SERVO_AXIS_Y;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    return hal_servo_move_smooth(axis, angle_deg, duration_ms);
}

esp_err_t hal_servo_cancel_all(void) {
    return hal_servo_cancel_all_with_source(HAL_SERVO_MOTION_SOURCE_UNKNOWN);
}

esp_err_t hal_servo_cancel_all_with_source(hal_servo_motion_source_t source) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_WATCHER_SERVO_MOTION_ENABLE
    return mcu_motion_stop(servo_source_to_mcu(source));
#else
    (void)source;
    return ESP_OK;
#endif
}

int hal_servo_get_angle(servo_axis_t axis) {
    int angle = -1;

    if (!s_initialized) {
        return -1;
    }

    if (axis != SERVO_AXIS_X && axis != SERVO_AXIS_Y) {
        return -1;
    }

    if (s_angle_mutex != NULL && xSemaphoreTake(s_angle_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        angle = s_angle[axis];
        xSemaphoreGive(s_angle_mutex);
    }

    return angle;
}
