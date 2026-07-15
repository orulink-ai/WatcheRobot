/**
 * @file ws_handlers.c
 * @brief WebSocket message handlers implementation (Watcher protocol v0.1.5)
 */

#include "ws_handlers.h"

#include "behavior_state_service.h"
#include "camera_service.h"
#include "control_ingress.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_servo.h"
#include "mcu_led_service.h"
#include "sfx_service.h"
#include "voice_service.h"
#include "ws_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TAG "WS_HANDLERS"

#define SERVO_POSITION_REPORT_ENABLED 0
#define SERVO_REPORT_INTERVAL_MS 50
#define SERVO_REPORT_TASK_STACK 4096
#define SERVO_REPORT_TASK_PRIORITY 4
#define WS_CAPTURE_DEFAULT_FPS 5
#define WS_CAPTURE_MAX_FPS 10
#define WS_CAPTURE_DEFAULT_QUALITY 80
#define WS_CAMERA_UPLOAD_TASK_STACK 6144
#define WS_CAMERA_UPLOAD_TASK_PRIORITY 5
#define WS_CAMERA_UPLOAD_TASK_EXIT_WAIT_MS 1000
#define WS_DEVICE_ERROR_CODE_CAMERA 1502
#define WS_DEVICE_ERROR_CODE_SERVO 1503
#define WS_DEVICE_ERROR_CODE_OTA 1504
#define SERVO_FEEDBACK_RAW_DELTA_MIN 70U
#define SERVO_FEEDBACK_ANGLE_X10_DELTA_MIN 25
#define SERVO_PWM_RETRY_COUNT 5
#define SERVO_PWM_RETRY_DELAY_MS 40
#define SERVO_TRAJECTORY_MIN_DURATION_MS 30
#define SERVO_TRAJECTORY_MAX_DURATION_MS 3000
#define SERVO_TRAJECTORY_X_MIN_DEG 0.0f
#define SERVO_TRAJECTORY_X_MAX_DEG 180.0f
#define SERVO_TRAJECTORY_Y_MIN_DEG 90.0f
#define SERVO_TRAJECTORY_Y_MAX_DEG 150.0f

typedef struct {
    SemaphoreHandle_t lock;
    SemaphoreHandle_t frame_ready_sem;
    bool callback_registered;
    bool streaming;
    bool transfer_active;
    bool shutdown_requested;
    bool video_first_frame_pending;
    int target_fps;
    int configured_width;
    int configured_height;
    int configured_quality;
    uint32_t frames_sent;
    uint64_t send_total_us;
    uint64_t send_lock_wait_total_us;
    uint64_t send_sock_total_us;
    uint32_t frames_dropped;
    uint8_t *pending_jpeg;
    size_t pending_size;
    uint32_t pending_timestamp_ms;
    TaskHandle_t upload_task;
} ws_camera_context_t;

typedef struct {
    bool initialized;
    uint16_t raw;
    int16_t angle_x10;
} ws_servo_feedback_report_state_t;

static ws_camera_context_t s_camera_ctx = {
    .lock = NULL,
    .frame_ready_sem = NULL,
    .callback_registered = false,
    .streaming = false,
    .transfer_active = false,
    .shutdown_requested = false,
    .video_first_frame_pending = false,
    .target_fps = 0,
    .configured_width = 0,
    .configured_height = 0,
    .configured_quality = WS_CAPTURE_DEFAULT_QUALITY,
    .frames_sent = 0,
    .send_total_us = 0,
    .send_lock_wait_total_us = 0,
    .send_sock_total_us = 0,
    .frames_dropped = 0,
    .pending_jpeg = NULL,
    .pending_size = 0,
    .pending_timestamp_ms = 0,
    .upload_task = NULL,
};
static ws_servo_feedback_report_state_t s_servo_feedback_x_state = {0};
static ws_servo_feedback_report_state_t s_servo_feedback_y_state = {0};

#if SERVO_POSITION_REPORT_ENABLED
static TaskHandle_t s_servo_report_task = NULL;
#endif
static ws_app_package_handler_t s_app_package_handler = {0};

