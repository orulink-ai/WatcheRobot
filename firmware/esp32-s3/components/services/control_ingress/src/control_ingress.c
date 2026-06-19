#include "control_ingress.h"

#include "behavior_state_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_servo.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define TAG "CTRL_INGRESS"

#define CONTROL_STATE_QUEUE_DEPTH 16
#define CONTROL_STATE_TASK_STACK 6144
#define CONTROL_STATE_TASK_PRIORITY 5
#define CONTROL_MANUAL_TOUCH_SUPPRESS_MS 10000U

typedef enum {
    CONTROL_STATE_MSG_AI_STATUS = 0,
    CONTROL_STATE_MSG_STATE_SET,
    CONTROL_STATE_MSG_STATE_TEXT,
} control_state_msg_type_t;

typedef struct {
    control_state_msg_type_t type;
    union {
        control_ai_status_request_t ai_status;
        control_state_set_request_t state_set;
        control_state_text_request_t state_text;
    } data;
} control_state_msg_t;

static QueueHandle_t s_state_queue = NULL;
static TaskHandle_t s_state_task = NULL;
static int64_t s_manual_touch_suppressed_until_us = 0;
static int64_t s_manual_jog_stream_until_us = 0;
static control_motion_source_t s_manual_jog_stream_source = CONTROL_MOTION_SOURCE_UNKNOWN;

static hal_servo_motion_source_t control_source_to_hal(control_motion_source_t source) {
    switch (source) {
    case CONTROL_MOTION_SOURCE_BEHAVIOR:
        return HAL_SERVO_MOTION_SOURCE_BEHAVIOR;
    case CONTROL_MOTION_SOURCE_BLE:
        return HAL_SERVO_MOTION_SOURCE_BLE;
    case CONTROL_MOTION_SOURCE_WS:
        return HAL_SERVO_MOTION_SOURCE_WS;
    case CONTROL_MOTION_SOURCE_RECOVERY:
        return HAL_SERVO_MOTION_SOURCE_RECOVERY;
    case CONTROL_MOTION_SOURCE_STRESS:
    case CONTROL_MOTION_SOURCE_UNKNOWN:
    default:
        return HAL_SERVO_MOTION_SOURCE_UNKNOWN;
    }
}

static esp_err_t control_default_interrupt_action(const char *source) {
    return behavior_state_interrupt_action(source);
}

static esp_err_t control_default_move_sync(int x_deg, int y_deg, int duration_ms, control_motion_source_t source,
                                          uint32_t *out_seq) {
    return hal_servo_move_sync_with_source_and_seq(x_deg, y_deg, duration_ms, control_source_to_hal(source), out_seq);
}

static esp_err_t control_default_move_axis(bool is_x_axis, int angle_deg, int duration_ms,
                                          control_motion_source_t source, uint32_t *out_seq) {
    return hal_servo_move_smooth_with_source_and_seq(is_x_axis ? SERVO_AXIS_X : SERVO_AXIS_Y, angle_deg, duration_ms,
                                                     control_source_to_hal(source), out_seq);
}

static esp_err_t control_default_jog_axis(bool is_x_axis, int velocity_deg_per_sec, int timeout_ms,
                                          control_motion_source_t source, uint32_t *out_seq) {
    return hal_servo_jog_with_source_and_seq(is_x_axis ? SERVO_AXIS_X : SERVO_AXIS_Y, velocity_deg_per_sec, timeout_ms,
                                             control_source_to_hal(source), out_seq);
}

static esp_err_t control_default_jog_vector(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec, int timeout_ms,
                                            control_motion_source_t source, uint32_t *out_seq) {
    return hal_servo_jog_vector_with_source_and_seq(x_velocity_deg_per_sec, y_velocity_deg_per_sec, timeout_ms,
                                                    control_source_to_hal(source), out_seq);
}

static esp_err_t control_default_stop(control_motion_source_t source) {
    return hal_servo_cancel_all_with_source(control_source_to_hal(source));
}