static esp_err_t ws_camera_ensure_lock(void) {
    if (s_camera_ctx.lock == NULL) {
        s_camera_ctx.lock = xSemaphoreCreateMutex();
        if (s_camera_ctx.lock == NULL) {
            ESP_LOGE(TAG, "camera ws lock create failed");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_camera_ctx.frame_ready_sem == NULL) {
        s_camera_ctx.frame_ready_sem = xSemaphoreCreateBinary();
        if (s_camera_ctx.frame_ready_sem == NULL) {
            ESP_LOGE(TAG, "camera ws frame semaphore create failed");
            vSemaphoreDelete(s_camera_ctx.lock);
            s_camera_ctx.lock = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static void ws_camera_drain_frame_signal(void) {
    if (s_camera_ctx.frame_ready_sem == NULL) {
        return;
    }
    while (xSemaphoreTake(s_camera_ctx.frame_ready_sem, 0) == pdTRUE) {
    }
}

static void ws_camera_release_pending_locked(void) {
    if (s_camera_ctx.pending_jpeg != NULL) {
        free(s_camera_ctx.pending_jpeg);
        s_camera_ctx.pending_jpeg = NULL;
    }
    s_camera_ctx.pending_size = 0;
    s_camera_ctx.pending_timestamp_ms = 0;
}

static void ws_camera_reset_stats_locked(void) {
    s_camera_ctx.frames_sent = 0;
    s_camera_ctx.send_total_us = 0;
    s_camera_ctx.send_lock_wait_total_us = 0;
    s_camera_ctx.send_sock_total_us = 0;
    s_camera_ctx.frames_dropped = 0;
}

static esp_err_t ws_camera_begin_transfer(bool streaming, int fps) {
    ESP_RETURN_ON_ERROR(ws_camera_ensure_lock(), TAG, "camera ws lock init failed");

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    s_camera_ctx.streaming = streaming;
    s_camera_ctx.transfer_active = true;
    s_camera_ctx.video_first_frame_pending = streaming;
    s_camera_ctx.target_fps = streaming ? fps : 0;
    ws_camera_reset_stats_locked();
    ws_camera_release_pending_locked();
    ws_camera_drain_frame_signal();
    xSemaphoreGive(s_camera_ctx.lock);
    return ESP_OK;
}

static void ws_camera_finish_one_shot(void) {
    if (ws_camera_ensure_lock() != ESP_OK) {
        return;
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        s_camera_ctx.streaming = false;
        s_camera_ctx.transfer_active = false;
        s_camera_ctx.video_first_frame_pending = false;
        s_camera_ctx.target_fps = 0;
        ws_camera_reset_stats_locked();
        ws_camera_release_pending_locked();
        ws_camera_drain_frame_signal();
        xSemaphoreGive(s_camera_ctx.lock);
    }
}

static void ws_camera_reset_stream(bool keep_config) {
    if (ws_camera_ensure_lock() != ESP_OK) {
        return;
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        s_camera_ctx.streaming = false;
        s_camera_ctx.transfer_active = false;
        s_camera_ctx.video_first_frame_pending = false;
        s_camera_ctx.target_fps = 0;
        if (!keep_config) {
            s_camera_ctx.configured_width = 0;
            s_camera_ctx.configured_height = 0;
            s_camera_ctx.configured_quality = WS_CAPTURE_DEFAULT_QUALITY;
        }
        ws_camera_reset_stats_locked();
        ws_camera_release_pending_locked();
        ws_camera_drain_frame_signal();
        xSemaphoreGive(s_camera_ctx.lock);
    }
}

static void ws_camera_upload_task(void *arg) {
    (void)arg;

    while (true) {
        uint8_t *jpeg = NULL;
        size_t size = 0;
        uint32_t timestamp_ms = 0;
        bool first_frame = false;
        uint32_t frames_sent = 0;
        uint64_t send_total_us = 0;
        uint64_t send_lock_wait_total_us = 0;
        uint64_t send_sock_total_us = 0;
        uint32_t frames_dropped = 0;
        ws_client_media_send_stats_t send_stats = {0};
        int send_ret;

        if (xSemaphoreTake(s_camera_ctx.frame_ready_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (s_camera_ctx.shutdown_requested) {
            ws_camera_release_pending_locked();
            s_camera_ctx.upload_task = NULL;
            xSemaphoreGive(s_camera_ctx.lock);
            vTaskDelete(NULL);
        }

        if (!s_camera_ctx.streaming || !s_camera_ctx.transfer_active || s_camera_ctx.pending_jpeg == NULL ||
            s_camera_ctx.pending_size == 0U) {
            xSemaphoreGive(s_camera_ctx.lock);
            continue;
        }

        jpeg = s_camera_ctx.pending_jpeg;
        size = s_camera_ctx.pending_size;
        timestamp_ms = s_camera_ctx.pending_timestamp_ms;
        first_frame = s_camera_ctx.video_first_frame_pending;
        s_camera_ctx.video_first_frame_pending = false;
        s_camera_ctx.pending_jpeg = NULL;
        s_camera_ctx.pending_size = 0;
        s_camera_ctx.pending_timestamp_ms = 0;
        xSemaphoreGive(s_camera_ctx.lock);

        send_ret = ws_send_video_frame(jpeg, size, first_frame);
        ws_client_get_media_send_stats(&send_stats);
        if (send_ret < 0) {
            ESP_LOGW(TAG, "video frame upload failed: size=%u ts=%lu", (unsigned int)size, (unsigned long)timestamp_ms);
        } else if (send_stats.valid && xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            s_camera_ctx.frames_sent++;
            s_camera_ctx.send_total_us += send_stats.total_us;
            s_camera_ctx.send_lock_wait_total_us += send_stats.lock_wait_us;
            s_camera_ctx.send_sock_total_us += send_stats.send_us;
            frames_sent = s_camera_ctx.frames_sent;
            send_total_us = s_camera_ctx.send_total_us;
            send_lock_wait_total_us = s_camera_ctx.send_lock_wait_total_us;
            send_sock_total_us = s_camera_ctx.send_sock_total_us;
            frames_dropped = s_camera_ctx.frames_dropped;
            xSemaphoreGive(s_camera_ctx.lock);

            if ((frames_sent % 30U) == 0U) {
                ESP_LOGI(TAG,
                         "ws media timing latest_us{total=%lu lock=%lu send=%lu payload=%u packet=%u ts=%lu} "
                         "avg_us{total=%lu lock=%lu send=%lu} frames=%lu dropped=%lu",
                         (unsigned long)send_stats.total_us, (unsigned long)send_stats.lock_wait_us,
                         (unsigned long)send_stats.send_us, (unsigned int)send_stats.payload_len,
                         (unsigned int)send_stats.packet_len, (unsigned long)timestamp_ms,
                         (unsigned long)(send_total_us / frames_sent),
                         (unsigned long)(send_lock_wait_total_us / frames_sent),
                         (unsigned long)(send_sock_total_us / frames_sent), (unsigned long)frames_sent,
                         (unsigned long)frames_dropped);
            }
        }

        free(jpeg);
    }
}

static esp_err_t ws_camera_ensure_upload_task(void) {
    BaseType_t task_ret;

    ESP_RETURN_ON_ERROR(ws_camera_ensure_lock(), TAG, "camera ws lock init failed");

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    if (s_camera_ctx.upload_task != NULL) {
        xSemaphoreGive(s_camera_ctx.lock);
        return ESP_OK;
    }
    s_camera_ctx.shutdown_requested = false;

    xSemaphoreGive(s_camera_ctx.lock);

    task_ret = xTaskCreate(ws_camera_upload_task, "ws_cam_upload", WS_CAMERA_UPLOAD_TASK_STACK, NULL,
                           WS_CAMERA_UPLOAD_TASK_PRIORITY, &s_camera_ctx.upload_task);
    if (task_ret != pdPASS) {
        if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            s_camera_ctx.upload_task = NULL;
            xSemaphoreGive(s_camera_ctx.lock);
        }
        ESP_LOGE(TAG, "camera upload task create failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void ws_camera_frame_cb(const uint8_t *jpeg, size_t size, uint32_t timestamp_ms, void *ctx) {
    bool streaming = false;
    bool transfer_active = false;
    uint8_t *jpeg_copy = NULL;

    (void)ctx;

    if (jpeg == NULL || size == 0U) {
        return;
    }

    if (ws_camera_ensure_lock() != ESP_OK) {
        return;
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        streaming = s_camera_ctx.streaming;
        transfer_active = s_camera_ctx.transfer_active;
        xSemaphoreGive(s_camera_ctx.lock);
    }

    if (!transfer_active) {
        ESP_LOGW(TAG, "camera frame dropped: transfer not active");
        return;
    }

    if (streaming) {
        jpeg_copy = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (jpeg_copy == NULL) {
            jpeg_copy = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
        }
        if (jpeg_copy == NULL) {
            ESP_LOGW(TAG, "camera frame drop: alloc failed size=%u", (unsigned int)size);
            if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
                s_camera_ctx.frames_dropped++;
                xSemaphoreGive(s_camera_ctx.lock);
            }
            return;
        }

        memcpy(jpeg_copy, jpeg, size);
        if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            if (s_camera_ctx.pending_jpeg != NULL) {
                free(s_camera_ctx.pending_jpeg);
                s_camera_ctx.frames_dropped++;
            }
            s_camera_ctx.pending_jpeg = jpeg_copy;
            s_camera_ctx.pending_size = size;
            s_camera_ctx.pending_timestamp_ms = timestamp_ms;
            xSemaphoreGive(s_camera_ctx.lock);
            xSemaphoreGive(s_camera_ctx.frame_ready_sem);
        } else {
            free(jpeg_copy);
        }
        return;
    }

    if (ws_send_image_frame(jpeg, size) < 0) {
        ESP_LOGW(TAG, "image frame upload failed: size=%u", (unsigned int)size);
        ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "image_upload_failed");
        ws_send_camera_state("capture_image", "error", 0, "image_upload_failed");
    } else {
        ws_send_camera_state("capture_image", "completed", 0, NULL);
    }

    ws_camera_finish_one_shot();
}

static esp_err_t ws_camera_ensure_ready(void) {
    esp_err_t ret;

    ESP_RETURN_ON_ERROR(ws_camera_ensure_lock(), TAG, "camera ws lock init failed");
    ESP_RETURN_ON_ERROR(ws_camera_ensure_upload_task(), TAG, "camera upload task init failed");
    ESP_RETURN_ON_ERROR(camera_service_init(), TAG, "camera service init failed");

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    if (!s_camera_ctx.callback_registered) {
        ret = camera_service_register_frame_callback(ws_camera_frame_cb, NULL);
        if (ret == ESP_OK) {
            s_camera_ctx.callback_registered = true;
        }
    } else {
        ret = ESP_OK;
    }

    xSemaphoreGive(s_camera_ctx.lock);
    return ret;
}

esp_err_t ws_camera_runtime_stop(void) {
    esp_err_t status = ESP_OK;
    esp_err_t ret;
    bool callback_registered = false;

    if (s_camera_ctx.lock == NULL) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        callback_registered = s_camera_ctx.callback_registered;
        s_camera_ctx.streaming = false;
        s_camera_ctx.transfer_active = false;
        s_camera_ctx.video_first_frame_pending = false;
        s_camera_ctx.target_fps = 0;
        ws_camera_reset_stats_locked();
        ws_camera_release_pending_locked();
        ws_camera_drain_frame_signal();
        xSemaphoreGive(s_camera_ctx.lock);
    } else {
        return ESP_FAIL;
    }

    if (callback_registered) {
        ret = camera_service_unregister_frame_callback();
        if (ret != ESP_OK) {
            status = ret;
            ESP_LOGW(TAG, "camera callback unregister failed: %s", esp_err_to_name(ret));
        }
    }

    if (camera_service_is_streaming()) {
        ret = camera_service_stop_stream();
        if (ret != ESP_OK && status == ESP_OK) {
            status = ret;
        }
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        s_camera_ctx.callback_registered = false;
        xSemaphoreGive(s_camera_ctx.lock);
    } else {
        status = ESP_FAIL;
    }

    return status;
}

static bool ws_camera_wait_for_upload_task_exit(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (s_camera_ctx.upload_task != NULL) {
        if (waited_ms >= timeout_ms) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        waited_ms += 50;
    }

    return true;
}

esp_err_t ws_camera_runtime_deinit(void) {
    esp_err_t status = ws_camera_runtime_stop();
    esp_err_t ret;

    if (s_camera_ctx.lock != NULL && xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        s_camera_ctx.shutdown_requested = true;
        ws_camera_release_pending_locked();
        xSemaphoreGive(s_camera_ctx.lock);
    }

    if (s_camera_ctx.frame_ready_sem != NULL) {
        xSemaphoreGive(s_camera_ctx.frame_ready_sem);
    }

    if (s_camera_ctx.upload_task != NULL &&
        !ws_camera_wait_for_upload_task_exit(WS_CAMERA_UPLOAD_TASK_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "camera upload task did not exit within %u ms", (unsigned)WS_CAMERA_UPLOAD_TASK_EXIT_WAIT_MS);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_camera_ctx.lock != NULL && xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        s_camera_ctx.shutdown_requested = false;
        s_camera_ctx.configured_width = 0;
        s_camera_ctx.configured_height = 0;
        s_camera_ctx.configured_quality = WS_CAPTURE_DEFAULT_QUALITY;
        xSemaphoreGive(s_camera_ctx.lock);
    }

    if (s_camera_ctx.frame_ready_sem != NULL) {
        vSemaphoreDelete(s_camera_ctx.frame_ready_sem);
        s_camera_ctx.frame_ready_sem = NULL;
    }
    if (s_camera_ctx.lock != NULL) {
        vSemaphoreDelete(s_camera_ctx.lock);
        s_camera_ctx.lock = NULL;
    }

    ret = camera_service_deinit();
    if (ret != ESP_OK && status == ESP_OK) {
        status = ret;
    }

    return status;
}

static const char *ws_camera_command_type(const ws_capture_cmd_t *cmd) {
    if (cmd == NULL) {
        return "ctrl.camera.unknown";
    }
    if (strcasecmp(cmd->action, "config") == 0) {
        return "ctrl.camera.video_config";
    }
    if (strcasecmp(cmd->action, "start") == 0) {
        return "ctrl.camera.start_video";
    }
    if (strcasecmp(cmd->action, "stop") == 0) {
        return "ctrl.camera.stop_video";
    }
    return "ctrl.camera.capture_image";
}

static void ws_camera_get_cached_config(int *width, int *height, int *fps, int *quality) {
    if (width != NULL) {
        *width = 0;
    }
    if (height != NULL) {
        *height = 0;
    }
    if (fps != NULL) {
        *fps = 0;
    }
    if (quality != NULL) {
        *quality = WS_CAPTURE_DEFAULT_QUALITY;
    }

    if (ws_camera_ensure_lock() != ESP_OK) {
        return;
    }

    if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
        if (width != NULL) {
            *width = s_camera_ctx.configured_width;
        }
        if (height != NULL) {
            *height = s_camera_ctx.configured_height;
        }
        if (fps != NULL) {
            *fps = s_camera_ctx.target_fps;
        }
        if (quality != NULL) {
            *quality = s_camera_ctx.configured_quality;
        }
        xSemaphoreGive(s_camera_ctx.lock);
    }
}

static bool ws_contains_nocase(const char *haystack, const char *needle) {
    size_t needle_len;

    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    needle_len = strlen(needle);
    while (*haystack != '\0') {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return true;
        }
        haystack++;
    }

    return false;
}

static void ws_copy_string(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#if SERVO_POSITION_REPORT_ENABLED
static void ws_servo_report_task(void *arg) {
    bool session_ready_seen = false;
    bool motion_active = false;
    bool final_report_sent = false;
    int last_observed_x = -1;
    int last_observed_y = -1;
    int last_reported_x = -1;
    int last_reported_y = -1;

    (void)arg;

    while (true) {
        int x_deg;
        int y_deg;
        bool suppress_action_reports;

        if (!ws_client_is_session_ready()) {
            session_ready_seen = false;
            motion_active = false;
            final_report_sent = false;
            last_observed_x = -1;
            last_observed_y = -1;
            last_reported_x = -1;
            last_reported_y = -1;
            vTaskDelay(pdMS_TO_TICKS(SERVO_REPORT_INTERVAL_MS));
            continue;
        }

        x_deg = hal_servo_get_angle(SERVO_AXIS_X);
        y_deg = hal_servo_get_angle(SERVO_AXIS_Y);
        if (x_deg < 0 || y_deg < 0) {
            vTaskDelay(pdMS_TO_TICKS(SERVO_REPORT_INTERVAL_MS));
            continue;
        }

        if (!session_ready_seen) {
            ws_send_servo_position((float)x_deg, (float)y_deg);
            session_ready_seen = true;
            motion_active = false;
            final_report_sent = true;
            last_observed_x = x_deg;
            last_observed_y = y_deg;
            last_reported_x = x_deg;
            last_reported_y = y_deg;
            vTaskDelay(pdMS_TO_TICKS(SERVO_REPORT_INTERVAL_MS));
            continue;
        }

        suppress_action_reports = behavior_state_is_action_active();
        if (suppress_action_reports) {
            motion_active = false;
            final_report_sent = true;
            last_observed_x = x_deg;
            last_observed_y = y_deg;
            last_reported_x = x_deg;
            last_reported_y = y_deg;
            vTaskDelay(pdMS_TO_TICKS(SERVO_REPORT_INTERVAL_MS));
            continue;
        }

        if (x_deg != last_observed_x || y_deg != last_observed_y) {
            motion_active = true;
            final_report_sent = false;
            last_observed_x = x_deg;
            last_observed_y = y_deg;
            if (x_deg != last_reported_x || y_deg != last_reported_y) {
                ws_send_servo_position((float)x_deg, (float)y_deg);
                last_reported_x = x_deg;
                last_reported_y = y_deg;
            }
        } else if (motion_active && !final_report_sent) {
            ws_send_servo_position((float)x_deg, (float)y_deg);
            motion_active = false;
            final_report_sent = true;
            last_reported_x = x_deg;
            last_reported_y = y_deg;
        }

        vTaskDelay(pdMS_TO_TICKS(SERVO_REPORT_INTERVAL_MS));
    }
}
#endif

static bool ws_servo_feedback_should_report(ws_servo_feedback_report_state_t *state, uint16_t raw, int16_t angle_x10) {
    uint16_t raw_delta;
    int angle_delta;

    if (state == NULL) {
        return false;
    }

    if (!state->initialized) {
        state->initialized = true;
        state->raw = raw;
        state->angle_x10 = angle_x10;
        return true;
    }

    raw_delta = (raw >= state->raw) ? (uint16_t)(raw - state->raw) : (uint16_t)(state->raw - raw);
    angle_delta = abs((int)angle_x10 - (int)state->angle_x10);
    if (raw_delta < SERVO_FEEDBACK_RAW_DELTA_MIN && angle_delta < SERVO_FEEDBACK_ANGLE_X10_DELTA_MIN) {
        return false;
    }

    state->raw = raw;
    state->angle_x10 = angle_x10;
    return true;
}

static float ws_clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int ws_round_float_to_int(float value) {
    return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static void ws_servo_feedback_reset_report_state(uint8_t axis_mask) {
    if ((axis_mask & HAL_SERVO_AXIS_MASK_X) != 0u) {
        s_servo_feedback_x_state.initialized = false;
    }
    if ((axis_mask & HAL_SERVO_AXIS_MASK_Y) != 0u) {
        s_servo_feedback_y_state.initialized = false;
    }
}

static void ws_servo_feedback_callback(const hal_servo_feedback_t *feedback, void *ctx) {
    (void)ctx;
    if (feedback == NULL) {
        return;
    }
    if ((feedback->axis_mask & HAL_SERVO_AXIS_MASK_X) != 0u &&
        ws_servo_feedback_should_report(&s_servo_feedback_x_state, feedback->x_raw, feedback->x_angle_x10)) {
        (void)ws_send_servo_feedback("x", feedback->x_raw, ((float)feedback->x_angle_x10) / 10.0f);
    }
    if ((feedback->axis_mask & HAL_SERVO_AXIS_MASK_Y) != 0u &&
        ws_servo_feedback_should_report(&s_servo_feedback_y_state, feedback->y_raw, feedback->y_angle_x10)) {
        (void)ws_send_servo_feedback("y", feedback->y_raw, ((float)feedback->y_angle_x10) / 10.0f);
    }
}

static esp_err_t ws_retry_servo_pwm_command(esp_err_t (*command_fn)(uint8_t), uint8_t axis_mask) {
    esp_err_t ret = ESP_FAIL;

    if (command_fn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int attempt = 0; attempt < SERVO_PWM_RETRY_COUNT; attempt++) {
        ret = command_fn(axis_mask);
        if (ret == ESP_OK || (ret != ESP_ERR_TIMEOUT && ret != ESP_ERR_INVALID_STATE)) {
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(SERVO_PWM_RETRY_DELAY_MS));
    }

    return ret;
}

void ws_handlers_init(void) {
    (void)hal_servo_set_feedback_callback(ws_servo_feedback_callback, NULL);
#if SERVO_POSITION_REPORT_ENABLED
    if (s_servo_report_task == NULL) {
        if (xTaskCreate(ws_servo_report_task, "ws_servo_report", SERVO_REPORT_TASK_STACK, NULL,
                        SERVO_REPORT_TASK_PRIORITY, &s_servo_report_task) != pdPASS) {
            s_servo_report_task = NULL;
            ESP_LOGE(TAG, "servo report task create failed");
        }
    }
#else
    ESP_LOGI(TAG, "Servo position reporting disabled");
#endif
}

static void ws_fill_app_package_transfer(const ws_transfer_cmd_t *cmd, ws_app_package_transfer_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (cmd == NULL) {
        return;
    }
    ws_copy_string(out->command_id, sizeof(out->command_id),
                   cmd->command_id[0] != '\0' ? cmd->command_id : cmd->transfer_id);
    ws_copy_string(out->app_id, sizeof(out->app_id), cmd->app_id);
    ws_copy_string(out->name, sizeof(out->name), cmd->name);
    ws_copy_string(out->version, sizeof(out->version), cmd->version);
    ws_copy_string(out->description, sizeof(out->description), cmd->description);
    ws_copy_string(out->package_type, sizeof(out->package_type), cmd->package_type);
    ws_copy_string(out->source_url, sizeof(out->source_url), cmd->source_url);
    ws_copy_string(out->image_version, sizeof(out->image_version), cmd->image_version);
    ws_copy_string(out->file_name, sizeof(out->file_name), cmd->file_name);
    ws_copy_string(out->sha256, sizeof(out->sha256), cmd->sha256);
    out->size_bytes = cmd->size_bytes;
}

static void ws_fill_app_package_command(const ws_transfer_cmd_t *cmd, ws_app_package_command_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (cmd == NULL) {
        return;
    }
    ws_copy_string(out->command_id, sizeof(out->command_id),
                   cmd->command_id[0] != '\0' ? cmd->command_id : cmd->transfer_id);
    ws_copy_string(out->app_id, sizeof(out->app_id), cmd->app_id);
    ws_copy_string(out->name, sizeof(out->name), cmd->name);
    ws_copy_string(out->version, sizeof(out->version), cmd->version);
}

static const char *ws_app_package_error_message(const char *fallback) {
    if (fallback != NULL && fallback[0] != '\0') {
        return fallback;
    }
    return "device rejected app package";
}

const char *ws_ai_status_to_emoji(const char *status, const char *message) {
    if (ws_contains_nocase(status, "bluetooth") || ws_contains_nocase(message, "bluetooth") ||
        ws_contains_nocase(status, "blue tooth") || ws_contains_nocase(message, "blue tooth") ||
        ws_contains_nocase(status, "pairing") || ws_contains_nocase(message, "pairing") ||
        ws_contains_nocase(status, "paired") || ws_contains_nocase(message, "paired")) {
        return "bluetooth";
    }
    if (ws_contains_nocase(status, "observing") || ws_contains_nocase(message, "observing")) {
        return "custom3";
    }
    if (ws_contains_nocase(status, "listening") || ws_contains_nocase(message, "listening")) {
        return "listening";
    }
    if (ws_contains_nocase(status, "thinking") || ws_contains_nocase(message, "thinking")) {
        return "thinking";
    }
    if (ws_contains_nocase(status, "processing") || ws_contains_nocase(status, "analyzing") ||
        ws_contains_nocase(message, "processing") || ws_contains_nocase(message, "analyzing")) {
        return "thinking";
    }
    if (ws_contains_nocase(status, "speaking") || ws_contains_nocase(message, "speaking")) {
        return "speaking";
    }
    if (ws_contains_nocase(status, "idle") || ws_contains_nocase(message, "idle")) {
        return "standby";
    }
    if (ws_contains_nocase(status, "done") || ws_contains_nocase(status, "completed") ||
        ws_contains_nocase(message, "done") || ws_contains_nocase(message, "completed")) {
        return "happy";
    }
    if (ws_contains_nocase(status, "error") || ws_contains_nocase(status, "fail") ||
        ws_contains_nocase(message, "error") || ws_contains_nocase(message, "fail")) {
        return "error";
    }
    return NULL;
}

void on_sys_ack_handler(const ws_sys_ack_t *msg) {
    if (msg == NULL) {
        return;
    }

    ESP_LOGI(TAG, "sys.ack: type=%s command_id=%s code=%d message=%s", msg->type, msg->command_id, msg->code,
             msg->message);
    if (strcmp(msg->type, "sys.client.hello") == 0) {
        (void)ws_client_apply_audio_uplink_negotiation(
            msg->audio_uplink_codec, msg->audio_uplink_sample_rate, msg->audio_uplink_channels,
            msg->audio_uplink_frame_duration_ms, msg->audio_uplink_packetization, msg->audio_uplink_version);
        ws_client_mark_hello_acked();
    }
}

void on_sys_nack_handler(const ws_sys_nack_t *msg) {
    control_state_text_request_t req = {0};

    if (msg == NULL) {
        return;
    }

    ESP_LOGW(TAG, "sys.nack: type=%s command_id=%s code=%d reason=%s", msg->type, msg->command_id, msg->code,
             msg->reason);
    if (strcmp(msg->type, "sys.client.hello") == 0) {
        ESP_LOGW(TAG, "Suppressing transient hello nack UI; transport will retry");
        return;
    }

    snprintf(req.state_id, sizeof(req.state_id), "%s", "error");
    snprintf(req.text, sizeof(req.text), "%s",
             msg->reason[0] != '\0' ? msg->reason : "");

    if (req.text[0] != '\0' && control_ingress_submit_state_text(&req) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enqueue error state update");
    }
}

void on_sys_ping_handler(void) {
    ws_send_sys_pong();
}

void on_sys_pong_handler(void) {
    ESP_LOGD(TAG, "sys.pong received");
}

void on_session_resume_handler(void) {
    ESP_LOGW(TAG, "sys.session.resume ignored");
}

void on_servo_handler(const ws_servo_cmd_t *cmd) {
    control_servo_request_t req = {0};
    esp_err_t ret;
    int duration_ms;

    if (cmd == NULL) {
        return;
    }

    if (!cmd->has_x && !cmd->has_y) {
        ws_send_sys_nack("ctrl.servo.angle", NULL, "invalid_servo_payload");
        return;
    }

    req.has_x = cmd->has_x;
    req.has_y = cmd->has_y;
    req.x_deg = (int)(cmd->x_deg + 0.5f);
    req.y_deg = (int)(cmd->y_deg + 0.5f);
    req.source = CONTROL_MOTION_SOURCE_WS;
    duration_ms = cmd->duration_ms;
    if (duration_ms <= 0) {
        ws_send_sys_nack("ctrl.servo.angle", NULL, "invalid_duration_ms");
        return;
    }
    req.duration_ms = duration_ms;
    if ((req.has_x && (req.x_deg < 0 || req.x_deg > 180)) || (req.has_y && (req.y_deg < 0 || req.y_deg > 180))) {
        ws_send_sys_nack("ctrl.servo.angle", NULL, "angle_out_of_range");
        return;
    }

    ESP_LOGI(TAG, "servo command: has_x=%d x=%d has_y=%d y=%d duration_ms=%d", req.has_x, req.x_deg, req.has_y,
             req.y_deg, duration_ms);
    ret = control_ingress_submit_servo(&req);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.servo.angle", NULL);
    } else if (ret == ESP_ERR_TIMEOUT) {
        ws_send_sys_nack("ctrl.servo.angle", NULL, "busy");
    } else {
        ws_send_device_error(WS_DEVICE_ERROR_CODE_SERVO, "servo_move_failed");
        ws_send_sys_nack("ctrl.servo.angle", NULL, "servo_move_failed");
    }
}

void on_servo_trajectory_play_handler(const ws_servo_trajectory_cmd_t *cmd) {
    esp_err_t ret;
    int frame_count;
    hal_servo_trajectory_frame_t frames[WS_SERVO_TRAJECTORY_MAX_FRAMES];
    size_t valid_frame_count = 0u;

    if (cmd == NULL || cmd->frame_count <= 0) {
        ws_send_sys_nack("ctrl.servo.trajectory.play", cmd != NULL ? cmd->command_id : NULL, "invalid_trajectory_payload");
        return;
    }

    frame_count = cmd->frame_count;
    if (frame_count > WS_SERVO_TRAJECTORY_MAX_FRAMES) {
        frame_count = WS_SERVO_TRAJECTORY_MAX_FRAMES;
    }

    for (int index = 0; index < frame_count; index++) {
        const ws_servo_trajectory_frame_t *frame = &cmd->frames[index];
        hal_servo_trajectory_frame_t *segment = &frames[valid_frame_count];
        float x_deg = ws_clamp_float(frame->x_deg, SERVO_TRAJECTORY_X_MIN_DEG, SERVO_TRAJECTORY_X_MAX_DEG);
        float y_deg = ws_clamp_float(frame->y_deg, SERVO_TRAJECTORY_Y_MIN_DEG, SERVO_TRAJECTORY_Y_MAX_DEG);
        int duration_ms = frame->duration_ms;

        if (duration_ms < SERVO_TRAJECTORY_MIN_DURATION_MS) {
            duration_ms = SERVO_TRAJECTORY_MIN_DURATION_MS;
        } else if (duration_ms > SERVO_TRAJECTORY_MAX_DURATION_MS) {
            duration_ms = SERVO_TRAJECTORY_MAX_DURATION_MS;
        }

        segment->axis_mask = frame->axis_mask & (HAL_SERVO_AXIS_MASK_X | HAL_SERVO_AXIS_MASK_Y);
        if (segment->axis_mask == 0u) {
            continue;
        }
        segment->x_deg_x10 = (int16_t)ws_round_float_to_int(x_deg * 10.0f);
        segment->y_deg_x10 = (int16_t)ws_round_float_to_int(y_deg * 10.0f);
        segment->duration_ms = (uint16_t)duration_ms;
        segment->motion_profile = frame->motion_profile == HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT
                                      ? HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT
                                      : HAL_SERVO_MOTION_PROFILE_LINEAR;

        ESP_LOGI(TAG,
                 "servo trajectory segment prepared: command_id=%s index=%u/%d axis_mask=0x%02x x_deg_x10=%d y_deg_x10=%d duration_ms=%u profile=%u enqueue_tick_us=%lld",
                 cmd->command_id,
                 (unsigned)(valid_frame_count + 1u),
                 frame_count,
                 (unsigned)segment->axis_mask,
                 (int)segment->x_deg_x10,
                 (int)segment->y_deg_x10,
                 (unsigned)segment->duration_ms,
                 (unsigned)segment->motion_profile,
                 (long long)esp_timer_get_time());

        valid_frame_count++;
    }

    if (valid_frame_count == 0u) {
        ws_send_sys_nack("ctrl.servo.trajectory.play", cmd->command_id, "empty_trajectory");
        return;
    }

    ret = hal_servo_play_trajectory(frames, valid_frame_count, HAL_SERVO_MOTION_SOURCE_WS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "servo trajectory sequence queue failed: command_id=%s segments=%u err=%s",
                 cmd->command_id,
                 (unsigned)valid_frame_count,
                 esp_err_to_name(ret));
        if (ret == ESP_ERR_INVALID_STATE) {
            ws_send_sys_nack("ctrl.servo.trajectory.play", cmd->command_id, "mcu_link_not_ready");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ws_send_sys_nack("ctrl.servo.trajectory.play", cmd->command_id, "busy");
        } else {
            ws_send_device_error(WS_DEVICE_ERROR_CODE_SERVO, "servo_trajectory_failed");
            ws_send_sys_nack("ctrl.servo.trajectory.play", cmd->command_id, "servo_trajectory_failed");
        }
        return;
    }

    ESP_LOGI(TAG,
             "servo trajectory play queued: command_id=%s segments=%u",
             cmd->command_id,
             (unsigned)valid_frame_count);
    ws_servo_feedback_reset_report_state(HAL_SERVO_AXIS_MASK_X | HAL_SERVO_AXIS_MASK_Y);
    ws_send_sys_ack("ctrl.servo.trajectory.play", cmd->command_id);
}

static bool light_byte_is_valid(int value) {
    return value >= 0 && value <= 255;
}

static bool light_u16_is_valid(int value) {
    return value >= 0 && value <= 65535;
}

static uint8_t light_effect_from_name(const char *effect) {
    if (effect == NULL || effect[0] == '\0') {
        return 0U;
    }
    if (strcasecmp(effect, "blink") == 0) {
        return MCU_LED_EFFECT_BLINK;
    }
    if (strcasecmp(effect, "breathing") == 0 || strcasecmp(effect, "breathe") == 0) {
        return MCU_LED_EFFECT_BREATHING;
    }
    if (strcasecmp(effect, "rainbow") == 0) {
        return MCU_LED_EFFECT_RAINBOW;
    }
    if (strcasecmp(effect, "status_pulse") == 0 || strcasecmp(effect, "status-pulse") == 0) {
        return MCU_LED_EFFECT_STATUS_PULSE;
    }
    return 0U;
}

static bool light_zone_from_name(const char *zone, mcu_led_zone_t *out_zone) {
    if (out_zone == NULL) {
        return false;
    }
    if (zone == NULL || zone[0] == '\0' || strcasecmp(zone, "all") == 0 || strcasecmp(zone, "both") == 0) {
        *out_zone = MCU_LED_ZONE_ALL;
        return true;
    }
    if (strcasecmp(zone, "side") == 0) {
        *out_zone = MCU_LED_ZONE_SIDE;
        return true;
    }
    if (strcasecmp(zone, "bottom") == 0) {
        *out_zone = MCU_LED_ZONE_BOTTOM;
        return true;
    }
    return false;
}

void on_light_set_handler(const ws_light_cmd_t *cmd) {
    mcu_led_request_t req = {0};
    esp_err_t ret;
    const char *mode;

    if (cmd == NULL) {
        return;
    }

    mode = cmd->mode[0] != '\0' ? cmd->mode : "static";
    if (!light_byte_is_valid(cmd->red) || !light_byte_is_valid(cmd->green) || !light_byte_is_valid(cmd->blue) ||
        !light_byte_is_valid(cmd->secondary_red) || !light_byte_is_valid(cmd->secondary_green) ||
        !light_byte_is_valid(cmd->secondary_blue) || !light_byte_is_valid(cmd->brightness) ||
        !light_u16_is_valid(cmd->period_ms) || !light_u16_is_valid(cmd->repeat_count)) {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "invalid_light_payload");
        return;
    }

    req.primary_red = (uint8_t)cmd->red;
    req.primary_green = (uint8_t)cmd->green;
    req.primary_blue = (uint8_t)cmd->blue;
    req.secondary_red = (uint8_t)cmd->secondary_red;
    req.secondary_green = (uint8_t)cmd->secondary_green;
    req.secondary_blue = (uint8_t)cmd->secondary_blue;
    req.brightness = (uint8_t)cmd->brightness;
    req.period_ms = (uint16_t)cmd->period_ms;
    req.repeat_count = (uint16_t)cmd->repeat_count;
    if (!light_zone_from_name(cmd->zone, &req.zone)) {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "invalid_light_zone");
        return;
    }

    if (strcasecmp(mode, "off") == 0) {
        req.mode = MCU_LED_MODE_OFF;
    } else if (strcasecmp(mode, "static") == 0) {
        req.mode = MCU_LED_MODE_STATIC;
    } else if (strcasecmp(mode, "effect") == 0) {
        req.mode = MCU_LED_MODE_EFFECT;
        req.effect_id = light_effect_from_name(cmd->effect);
        if (req.effect_id == 0U) {
            ws_send_sys_nack("ctrl.light.set", cmd->command_id, "invalid_light_effect");
            return;
        }
        if (req.period_ms == 0U) {
            ws_send_sys_nack("ctrl.light.set", cmd->command_id, "invalid_period_ms");
            return;
        }
    } else {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "invalid_light_mode");
        return;
    }

    ESP_LOGI(TAG,
             "light command: zone=%s mode=%s effect=%s rgb=(%d,%d,%d) brightness=%d period=%d repeat=%d command_id=%s",
             cmd->zone[0] != '\0' ? cmd->zone : "all", mode, cmd->effect, cmd->red, cmd->green, cmd->blue,
             cmd->brightness, cmd->period_ms, cmd->repeat_count, cmd->command_id);
    ret = mcu_led_submit(&req);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.light.set", cmd->command_id);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "mcu_link_not_ready");
    } else if (ret == ESP_ERR_TIMEOUT) {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "busy");
    } else {
        ws_send_sys_nack("ctrl.light.set", cmd->command_id, "light_set_failed");
    }
}

void on_servo_pwm_unlock_handler(const ws_servo_pwm_unlock_cmd_t *cmd) {
    esp_err_t ret;

    if (cmd == NULL || cmd->axis_mask == 0u) {
        ws_send_sys_nack("ctrl.servo.pwm.unlock", cmd != NULL ? cmd->command_id : NULL, "invalid_axes");
        return;
    }

    ret = ws_retry_servo_pwm_command(hal_servo_pwm_unlock, cmd->axis_mask);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "servo pwm unlock failed axis_mask=0x%02x err=%s", cmd->axis_mask, esp_err_to_name(ret));
        ws_send_sys_nack("ctrl.servo.pwm.unlock", cmd->command_id, esp_err_to_name(ret));
        return;
    }

    ws_servo_feedback_reset_report_state(cmd->axis_mask);
    ws_send_sys_ack("ctrl.servo.pwm.unlock", cmd->command_id);
}

void on_servo_pwm_lock_handler(const ws_servo_pwm_unlock_cmd_t *cmd) {
    esp_err_t ret;

    if (cmd == NULL || cmd->axis_mask == 0u) {
        ws_send_sys_nack("ctrl.servo.pwm.lock", cmd != NULL ? cmd->command_id : NULL, "invalid_axes");
        return;
    }

    ret = ws_retry_servo_pwm_command(hal_servo_pwm_lock, cmd->axis_mask);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "servo pwm lock failed axis_mask=0x%02x err=%s", cmd->axis_mask, esp_err_to_name(ret));
        ws_send_sys_nack("ctrl.servo.pwm.lock", cmd->command_id, esp_err_to_name(ret));
        return;
    }

    ws_servo_feedback_reset_report_state(cmd->axis_mask);
    ws_send_sys_ack("ctrl.servo.pwm.lock", cmd->command_id);
}