static bool control_source_is_manual(control_motion_source_t source) {
    return source == CONTROL_MOTION_SOURCE_WS || source == CONTROL_MOTION_SOURCE_BLE;
}

static void control_suppress_touch_for_manual_source(control_motion_source_t source) {
    if (control_source_is_manual(source)) {
        control_ingress_suppress_manual_touch_for_ms(CONTROL_MANUAL_TOUCH_SUPPRESS_MS);
    }
}

static bool control_begin_jog_stream(control_motion_source_t source, int timeout_ms) {
    const int64_t now_us = esp_timer_get_time();
    const bool stream_active = control_source_is_manual(source) && s_manual_jog_stream_source == source &&
                               now_us < s_manual_jog_stream_until_us;

    if (control_source_is_manual(source)) {
        s_manual_jog_stream_source = source;
        s_manual_jog_stream_until_us = now_us + ((int64_t)timeout_ms * 1000LL);
    }

    return !stream_active;
}

static void control_end_jog_stream(control_motion_source_t source) {
    if (!control_source_is_manual(source)) {
        return;
    }
    if (s_manual_jog_stream_source == source) {
        s_manual_jog_stream_until_us = 0;
        s_manual_jog_stream_source = CONTROL_MOTION_SOURCE_UNKNOWN;
    }
}

static const control_ingress_ops_t s_default_ops = {
    .interrupt_action = control_default_interrupt_action,
    .move_sync = control_default_move_sync,
    .move_axis = control_default_move_axis,
    .jog_axis = control_default_jog_axis,
    .jog_vector = control_default_jog_vector,
    .stop = control_default_stop,
};

static const control_ingress_ops_t *s_ops = &s_default_ops;

#if defined(CONTROL_INGRESS_ENABLE_TEST_API)
void control_ingress_set_ops_for_test(const control_ingress_ops_t *ops) {
    s_ops = ops != NULL ? ops : &s_default_ops;
    s_manual_touch_suppressed_until_us = 0;
    s_manual_jog_stream_until_us = 0;
    s_manual_jog_stream_source = CONTROL_MOTION_SOURCE_UNKNOWN;
}

void control_ingress_reset_ops_for_test(void) {
    s_ops = &s_default_ops;
    s_manual_touch_suppressed_until_us = 0;
    s_manual_jog_stream_until_us = 0;
    s_manual_jog_stream_source = CONTROL_MOTION_SOURCE_UNKNOWN;
}
#endif

static bool control_contains_nocase(const char *haystack, const char *needle) {
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

static void control_normalize_resource_name(const char *raw, char *out, size_t out_size) {
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
        char ch = (char)tolower((unsigned char)base[i]);
        out[i] = ch == '-' ? '_' : ch;
    }
    out[len] = '\0';

    if (strncmp(out, "watcher_", 8) == 0) {
        memmove(out, out + 8, strlen(out + 8) + 1);
    }

    len = strlen(out);
    if (len > 6 && strcmp(out + len - 6, "_10fps") == 0) {
        out[len - 6] = '\0';
    }

    if (strcmp(out, "look1") == 0) {
        snprintf(out, out_size, "standby1");
    } else if (strcmp(out, "look2") == 0) {
        snprintf(out, out_size, "standby2");
    } else if (strcmp(out, "look3") == 0) {
        snprintf(out, out_size, "standby3");
    } else if (strcmp(out, "look4") == 0) {
        snprintf(out, out_size, "standby4");
    }
}

#if defined(CONTROL_INGRESS_ENABLE_TEST_API)
void control_ingress_normalize_resource_name_for_test(const char *raw, char *out, size_t out_size) {
    control_normalize_resource_name(raw, out, out_size);
}
#endif

static bool control_append_state_candidate(const char **candidates, size_t *count, size_t max_count,
                                           const char *candidate) {
    size_t i;

    if (candidates == NULL || count == NULL || candidate == NULL || candidate[0] == '\0') {
        return false;
    }

    for (i = 0; i < *count; ++i) {
        if (strcasecmp(candidates[i], candidate) == 0) {
            return false;
        }
    }

    if (*count >= max_count) {
        return false;
    }

    candidates[*count] = candidate;
    (*count)++;
    return true;
}