void on_motion_jog_handler(const ws_motion_jog_cmd_t *cmd) {
    control_jog_request_t req = {0};
    esp_err_t ret;
    int velocity;

    if (cmd == NULL) {
        return;
    }
    if (cmd->direction != -1 && cmd->direction != 1) {
        ws_send_sys_nack("ctrl.motion.jog", NULL, "invalid_direction");
        return;
    }
    velocity = cmd->velocity_deg_per_sec;
    if (velocity == 0) {
        velocity = (int)(cmd->speed * 90.0f + 0.5f);
    }
    if (velocity <= 0 || velocity > 180 || cmd->timeout_ms <= 0 || cmd->timeout_ms > 5000) {
        ws_send_sys_nack("ctrl.motion.jog", NULL, "invalid_jog_payload");
        return;
    }

    req.is_x_axis = cmd->is_x_axis;
    req.velocity_deg_per_sec = velocity * cmd->direction;
    req.timeout_ms = cmd->timeout_ms;
    req.source = CONTROL_MOTION_SOURCE_WS;
    ret = control_ingress_submit_jog(&req);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.motion.jog", NULL);
    } else if (ret == ESP_ERR_TIMEOUT) {
        ws_send_sys_nack("ctrl.motion.jog", NULL, "busy");
    } else {
        ws_send_sys_nack("ctrl.motion.jog", NULL, "jog_failed");
    }
}

void on_motion_stop_handler(void) {
    esp_err_t ret = control_ingress_stop_manual(CONTROL_MOTION_SOURCE_WS);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.motion.stop", NULL);
    } else {
        ws_send_sys_nack("ctrl.motion.stop", NULL, "stop_failed");
    }
}

static void send_microphone_command_result(const char *command_type, const char *command_id, esp_err_t ret) {
    if (ret == ESP_OK) {
        ws_send_sys_ack(command_type, command_id);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ws_send_sys_nack(command_type, command_id, "recorder_not_ready");
    } else if (ret == ESP_ERR_TIMEOUT) {
        ws_send_sys_nack(command_type, command_id, "busy");
    } else {
        ws_send_sys_nack(command_type, command_id, "microphone_control_failed");
    }
}

void on_microphone_open_handler(const ws_microphone_cmd_t *cmd) {
    esp_err_t ret;

    if (cmd == NULL) {
        return;
    }

    ESP_LOGI(TAG, "microphone command: type=ctrl.microphone.open command_id=%s", cmd->command_id);
    if (ws_client_is_tts_playing()) {
        ESP_LOGI(TAG, "microphone open during TTS playback: treating as barge-in command_id=%s", cmd->command_id);
        ws_client_abort_tts_playback();
    }
    ret = voice_recorder_request_open();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "microphone open rejected: %s", esp_err_to_name(ret));
    }
    send_microphone_command_result("ctrl.microphone.open", cmd->command_id, ret);
}