static const char *control_ai_status_to_fallback(const char *status, const char *message) {
    if (control_contains_nocase(status, "bluetooth") || control_contains_nocase(message, "bluetooth") ||
        control_contains_nocase(status, "blue tooth") || control_contains_nocase(message, "blue tooth") ||
        control_contains_nocase(status, "pairing") || control_contains_nocase(message, "pairing") ||
        control_contains_nocase(status, "paired") || control_contains_nocase(message, "paired")) {
        return "bluetooth";
    }
    if (control_contains_nocase(status, "observing") || control_contains_nocase(message, "observing")) {
        return "custom3";
    }
    if (control_contains_nocase(status, "listening") || control_contains_nocase(message, "listening")) {
        return "listening";
    }
    if (control_contains_nocase(status, "thinking") || control_contains_nocase(message, "thinking")) {
        return "thinking";
    }
    if (control_contains_nocase(status, "processing") || control_contains_nocase(status, "analyzing") ||
        control_contains_nocase(message, "processing") || control_contains_nocase(message, "analyzing")) {
        return "processing";
    }
    if (control_contains_nocase(status, "speaking") || control_contains_nocase(message, "speaking")) {
        return "speaking";
    }
    if (control_contains_nocase(status, "idle") || control_contains_nocase(message, "idle")) {
        return "standby";
    }
    if (control_contains_nocase(status, "done") || control_contains_nocase(status, "completed") ||
        control_contains_nocase(message, "done") || control_contains_nocase(message, "completed")) {
        return "happy";
    }
    if (control_contains_nocase(status, "error") || control_contains_nocase(status, "fail") ||
        control_contains_nocase(message, "error") || control_contains_nocase(message, "fail")) {
        return "error";
    }

    return NULL;
}

static bool control_is_voice_flow_state(const char *state_id) {
    if (state_id == NULL || state_id[0] == '\0') {
        return false;
    }
    return strcasecmp(state_id, "listening") == 0 || strcasecmp(state_id, "thinking") == 0 ||
           strcasecmp(state_id, "processing") == 0 || strcasecmp(state_id, "speaking") == 0;
}

static bool control_ai_status_uses_voice_flow_state(const char *action_state_id, const char *status_state_id,
                                                    const char *fallback_state_id, const char *image_name) {
    return control_is_voice_flow_state(action_state_id) || control_is_voice_flow_state(status_state_id) ||
           control_is_voice_flow_state(fallback_state_id) || control_is_voice_flow_state(image_name);
}

static const char *control_ai_status_display_text(const control_ai_status_request_t *req) {
    static const char clear_text[] = "";
    char action_state_id[sizeof(req->action_file)];
    char status_state_id[sizeof(req->status)];
    char fallback_state_id[sizeof(req->status)];
    char image_name[sizeof(req->image_name)];

    if (req == NULL) {
        return NULL;
    }
    if (req->message[0] == '\0') {
        return clear_text;
    }

    control_normalize_resource_name(req->action_file, action_state_id, sizeof(action_state_id));
    control_normalize_resource_name(req->status, status_state_id, sizeof(status_state_id));
    control_normalize_resource_name(req->image_name, image_name, sizeof(image_name));
    control_normalize_resource_name(control_ai_status_to_fallback(req->status, req->message), fallback_state_id,
                                    sizeof(fallback_state_id));

    if (control_ai_status_uses_voice_flow_state(action_state_id, status_state_id, fallback_state_id, image_name)) {
        return clear_text;
    }
    return req->message;
}

#if defined(CONTROL_INGRESS_ENABLE_TEST_API)
const char *control_ingress_ai_status_text_for_test(const control_ai_status_request_t *req) {
    return control_ai_status_display_text(req);
}
#endif

static esp_err_t control_apply_ai_status(const control_ai_status_request_t *req) {
    char action_state_id[sizeof(req->action_file)];
    char status_state_id[sizeof(req->status)];
    char fallback_state_id[sizeof(req->status)];
    char image_name[sizeof(req->image_name)];
    char sound_id[sizeof(req->sound_file)];
    const char *status_sound_id = "";
    const char *state_candidates[3] = {0};
    const char *action_candidates[3] = {0};
    const char *selected_action_id = NULL;
    const char *text;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    size_t state_candidate_count = 0;
    size_t action_candidate_count = 0;
    size_t i;

    control_normalize_resource_name(req->action_file, action_state_id, sizeof(action_state_id));
    control_normalize_resource_name(req->status, status_state_id, sizeof(status_state_id));
    control_normalize_resource_name(req->image_name, image_name, sizeof(image_name));
    control_normalize_resource_name(req->sound_file, sound_id, sizeof(sound_id));
    control_normalize_resource_name(control_ai_status_to_fallback(req->status, req->message), fallback_state_id,
                                    sizeof(fallback_state_id));

    text = control_ai_status_display_text(req);
    /* Preserve explicit panel/protocol SFX while still suppressing implicit state-default
     * sounds for cloud AI statuses that do not provide a sound_file. */
    if (sound_id[0] != '\0') {
        status_sound_id = sound_id;
    }

    control_append_state_candidate(state_candidates, &state_candidate_count, 3, action_state_id);
    control_append_state_candidate(state_candidates, &state_candidate_count, 3, status_state_id);
    control_append_state_candidate(state_candidates, &state_candidate_count, 3, fallback_state_id);
    control_append_state_candidate(action_candidates, &action_candidate_count, 3, action_state_id);
    control_append_state_candidate(action_candidates, &action_candidate_count, 3, status_state_id);
    control_append_state_candidate(action_candidates, &action_candidate_count, 3, fallback_state_id);

    for (i = 0; i < action_candidate_count; ++i) {
        if (behavior_state_has_action(action_candidates[i])) {
            selected_action_id = action_candidates[i];
            break;
        }
    }

    for (i = 0; i < state_candidate_count; ++i) {
        ret = behavior_state_set_with_resources_and_action(state_candidates[i], text, 0,
                                                           image_name[0] != '\0' ? image_name : NULL, status_sound_id,
                                                           selected_action_id);
        if (ret != ESP_ERR_NOT_FOUND) {
            break;
        }
    }

    if (ret == ESP_ERR_NOT_FOUND) {
        ret = behavior_state_set_with_resources_and_action(
            "standby", text, 0, image_name[0] != '\0' ? image_name : NULL, status_sound_id, selected_action_id);
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "No local match for AI status=%s action=%s fallback=%s image=%s", req->status,
                     action_state_id[0] != '\0' ? action_state_id : "<none>",
                     fallback_state_id[0] != '\0' ? fallback_state_id : "<none>",
                     image_name[0] != '\0' ? image_name : "<none>");
        }
    }

    if (ret != ESP_OK && text != NULL) {
        ret = behavior_state_set_text(text, 0);
    }

    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "AI status apply failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void control_state_task(void *arg) {
    control_state_msg_t msg;

    (void)arg;

    while (true) {
        if (xQueueReceive(s_state_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.type) {
        case CONTROL_STATE_MSG_AI_STATUS: {
            esp_err_t ret = control_apply_ai_status(&msg.data.ai_status);
            if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "AI status task apply failed: status=%s err=%s", msg.data.ai_status.status,
                         esp_err_to_name(ret));
            }
            break;
        }

        case CONTROL_STATE_MSG_STATE_SET: {
            esp_err_t ret = behavior_state_set(msg.data.state_set.state_id);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "State set failed: state=%s err=%s", msg.data.state_set.state_id, esp_err_to_name(ret));
            }
            break;
        }

        case CONTROL_STATE_MSG_STATE_TEXT: {
            const char *text = msg.data.state_text.text[0] != '\0' ? msg.data.state_text.text : NULL;
            esp_err_t ret;

            if (msg.data.state_text.state_id[0] != '\0') {
                ret = behavior_state_set_with_resources(msg.data.state_text.state_id, text,
                                                        msg.data.state_text.font_size, NULL, "");
            } else if (text != NULL) {
                ret = behavior_state_set_text(text, msg.data.state_text.font_size);
            } else {
                ret = ESP_ERR_INVALID_ARG;
            }

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "State text apply failed: state=%s err=%s",
                         msg.data.state_text.state_id[0] != '\0' ? msg.data.state_text.state_id : "<none>",
                         esp_err_to_name(ret));
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown control state msg: %d", (int)msg.type);
            break;
        }
    }
}