void on_microphone_close_handler(const ws_microphone_cmd_t *cmd) {
    esp_err_t ret;

    if (cmd == NULL) {
        return;
    }

    ESP_LOGI(TAG, "microphone command: type=ctrl.microphone.close command_id=%s", cmd->command_id);
    if (ws_client_is_tts_playing()) {
        ESP_LOGW(TAG, "microphone close ignored during TTS playback command_id=%s", cmd->command_id);
        ws_send_sys_ack("ctrl.microphone.close", cmd->command_id);
        return;
    }
    ret = voice_recorder_request_close();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "microphone close rejected: %s", esp_err_to_name(ret));
    }
    send_microphone_command_result("ctrl.microphone.close", cmd->command_id, ret);
}

void on_state_set_handler(const ws_state_cmd_t *cmd) {
    control_state_set_request_t req = {0};
    esp_err_t ret;

    if (cmd == NULL || cmd->state_id[0] == '\0') {
        ws_send_sys_nack("ctrl.robot.state.set", cmd != NULL ? cmd->command_id : NULL, "invalid_state_payload");
        return;
    }

    snprintf(req.state_id, sizeof(req.state_id), "%s", cmd->state_id);
    ret = control_ingress_submit_state_set(&req);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.robot.state.set", cmd->command_id);
    } else if (ret == ESP_ERR_TIMEOUT) {
        ws_send_sys_nack("ctrl.robot.state.set", cmd->command_id, "busy");
    } else {
        ws_send_sys_nack("ctrl.robot.state.set", cmd->command_id, "state_set_failed");
    }
}

static void normalize_sound_id(const char *raw, char *out, size_t out_size) {
    const char *base;
    const char *ext;
    size_t len;
    size_t i;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (raw == NULL || raw[0] == '\0') {
        return;
    }

    base = raw;
    for (i = 0; raw[i] != '\0'; ++i) {
        if (raw[i] == '/' || raw[i] == '\\') {
            base = &raw[i + 1];
        }
    }

    ext = strrchr(base, '.');
    len = (ext != NULL && ext > base) ? (size_t)(ext - base) : strlen(base);
    if (len >= out_size) {
        len = out_size - 1;
    }
    for (i = 0; i < len; ++i) {
        out[i] = (char)tolower((unsigned char)base[i]);
    }
    out[len] = '\0';
}

void on_sound_play_handler(const ws_sound_cmd_t *cmd) {
    char sound_id[WS_RESOURCE_NAME_MAX];
    esp_err_t ret;

    if (cmd == NULL) {
        return;
    }

    normalize_sound_id(cmd->sound_id, sound_id, sizeof(sound_id));
    if (sound_id[0] == '\0') {
        ws_send_sys_nack("ctrl.sound.play", cmd->command_id, "invalid_sound_payload");
        return;
    }

    ESP_LOGI(TAG, "sound command: sound_id=%s delay=%dms command_id=%s", sound_id, cmd->delay_ms, cmd->command_id);
    ret = sfx_service_play_delayed(sound_id, cmd->delay_ms);
    if (ret == ESP_OK) {
        ws_send_sys_ack("ctrl.sound.play", cmd->command_id);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ws_send_sys_nack("ctrl.sound.play", cmd->command_id, "audio_busy_tts");
    } else {
        ws_send_sys_nack("ctrl.sound.play", cmd->command_id, "sound_play_failed");
    }
}

void on_capture_handler(const ws_capture_cmd_t *cmd) {
    esp_err_t ret;
    int fps = WS_CAPTURE_DEFAULT_FPS;
    int requested_width = 0;
    int requested_height = 0;
    int requested_quality = WS_CAPTURE_DEFAULT_QUALITY;
    int applied_width = 0;
    int applied_height = 0;
    int cached_fps = 0;
    const char *command_type;
    char config_message[96];

    if (cmd == NULL) {
        return;
    }

    command_type = ws_camera_command_type(cmd);
    ESP_LOGI(TAG, "camera command: type=%s command_id=%s action=%s width=%d height=%d fps=%d quality=%d", command_type,
             cmd->command_id, cmd->action, cmd->width, cmd->height, cmd->fps, cmd->quality);

    if (strcasecmp(cmd->action, "stop") == 0) {
        if (!camera_service_is_streaming()) {
            ws_send_sys_nack(command_type, cmd->command_id, "not_streaming");
            return;
        }

        if (s_camera_ctx.lock != NULL && xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            fps = s_camera_ctx.target_fps;
            xSemaphoreGive(s_camera_ctx.lock);
        }

        ws_send_sys_ack(command_type, cmd->command_id);
        ret = camera_service_stop_stream();
        if (ret == ESP_OK) {
            ws_send_video_end();
            ws_send_camera_state("stop_video", "stopped", fps, NULL);
            ws_camera_reset_stream(true);
        } else {
            ESP_LOGE(TAG, "camera stream stop failed: %s", esp_err_to_name(ret));
            ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "stream_stop_failed");
            ws_send_camera_state("stop_video", "error", fps, "stream_stop_failed");
        }
        return;
    }

    ret = ws_camera_ensure_ready();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera path not ready: %s", esp_err_to_name(ret));
        ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "camera_init_failed");
        ws_send_sys_nack(command_type, cmd->command_id, "camera_init_failed");
        (void)ws_camera_runtime_deinit();
        return;
    }

    if (strcasecmp(cmd->action, "config") == 0) {
        ws_camera_get_cached_config(&requested_width, &requested_height, &cached_fps, &requested_quality);
        if (cmd->width > 0) {
            requested_width = cmd->width;
        }
        if (cmd->height > 0) {
            requested_height = cmd->height;
        }
        if (cmd->quality > 0) {
            requested_quality = cmd->quality;
        }

        if ((requested_width > 0) != (requested_height > 0)) {
            ws_send_sys_nack(command_type, cmd->command_id, "width_height_must_be_paired");
            return;
        }

        if (requested_width > 0 && requested_height > 0) {
            ret = camera_service_configure(requested_width, requested_height, requested_quality, &applied_width,
                                           &applied_height);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "camera config failed: %s", esp_err_to_name(ret));
                if (ret == ESP_ERR_NOT_SUPPORTED) {
                    ws_send_sys_nack(command_type, cmd->command_id, "unsupported_resolution");
                } else if (ret == ESP_ERR_INVALID_STATE) {
                    ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "camera_busy");
                    ws_send_sys_nack(command_type, cmd->command_id, "camera_busy");
                } else {
                    ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "camera_config_failed");
                    ws_send_sys_nack(command_type, cmd->command_id, "camera_config_failed");
                }
                return;
            }
        } else {
            applied_width = requested_width;
            applied_height = requested_height;
        }

        if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            if (applied_width > 0) {
                s_camera_ctx.configured_width = applied_width;
            }
            if (applied_height > 0) {
                s_camera_ctx.configured_height = applied_height;
            }
            if (cmd->fps > 0) {
                s_camera_ctx.target_fps = cmd->fps;
            }
            s_camera_ctx.configured_quality = requested_quality;
            xSemaphoreGive(s_camera_ctx.lock);
        }

        if (applied_width > 0 && applied_height > 0) {
            snprintf(config_message, sizeof(config_message), "applied=%dx%d quality_hint=%d", applied_width,
                     applied_height, requested_quality);
        } else {
            snprintf(config_message, sizeof(config_message), "fps=%d quality_hint=%d resolution_unchanged",
                     cmd->fps > 0 ? cmd->fps : cached_fps, requested_quality);
        }

        ws_send_sys_ack(command_type, cmd->command_id);
        ws_send_camera_state("video_config", "accepted", cmd->fps > 0 ? cmd->fps : cached_fps, config_message);
        return;
    }

    if (strcasecmp(cmd->action, "start") == 0) {
        if (camera_service_is_streaming()) {
            ws_send_sys_nack(command_type, cmd->command_id, "already_streaming");
            return;
        }

        if (xSemaphoreTake(s_camera_ctx.lock, portMAX_DELAY) == pdTRUE) {
            if (s_camera_ctx.target_fps > 0) {
                fps = s_camera_ctx.target_fps;
            }
            xSemaphoreGive(s_camera_ctx.lock);
        }
        if (cmd->fps > 0) {
            fps = cmd->fps;
        }
        if (fps > WS_CAPTURE_MAX_FPS) {
            fps = WS_CAPTURE_MAX_FPS;
        }
        if (fps <= 0) {
            fps = WS_CAPTURE_DEFAULT_FPS;
        }

        if (ws_camera_begin_transfer(true, fps) != ESP_OK) {
            ws_send_sys_nack(command_type, cmd->command_id, "camera_state_unavailable");
            return;
        }

        ws_send_sys_ack(command_type, cmd->command_id);
        ret = camera_service_start_stream(fps);
        if (ret == ESP_OK) {
            ws_send_camera_state("start_video", "started", fps, "format=mjpeg");
            return;
        }

        ESP_LOGE(TAG, "camera stream start failed: %s", esp_err_to_name(ret));
        ws_camera_reset_stream(true);
        ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "stream_start_failed");
        ws_send_camera_state("start_video", "error", fps, "stream_start_failed");
        return;
    }

    if (ws_camera_begin_transfer(false, 0) != ESP_OK) {
        ws_send_sys_nack(command_type, cmd->command_id, "camera_state_unavailable");
        return;
    }

    ws_send_sys_ack(command_type, cmd->command_id);
    ret = camera_service_capture_once();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera single capture failed: %s", esp_err_to_name(ret));
        ws_send_device_error(WS_DEVICE_ERROR_CODE_CAMERA, "capture_failed");
        ws_send_camera_state("capture_image", "error", 0, "capture_failed");
        ws_camera_finish_one_shot();
    }
}

void on_asr_result_handler(const ws_text_event_t *event) {
    if (event == NULL) {
        return;
    }

    ESP_LOGI(TAG, "ASR result received; local voice state remains unchanged: %s", event->text);
}