static esp_err_t control_submit_state_msg(const control_state_msg_t *msg) {
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_state_queue, msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t control_ingress_init(void) {
    if (s_state_queue != NULL && s_state_task != NULL) {
        return ESP_OK;
    }

    s_state_queue = xQueueCreate(CONTROL_STATE_QUEUE_DEPTH, sizeof(control_state_msg_t));
    if (s_state_queue == NULL) {
        ESP_LOGE(TAG, "state queue create failed");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(control_state_task, "control_state", CONTROL_STATE_TASK_STACK, NULL, CONTROL_STATE_TASK_PRIORITY,
                    &s_state_task) != pdPASS) {
        vQueueDelete(s_state_queue);
        s_state_queue = NULL;
        s_state_task = NULL;
        ESP_LOGE(TAG, "state task create failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t control_ingress_submit_servo(const control_servo_request_t *req) {
    return control_ingress_submit_servo_with_seq(req, NULL);
}

esp_err_t control_ingress_submit_jog(const control_jog_request_t *req) {
    return control_ingress_submit_jog_with_seq(req, NULL);
}

esp_err_t control_ingress_submit_jog_vector(const control_jog_vector_request_t *req) {
    return control_ingress_submit_jog_vector_with_seq(req, NULL);
}

esp_err_t control_ingress_submit_servo_with_seq(const control_servo_request_t *req, uint32_t *out_seq) {
    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->duration_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!req->has_x && !req->has_y) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->has_x && (req->x_deg < 0 || req->x_deg > 180)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->has_y && (req->y_deg < 0 || req->y_deg > 180)) {
        return ESP_ERR_INVALID_ARG;
    }

    control_end_jog_stream(req->source);
    control_suppress_touch_for_manual_source(req->source);

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    esp_err_t interrupt_ret;

    if (s_ops->interrupt_action != NULL) {
        interrupt_ret = s_ops->interrupt_action("control_ingress_servo");
        if (interrupt_ret != ESP_OK && interrupt_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to interrupt action loop before servo control: %s", esp_err_to_name(interrupt_ret));
        }
    }
#endif

    if (req->has_x && req->has_y) {
        if (s_ops->move_sync == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        return s_ops->move_sync(req->x_deg, req->y_deg, req->duration_ms, req->source, out_seq);
    }

    if (s_ops->move_axis == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_ops->move_axis(req->has_x, req->has_x ? req->x_deg : req->y_deg, req->duration_ms, req->source, out_seq);
}

esp_err_t control_ingress_submit_jog_with_seq(const control_jog_request_t *req, uint32_t *out_seq) {
    bool should_interrupt;

    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->velocity_deg_per_sec == 0 || req->velocity_deg_per_sec < -180 || req->velocity_deg_per_sec > 180 ||
        req->timeout_ms <= 0 || req->timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }

    control_suppress_touch_for_manual_source(req->source);
    should_interrupt = control_begin_jog_stream(req->source, req->timeout_ms);

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    if (should_interrupt && s_ops->interrupt_action != NULL) {
        esp_err_t interrupt_ret = s_ops->interrupt_action("control_ingress_jog");
        if (interrupt_ret != ESP_OK && interrupt_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to interrupt action loop before jog control: %s", esp_err_to_name(interrupt_ret));
        }
    }
#endif

    if (s_ops->jog_axis == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_ops->jog_axis(req->is_x_axis, req->velocity_deg_per_sec, req->timeout_ms, req->source, out_seq);
}

esp_err_t control_ingress_submit_jog_vector_with_seq(const control_jog_vector_request_t *req, uint32_t *out_seq) {
    bool should_interrupt;

    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!req->has_x && !req->has_y) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->timeout_ms <= 0 || req->timeout_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((req->has_x && (req->x_velocity_deg_per_sec == 0 || req->x_velocity_deg_per_sec < -180 ||
                        req->x_velocity_deg_per_sec > 180)) ||
        (req->has_y && (req->y_velocity_deg_per_sec == 0 || req->y_velocity_deg_per_sec < -180 ||
                        req->y_velocity_deg_per_sec > 180))) {
        return ESP_ERR_INVALID_ARG;
    }

    control_suppress_touch_for_manual_source(req->source);
    should_interrupt = control_begin_jog_stream(req->source, req->timeout_ms);

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    if (should_interrupt && s_ops->interrupt_action != NULL) {
        esp_err_t interrupt_ret = s_ops->interrupt_action("control_ingress_jog");
        if (interrupt_ret != ESP_OK && interrupt_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to interrupt action loop before jog control: %s", esp_err_to_name(interrupt_ret));
        }
    }
#endif

    if (s_ops->jog_vector == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_ops->jog_vector(req->has_x ? req->x_velocity_deg_per_sec : 0,
                             req->has_y ? req->y_velocity_deg_per_sec : 0, req->timeout_ms, req->source, out_seq);
}

esp_err_t control_ingress_stop_manual(control_motion_source_t source) {
    control_end_jog_stream(source);
    control_suppress_touch_for_manual_source(source);

    if (s_ops->interrupt_action != NULL) {
        esp_err_t interrupt_ret = s_ops->interrupt_action("control_ingress_stop");
        if (interrupt_ret != ESP_OK && interrupt_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to interrupt action loop before stop control: %s", esp_err_to_name(interrupt_ret));
        }
    }

    if (s_ops->stop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_ops->stop(source);
}

void control_ingress_suppress_manual_touch_for_ms(uint32_t duration_ms) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t requested_until_us = now_us + ((int64_t)duration_ms * 1000);

    if (requested_until_us > s_manual_touch_suppressed_until_us) {
        s_manual_touch_suppressed_until_us = requested_until_us;
    }
}

bool control_ingress_is_manual_touch_suppressed(void) {
    return esp_timer_get_time() < s_manual_touch_suppressed_until_us;
}

uint32_t control_ingress_manual_touch_remaining_ms(void) {
    const int64_t remaining_us = s_manual_touch_suppressed_until_us - esp_timer_get_time();

    if (remaining_us <= 0) {
        return 0;
    }
    return (uint32_t)((remaining_us + 999) / 1000);
}

esp_err_t control_ingress_submit_ai_status(const control_ai_status_request_t *req) {
    control_state_msg_t msg = {0};

    if (req == NULL || req->status[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    msg.type = CONTROL_STATE_MSG_AI_STATUS;
    msg.data.ai_status = *req;
    return control_submit_state_msg(&msg);
}

esp_err_t control_ingress_submit_state_set(const control_state_set_request_t *req) {
    control_state_msg_t msg = {0};

    if (req == NULL || req->state_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    msg.type = CONTROL_STATE_MSG_STATE_SET;
    msg.data.state_set = *req;
    return control_submit_state_msg(&msg);
}

esp_err_t control_ingress_submit_state_text(const control_state_text_request_t *req) {
    control_state_msg_t msg = {0};

    if (req == NULL || (req->state_id[0] == '\0' && req->text[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }

    msg.type = CONTROL_STATE_MSG_STATE_TEXT;
    msg.data.state_text = *req;
    return control_submit_state_msg(&msg);
}