void on_ai_status_handler(const ws_ai_status_t *event) {
    control_ai_status_request_t req = {0};
    const char *target_state;
    bool apply_after_tts;
    esp_err_t ret;

    if (event == NULL) {
        return;
    }

    target_state = ws_ai_status_to_emoji(event->status, event->message);
    apply_after_tts = ws_client_is_tts_playing() &&
                      !(target_state != NULL && strcmp(target_state, "error") == 0);

    ESP_LOGI(TAG,
             "AI status received: status=%s message=%s image=%s action=%s sound=%s tts_playing=%d defer=%d",
             event->status, event->message, event->image_name, event->action_file, event->sound_file,
             ws_client_is_tts_playing() ? 1 : 0, apply_after_tts ? 1 : 0);

    ws_copy_string(req.status, sizeof(req.status), event->status);
    ws_copy_string(req.message, sizeof(req.message), event->message);
    ws_copy_string(req.image_name, sizeof(req.image_name), event->image_name);
    ws_copy_string(req.action_file, sizeof(req.action_file), event->action_file);
    ws_copy_string(req.sound_file, sizeof(req.sound_file), event->sound_file);
    req.defer_ui_until_tts_complete = apply_after_tts;
    if (apply_after_tts) {
        ESP_LOGI(TAG, "Deferring remote AI status until current TTS completes: status=%s", event->status);
    }
    ret = control_ingress_submit_ai_status(&req);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enqueue AI status update: %s", esp_err_to_name(ret));
    }
}

void on_ai_thinking_handler(const ws_ai_thinking_t *event) {
    if (event == NULL) {
        return;
    }

    ESP_LOGI(TAG, "AI thinking: kind=%s content=%s", event->kind, event->content);
    ESP_LOGD(TAG, "Ignoring AI thinking event for local voice state");
}

void on_ai_reply_handler(const ws_text_event_t *event) {
    if (event == NULL) {
        return;
    }

    ESP_LOGI(TAG, "AI reply: %s", event->text);
}

void on_transfer_handler(const ws_transfer_cmd_t *cmd) {
    ws_app_package_transfer_t transfer;
    ws_app_package_command_t command;

    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd->message_type, "app.package.transfer.begin") == 0) {
        ws_fill_app_package_transfer(cmd, &transfer);
        if (s_app_package_handler.begin == NULL || s_app_package_handler.begin(&transfer) != 0) {
            ws_send_sys_nack(cmd->message_type, transfer.command_id, "app_package_begin_failed");
            ws_send_app_package_status(transfer.command_id, transfer.app_id, transfer.name, transfer.version,
                                       "install_failed", ws_app_package_error_message("begin failed"));
        } else {
            ws_send_sys_ack(cmd->message_type, transfer.command_id);
        }
        return;
    }
    if (strcmp(cmd->message_type, "app.package.transfer.commit") == 0) {
        ws_fill_app_package_transfer(cmd, &transfer);
        if (s_app_package_handler.commit == NULL || s_app_package_handler.commit(&transfer) != 0) {
            ws_send_sys_nack(cmd->message_type, transfer.command_id, "app_package_commit_failed");
            ws_send_app_package_status(transfer.command_id, transfer.app_id, transfer.name, transfer.version,
                                       "install_failed",
                                       ws_app_package_error_message("commit failed: check size, SHA-256, manifest, and permissions"));
        } else {
            ws_send_sys_ack(cmd->message_type, transfer.command_id);
        }
        return;
    }
    if (strcmp(cmd->message_type, "app.package.transfer.abort") == 0) {
        ws_fill_app_package_command(cmd, &command);
        if (s_app_package_handler.abort != NULL) {
            s_app_package_handler.abort(&command, cmd->reason);
        }
        ws_send_sys_ack(cmd->message_type, command.command_id);
        ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version,
                                   "install_failed", cmd->reason[0] != '\0' ? cmd->reason : "aborted");
        return;
    }
    if (strcmp(cmd->message_type, "app.package.list") == 0) {
        ws_fill_app_package_command(cmd, &command);
        if (s_app_package_handler.list == NULL || s_app_package_handler.list(&command) != 0) {
            ws_send_sys_nack(cmd->message_type, command.command_id, "app_package_list_failed");
        } else {
            ws_send_sys_ack(cmd->message_type, command.command_id);
        }
        return;
    }
    if (strcmp(cmd->message_type, "app.package.install") == 0) {
        ws_fill_app_package_transfer(cmd, &transfer);
        if (s_app_package_handler.install == NULL || s_app_package_handler.install(&transfer) != 0) {
            ws_send_sys_nack(cmd->message_type, transfer.command_id, "app_package_install_failed");
            ws_send_app_package_status(transfer.command_id, transfer.app_id, transfer.name, transfer.version,
                                       "install_failed",
                                       ws_app_package_error_message("install failed: check compatibility, HTTPS URL, size, SHA-256, and signature"));
        } else {
            ws_send_sys_ack(cmd->message_type, transfer.command_id);
        }
        return;
    }
    if (strcmp(cmd->message_type, "app.package.open") == 0) {
        int open_ret;
        ws_fill_app_package_command(cmd, &command);
        if (s_app_package_handler.open == NULL) {
            ws_send_sys_nack(cmd->message_type, command.command_id, "app_package_open_failed");
            ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version, "open_failed",
                                       "open handler missing");
        } else if ((open_ret = s_app_package_handler.open(&command)) != 0) {
            if (open_ret == ESP_ERR_NOT_FOUND) {
                ws_send_sys_nack(cmd->message_type, command.command_id, "app_package_not_installed");
                ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version, "uninstalled",
                                           "package is no longer installed on this device");
            } else {
                ws_send_sys_nack(cmd->message_type, command.command_id, "app_package_open_failed");
                ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version, "open_failed",
                                           "open failed");
            }
        } else {
            ws_send_sys_ack(cmd->message_type, command.command_id);
            ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version, "installed",
                                       "opened");
        }
        return;
    }
    if (strcmp(cmd->message_type, "app.package.uninstall") == 0) {
        ws_fill_app_package_command(cmd, &command);
        if (s_app_package_handler.uninstall == NULL || s_app_package_handler.uninstall(&command) != 0) {
            ws_send_sys_nack(cmd->message_type, command.command_id, "app_package_uninstall_failed");
            ws_send_app_package_status(command.command_id, command.app_id, command.name, command.version,
                                       "uninstall_failed", "uninstall failed");
        } else {
            ws_send_sys_ack(cmd->message_type, command.command_id);
        }
        return;
    }

    ESP_LOGW(TAG, "transfer not supported: type=%s transfer_id=%s", cmd->message_type, cmd->transfer_id);
    if (strcmp(cmd->message_type, "xfer.ota.handshake") == 0) {
        ws_send_ota_handshake(cmd->transfer_id[0] != '\0' ? cmd->transfer_id : NULL, "not_supported");
    }
    ws_send_ota_progress(0, "rejected", "not_supported");
    ws_send_device_error(WS_DEVICE_ERROR_CODE_OTA, "ota_not_supported");
    ws_send_sys_nack(cmd->message_type, cmd->transfer_id[0] != '\0' ? cmd->transfer_id : NULL, "not_supported");
}

void ws_handlers_set_app_package_handler(const ws_app_package_handler_t *handler) {
    if (handler == NULL) {
        memset(&s_app_package_handler, 0, sizeof(s_app_package_handler));
        return;
    }
    s_app_package_handler = *handler;
}

ws_router_t ws_handlers_get_router(void) {
    ws_router_t router = {
        .on_sys_ack = on_sys_ack_handler,
        .on_sys_nack = on_sys_nack_handler,
        .on_sys_ping = on_sys_ping_handler,
        .on_sys_pong = on_sys_pong_handler,
        .on_session_resume = on_session_resume_handler,
        .on_servo = on_servo_handler,
        .on_servo_pwm_unlock = on_servo_pwm_unlock_handler,
        .on_servo_pwm_lock = on_servo_pwm_lock_handler,
        .on_servo_trajectory_play = on_servo_trajectory_play_handler,
        .on_light_set = on_light_set_handler,
        .on_motion_jog = on_motion_jog_handler,
        .on_motion_stop = on_motion_stop_handler,
        .on_microphone_open = on_microphone_open_handler,
        .on_microphone_close = on_microphone_close_handler,
        .on_state_set = on_state_set_handler,
        .on_sound_play = on_sound_play_handler,
        .on_capture = on_capture_handler,
        .on_asr_result = on_asr_result_handler,
        .on_ai_status = on_ai_status_handler,
        .on_ai_thinking = on_ai_thinking_handler,
        .on_ai_reply = on_ai_reply_handler,
        .on_transfer = on_transfer_handler,
    };
    return router;
}
