#include "watcher_sdk.h"

#include "animation_registry.h"
#include "behavior_state_service.h"
#include "camera_service.h"
#include "control_ingress.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mcu_led_service.h"
#include "mcu_motion_service.h"
#include "sfx_service.h"
#include "voice_service.h"
#include "watcher_sdk_motion_tracker.h"

#include <stdlib.h>
#include <string.h>

#define WATCHER_SDK_COMPLETION_GRACE_MS 20U
#define WATCHER_SDK_MOTION_EVENT_CAPACITY 8U
#define WATCHER_SDK_CAMERA_TASK_STACK 4096U
#define WATCHER_SDK_CAMERA_TASK_PRIORITY 4U
#define WATCHER_SDK_CAMERA_DEFAULT_WIDTH 640
#define WATCHER_SDK_CAMERA_DEFAULT_HEIGHT 480
#define WATCHER_SDK_CAMERA_WARM_UP_FRAMES 2
#define WATCHER_SDK_CAMERA_WARM_UP_SETTLE_MS 500

static const char *TAG = "WATCHER_SDK";

struct watcher_sdk_context {
    watcher_sdk_core_t core;
    char app_id[WATCHER_SDK_APP_ID_MAX];
    watcher_sdk_job_id_t behavior_job_id;
    watcher_sdk_job_id_t animation_job_id;
    watcher_sdk_job_id_t audio_job_id;
    watcher_sdk_job_id_t action_job_id;
    char behavior_id[64];
    uint16_t behavior_repeat_remaining;
    uint32_t behavior_started_ms;
    uint32_t animation_started_ms;
    uint32_t audio_started_ms;
    uint32_t action_started_ms;
    watcher_sdk_session_id_t next_session_id;
    watcher_sdk_session_id_t microphone_session_id;
    bool microphone_audio_release_pending;
    watcher_sdk_session_id_t camera_session_id;
    watcher_microphone_config_t microphone;
    watcher_camera_capture_config_t camera;
    TaskHandle_t camera_task;
    SemaphoreHandle_t camera_capture_done;
    watcher_sdk_motion_tracker_t motion_tracker;
    bool camera_initialized;
    bool open;
};

static watcher_sdk_context_t *s_active_context = NULL;
static mcu_motion_lifecycle_event_t s_motion_events[WATCHER_SDK_MOTION_EVENT_CAPACITY];
static size_t s_motion_event_head;
static size_t s_motion_event_count;
static uint32_t s_motion_event_dropped;
static portMUX_TYPE s_motion_event_lock = portMUX_INITIALIZER_UNLOCKED;

static void sdk_motion_event_reset(void) {
    portENTER_CRITICAL(&s_motion_event_lock);
    s_motion_event_head = 0U;
    s_motion_event_count = 0U;
    s_motion_event_dropped = 0U;
    portEXIT_CRITICAL(&s_motion_event_lock);
}

static void sdk_motion_lifecycle_callback(const mcu_motion_lifecycle_event_t *event, void *ctx) {
    size_t tail;

    (void)ctx;
    if (event == NULL || event->type == MCU_MOTION_LIFECYCLE_ACKED) {
        return;
    }
    portENTER_CRITICAL(&s_motion_event_lock);
    if (s_motion_event_count == WATCHER_SDK_MOTION_EVENT_CAPACITY) {
        s_motion_event_head = (s_motion_event_head + 1U) % WATCHER_SDK_MOTION_EVENT_CAPACITY;
        s_motion_event_count--;
        s_motion_event_dropped++;
    }
    tail = (s_motion_event_head + s_motion_event_count) % WATCHER_SDK_MOTION_EVENT_CAPACITY;
    s_motion_events[tail] = *event;
    s_motion_event_count++;
    portEXIT_CRITICAL(&s_motion_event_lock);
}

static bool sdk_motion_event_pop(mcu_motion_lifecycle_event_t *out_event) {
    bool has_event = false;

    if (out_event == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_motion_event_lock);
    if (s_motion_event_count > 0U) {
        *out_event = s_motion_events[s_motion_event_head];
        s_motion_event_head = (s_motion_event_head + 1U) % WATCHER_SDK_MOTION_EVENT_CAPACITY;
        s_motion_event_count--;
        has_event = true;
    }
    portEXIT_CRITICAL(&s_motion_event_lock);
    return has_event;
}

static uint32_t sdk_motion_event_take_dropped(void) {
    uint32_t dropped;

    portENTER_CRITICAL(&s_motion_event_lock);
    dropped = s_motion_event_dropped;
    s_motion_event_dropped = 0U;
    portEXIT_CRITICAL(&s_motion_event_lock);
    return dropped;
}

static void sdk_clear_motion_tracking(watcher_sdk_context_t *context) {
    if (context != NULL) {
        watcher_sdk_motion_tracker_clear(&context->motion_tracker);
    }
}

static uint32_t sdk_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static watcher_sdk_session_id_t sdk_next_session_id(watcher_sdk_context_t *context) {
    watcher_sdk_session_id_t id = context->next_session_id++;
    if (context->next_session_id == WATCHER_SDK_SESSION_INVALID) {
        context->next_session_id = 1U;
    }
    return id == WATCHER_SDK_SESSION_INVALID ? sdk_next_session_id(context) : id;
}

static watcher_sdk_result_t sdk_from_esp_err(esp_err_t error) {
    if (error == ESP_OK) {
        return WATCHER_SDK_RESULT_OK;
    }
    if (error == ESP_ERR_INVALID_ARG) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (error == ESP_ERR_INVALID_STATE) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    if (error == ESP_ERR_TIMEOUT) {
        return WATCHER_SDK_RESULT_BUSY;
    }
    if (error == ESP_ERR_NOT_FOUND) {
        return WATCHER_SDK_RESULT_NOT_FOUND;
    }
    return WATCHER_SDK_RESULT_EXECUTOR_FAILED;
}

static mcu_led_zone_t sdk_light_zone(const char *zone) {
    if (zone != NULL && strcmp(zone, "side") == 0) {
        return MCU_LED_ZONE_SIDE;
    }
    if (zone != NULL && strcmp(zone, "bottom") == 0) {
        return MCU_LED_ZONE_BOTTOM;
    }
    return MCU_LED_ZONE_ALL;
}

static uint8_t sdk_brightness(uint8_t percent) {
    uint8_t clamped = percent > 100U ? 100U : percent;
    return (uint8_t)(((uint16_t)clamped * UINT8_MAX + 50U) / 100U);
}

static esp_err_t sdk_light_submit_off(void) {
    const mcu_led_request_t request = {.mode = MCU_LED_MODE_OFF, .zone = MCU_LED_ZONE_ALL};
    return mcu_led_submit(&request);
}

static void sdk_cancel_domain(watcher_sdk_domain_t domain, void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;

    switch (domain) {
    case WATCHER_SDK_DOMAIN_BEHAVIOR:
    case WATCHER_SDK_DOMAIN_ANIMATION:
        (void)behavior_state_cancel();
        break;
    case WATCHER_SDK_DOMAIN_MOTION:
        (void)control_ingress_stop_manual(CONTROL_MOTION_SOURCE_SDK);
        break;
    case WATCHER_SDK_DOMAIN_AUDIO:
        sfx_service_stop();
        break;
    case WATCHER_SDK_DOMAIN_LIGHT:
        (void)sdk_light_submit_off();
        break;
    case WATCHER_SDK_DOMAIN_MICROPHONE:
        if (context != NULL && context->microphone_session_id != WATCHER_SDK_SESSION_INVALID) {
            (void)voice_recorder_request_close();
            context->microphone_audio_release_pending = true;
        }
        break;
    case WATCHER_SDK_DOMAIN_CAMERA:
        if (camera_service_is_streaming()) {
            (void)camera_service_stop_stream();
        }
        break;
    case WATCHER_SDK_DOMAIN_NONE:
    case WATCHER_SDK_DOMAIN_COUNT:
    default:
        break;
    }
}

static bool sdk_context_is_open(const watcher_sdk_context_t *context) {
    return context != NULL && context->open;
}

static void sdk_camera_frame(const uint8_t *jpeg, size_t size, uint32_t timestamp_ms, void *user_context);

static esp_err_t sdk_camera_prepare(watcher_sdk_context_t *context) {
    int applied_width = 0;
    int applied_height = 0;
    esp_err_t error;

    if (context->camera_initialized) {
        return camera_service_configure(context->camera.width, context->camera.height, context->camera.quality,
                                        &applied_width, &applied_height);
    }

    error = camera_service_init();
    if (error == ESP_OK) {
        error = camera_service_configure(context->camera.width, context->camera.height, context->camera.quality,
                                         &applied_width, &applied_height);
    }
    for (int frame_index = 0;
         error == ESP_OK && frame_index < WATCHER_SDK_CAMERA_WARM_UP_FRAMES;
         ++frame_index) {
        error = camera_service_capture_once();
        if (error == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(WATCHER_SDK_CAMERA_WARM_UP_SETTLE_MS));
        }
    }
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "camera cold-start warm-up frames discarded: %d", WATCHER_SDK_CAMERA_WARM_UP_FRAMES);
        error = camera_service_register_frame_callback(sdk_camera_frame, context);
    }
    if (error != ESP_OK) {
        (void)camera_service_deinit();
        return error;
    }

    context->camera_initialized = true;
    return ESP_OK;
}

static void sdk_camera_capture_task(void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;
    esp_err_t error;

    if (context == NULL) {
        vTaskDelete(NULL);
        return;
    }
    error = sdk_camera_prepare(context);
    if (error == ESP_OK) {
        error = camera_service_capture_once();
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "camera capture session %lu failed: %s",
                 (unsigned long)context->camera_session_id, esp_err_to_name(error));
    }

    context->camera_session_id = WATCHER_SDK_SESSION_INVALID;
    context->camera_task = NULL;
    if (context->camera_capture_done != NULL) {
        /* This give is the task's final context access; close waits on it before freeing context. */
        xSemaphoreGive(context->camera_capture_done);
    }
    vTaskDelete(NULL);
}

watcher_sdk_result_t watcher_sdk_open(const watcher_sdk_config_t *config, watcher_sdk_context_t **out_context) {
    watcher_sdk_context_t *context;
    watcher_sdk_core_config_t core_config;

    if (config == NULL || config->app_id == NULL || config->app_id[0] == '\0' || out_context == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (s_active_context != NULL) {
        return WATCHER_SDK_RESULT_BUSY;
    }
    context = (watcher_sdk_context_t *)calloc(1U, sizeof(*context));
    if (context == NULL) {
        return WATCHER_SDK_RESULT_NO_CAPACITY;
    }
    core_config.cancel_domain = sdk_cancel_domain;
    core_config.executor_context = context;
    if (watcher_sdk_core_init(&context->core, &core_config) != WATCHER_SDK_RESULT_OK) {
        free(context);
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    strncpy(context->app_id, config->app_id, sizeof(context->app_id) - 1U);
    context->next_session_id = 1U;
    watcher_sdk_motion_tracker_init(&context->motion_tracker);
    context->camera_capture_done = xSemaphoreCreateBinary();
    if (context->camera_capture_done == NULL) {
        watcher_sdk_core_deinit(&context->core);
        free(context);
        return WATCHER_SDK_RESULT_NO_CAPACITY;
    }
    xSemaphoreGive(context->camera_capture_done);
    sdk_motion_event_reset();
    if (mcu_motion_set_lifecycle_callback(sdk_motion_lifecycle_callback, NULL) != ESP_OK) {
        vSemaphoreDelete(context->camera_capture_done);
        watcher_sdk_core_deinit(&context->core);
        free(context);
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    context->open = true;
    s_active_context = context;
    *out_context = context;
    return WATCHER_SDK_RESULT_OK;
}

void watcher_sdk_close(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return;
    }
    context->open = false;
    if (context->camera_capture_done != NULL) {
        (void)xSemaphoreTake(context->camera_capture_done, portMAX_DELAY);
    }
    (void)mcu_motion_set_lifecycle_callback(NULL, NULL);
    sdk_motion_event_reset();
    sdk_clear_motion_tracking(context);
    watcher_sdk_core_cancel_all(&context->core);
    if (context->microphone_session_id != WATCHER_SDK_SESSION_INVALID) {
        (void)voice_recorder_request_close();
        voice_recorder_reset_transport();
    }
    sfx_service_set_voice_audio_busy(false);
    if (context->camera_initialized) {
        (void)camera_service_unregister_frame_callback();
        (void)camera_service_deinit();
    }
    if (context->camera_capture_done != NULL) {
        vSemaphoreDelete(context->camera_capture_done);
        context->camera_capture_done = NULL;
    }
    watcher_sdk_core_deinit(&context->core);
    if (s_active_context == context) {
        s_active_context = NULL;
    }
    free(context);
}

static bool sdk_elapsed(uint32_t now_ms, uint32_t started_ms) {
    return (uint32_t)(now_ms - started_ms) >= WATCHER_SDK_COMPLETION_GRACE_MS;
}

static watcher_sdk_motion_signal_t sdk_motion_signal_from_event(const mcu_motion_lifecycle_event_t *event) {
    if (event->type == MCU_MOTION_LIFECYCLE_ACKED) {
        return WATCHER_SDK_MOTION_SIGNAL_ACKED;
    }
    if (event->type == MCU_MOTION_LIFECYCLE_REJECTED) {
        return WATCHER_SDK_MOTION_SIGNAL_REJECTED;
    }
    if (event->type == MCU_MOTION_LIFECYCLE_FAULT) {
        return WATCHER_SDK_MOTION_SIGNAL_FAULT;
    }
    switch (event->result) {
    case MCU_MOTION_RESULT_SUCCESS:
        return WATCHER_SDK_MOTION_SIGNAL_SUCCESS;
    case MCU_MOTION_RESULT_STOPPED:
        return WATCHER_SDK_MOTION_SIGNAL_STOPPED;
    case MCU_MOTION_RESULT_INTERRUPTED:
        return WATCHER_SDK_MOTION_SIGNAL_INTERRUPTED;
    case MCU_MOTION_RESULT_FAULT:
    default:
        return WATCHER_SDK_MOTION_SIGNAL_FAULT;
    }
}

static int sdk_motion_failure_code(const mcu_motion_lifecycle_event_t *event) {
    if (event->type == MCU_MOTION_LIFECYCLE_REJECTED && event->reason != 0U) {
        return (int)event->reason;
    }
    if (event->type == MCU_MOTION_LIFECYCLE_FAULT && event->fault_code != 0U) {
        return (int)event->fault_code;
    }
    return ESP_FAIL;
}

static void sdk_process_motion_events(watcher_sdk_context_t *context, uint32_t now_ms) {
    mcu_motion_lifecycle_event_t event;

    while (sdk_motion_event_pop(&event)) {
        watcher_sdk_job_id_t job_id = watcher_sdk_motion_tracker_job_id(&context->motion_tracker);
        watcher_sdk_motion_outcome_t outcome = watcher_sdk_motion_tracker_on_signal(
            &context->motion_tracker, event.ref_seq, sdk_motion_signal_from_event(&event));

        if (outcome == WATCHER_SDK_MOTION_OUTCOME_COMPLETED) {
            (void)watcher_sdk_core_complete(&context->core, job_id);
        } else if (outcome == WATCHER_SDK_MOTION_OUTCOME_CANCELLED) {
            (void)watcher_sdk_core_cancel_observed(&context->core, job_id);
        } else if (outcome == WATCHER_SDK_MOTION_OUTCOME_FAILED) {
            (void)watcher_sdk_core_fail(&context->core, job_id, sdk_motion_failure_code(&event));
        }
    }
    if (sdk_motion_event_take_dropped() > 0U &&
        watcher_sdk_motion_tracker_is_active(&context->motion_tracker)) {
        watcher_sdk_job_id_t job_id = watcher_sdk_motion_tracker_job_id(&context->motion_tracker);
        sdk_clear_motion_tracking(context);
        (void)watcher_sdk_core_fail(&context->core, job_id, ESP_ERR_NO_MEM);
    }
    if (watcher_sdk_motion_tracker_is_active(&context->motion_tracker)) {
        watcher_sdk_job_id_t job_id = watcher_sdk_motion_tracker_job_id(&context->motion_tracker);
        if (watcher_sdk_motion_tracker_poll_timeout(&context->motion_tracker, now_ms)) {
            (void)watcher_sdk_core_fail(&context->core, job_id, ESP_ERR_TIMEOUT);
        }
    }
}

void watcher_sdk_tick(watcher_sdk_context_t *context) {
    behavior_animation_event_t animation_event;
    uint32_t now_ms;

    if (!sdk_context_is_open(context)) {
        return;
    }
    now_ms = sdk_now_ms();
    watcher_sdk_core_tick(&context->core, now_ms);
    sdk_process_motion_events(context, now_ms);

    if (context->microphone_audio_release_pending && voice_recorder_get_state() != VOICE_STATE_RECORDING) {
        sfx_service_set_voice_audio_busy(false);
        context->microphone_audio_release_pending = false;
    }

    if (context->behavior_job_id != WATCHER_SDK_JOB_INVALID &&
        watcher_sdk_core_get_state(&context->core, context->behavior_job_id) >= WATCHER_SDK_JOB_COMPLETED) {
        context->behavior_job_id = WATCHER_SDK_JOB_INVALID;
        context->behavior_repeat_remaining = 0U;
    }
    if (context->animation_job_id != WATCHER_SDK_JOB_INVALID &&
        watcher_sdk_core_get_state(&context->core, context->animation_job_id) >= WATCHER_SDK_JOB_COMPLETED) {
        context->animation_job_id = WATCHER_SDK_JOB_INVALID;
    }
    if (context->audio_job_id != WATCHER_SDK_JOB_INVALID &&
        watcher_sdk_core_get_state(&context->core, context->audio_job_id) >= WATCHER_SDK_JOB_COMPLETED) {
        context->audio_job_id = WATCHER_SDK_JOB_INVALID;
    }
    if (context->action_job_id != WATCHER_SDK_JOB_INVALID &&
        watcher_sdk_core_get_state(&context->core, context->action_job_id) >= WATCHER_SDK_JOB_COMPLETED) {
        context->action_job_id = WATCHER_SDK_JOB_INVALID;
    }

    if (context->behavior_job_id != WATCHER_SDK_JOB_INVALID && sdk_elapsed(now_ms, context->behavior_started_ms) &&
        !behavior_state_is_busy()) {
        if (context->behavior_repeat_remaining > 1U && behavior_state_set(context->behavior_id) == ESP_OK) {
            context->behavior_repeat_remaining--;
            context->behavior_started_ms = now_ms;
        } else {
            (void)watcher_sdk_core_complete(&context->core, context->behavior_job_id);
            context->behavior_job_id = WATCHER_SDK_JOB_INVALID;
            context->behavior_repeat_remaining = 0U;
        }
    }
    if (context->audio_job_id != WATCHER_SDK_JOB_INVALID && sdk_elapsed(now_ms, context->audio_started_ms) &&
        !sfx_service_is_busy()) {
        (void)watcher_sdk_core_complete(&context->core, context->audio_job_id);
        context->audio_job_id = WATCHER_SDK_JOB_INVALID;
    }
    if (context->action_job_id != WATCHER_SDK_JOB_INVALID && sdk_elapsed(now_ms, context->action_started_ms) &&
        !behavior_state_is_action_active()) {
        (void)watcher_sdk_core_complete(&context->core, context->action_job_id);
        context->action_job_id = WATCHER_SDK_JOB_INVALID;
    }

    while (behavior_state_poll_animation_event(&animation_event)) {
        animation_event_type_t event_type = animation_event.event.type;
        if (context->animation_job_id != WATCHER_SDK_JOB_INVALID) {
            if (event_type == ANIMATION_EVENT_COMPLETED) {
                (void)watcher_sdk_core_complete(&context->core, context->animation_job_id);
                context->animation_job_id = WATCHER_SDK_JOB_INVALID;
            } else if (event_type == ANIMATION_EVENT_FAILED) {
                (void)watcher_sdk_core_fail(&context->core, context->animation_job_id,
                                            (int)animation_event.event.failure);
                context->animation_job_id = WATCHER_SDK_JOB_INVALID;
            } else if (event_type == ANIMATION_EVENT_CANCELLED || event_type == ANIMATION_EVENT_PREEMPTED) {
                (void)watcher_sdk_core_cancel(&context->core, context->animation_job_id);
                context->animation_job_id = WATCHER_SDK_JOB_INVALID;
            }
        }
        if (context->behavior_job_id != WATCHER_SDK_JOB_INVALID &&
            event_type == ANIMATION_EVENT_COMPLETED &&
            strcmp(animation_event.state_id, context->behavior_id) == 0) {
            if (context->behavior_repeat_remaining > 1U &&
                behavior_state_set(context->behavior_id) == ESP_OK) {
                context->behavior_repeat_remaining--;
                context->behavior_started_ms = now_ms;
            } else {
                (void)watcher_sdk_core_complete(&context->core, context->behavior_job_id);
                context->behavior_job_id = WATCHER_SDK_JOB_INVALID;
                context->behavior_repeat_remaining = 0U;
            }
        }
        if (context->behavior_job_id != WATCHER_SDK_JOB_INVALID && event_type == ANIMATION_EVENT_FAILED) {
            (void)watcher_sdk_core_fail(&context->core, context->behavior_job_id,
                                        (int)animation_event.event.failure);
            context->behavior_job_id = WATCHER_SDK_JOB_INVALID;
        }
    }
}

bool watcher_sdk_poll_event(watcher_sdk_context_t *context, watcher_sdk_event_t *out_event) {
    if (!sdk_context_is_open(context)) {
        return false;
    }
    watcher_sdk_tick(context);
    return watcher_sdk_core_poll_event(&context->core, out_event);
}

watcher_sdk_job_state_t watcher_job_get_state(watcher_sdk_context_t *context, watcher_sdk_job_id_t job_id) {
    return sdk_context_is_open(context) ? watcher_sdk_core_get_state(&context->core, job_id)
                                        : WATCHER_SDK_JOB_UNKNOWN;
}

watcher_sdk_result_t watcher_job_cancel(watcher_sdk_context_t *context, watcher_sdk_job_id_t job_id) {
    watcher_sdk_result_t result;

    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    result = watcher_sdk_core_cancel(&context->core, job_id);
    if (result == WATCHER_SDK_RESULT_OK &&
        watcher_sdk_motion_tracker_job_id(&context->motion_tracker) == job_id) {
        sdk_clear_motion_tracking(context);
    }
    return result;
}

watcher_sdk_result_t watcher_behavior_play(watcher_sdk_context_t *context, const char *behavior_id,
                                           uint16_t repeat_count, watcher_sdk_job_id_t *out_job_id) {
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    esp_err_t error;

    if (!sdk_context_is_open(context) || behavior_id == NULL || behavior_id[0] == '\0' || out_job_id == NULL ||
        repeat_count == 0U) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (!behavior_state_has_state(behavior_id)) {
        return WATCHER_SDK_RESULT_NOT_FOUND;
    }
    result = watcher_sdk_core_start_behavior(&context->core, sdk_now_ms(), &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    error = behavior_state_set(behavior_id);
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    context->behavior_job_id = job_id;
    strncpy(context->behavior_id, behavior_id, sizeof(context->behavior_id) - 1U);
    context->behavior_repeat_remaining = repeat_count;
    context->behavior_started_ms = sdk_now_ms();
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_behavior_stop(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_cancel_domain(&context->core, WATCHER_SDK_DOMAIN_BEHAVIOR);
    context->behavior_job_id = WATCHER_SDK_JOB_INVALID;
    context->behavior_repeat_remaining = 0U;
    return sdk_from_esp_err(behavior_state_cancel());
}

watcher_sdk_result_t watcher_animation_play(watcher_sdk_context_t *context, const char *animation_id,
                                            watcher_sdk_job_id_t *out_job_id) {
    emoji_anim_type_t animation_type;
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    esp_err_t error;

    if (!sdk_context_is_open(context) || animation_id == NULL || animation_id[0] == '\0' || out_job_id == NULL ||
        !animation_registry_type_from_name(animation_id, &animation_type)) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    (void)animation_type;
    result = watcher_sdk_core_start_direct(&context->core, WATCHER_SDK_DOMAIN_ANIMATION, sdk_now_ms(), 0U, &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    error = behavior_state_set_with_resources("creator_mode", "", 0, animation_id, "");
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    context->animation_job_id = job_id;
    context->animation_started_ms = sdk_now_ms();
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_animation_stop(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_cancel_domain(&context->core, WATCHER_SDK_DOMAIN_ANIMATION);
    context->animation_job_id = WATCHER_SDK_JOB_INVALID;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_motion_move_to(watcher_sdk_context_t *context, const watcher_motion_target_t *target,
                                            watcher_sdk_job_id_t *out_job_id) {
    control_servo_request_t request = {0};
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    uint32_t command_seq = 0U;
    uint32_t now_ms;
    esp_err_t error;

    if (!sdk_context_is_open(context) || target == NULL || out_job_id == NULL || target->duration_ms == 0U) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    now_ms = sdk_now_ms();
    result = watcher_sdk_core_start_direct(&context->core, WATCHER_SDK_DOMAIN_MOTION, now_ms, 0U, &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    request.has_x = true;
    request.has_y = true;
    request.x_deg = target->pan_deg;
    request.y_deg = target->tilt_deg;
    request.duration_ms = (int)target->duration_ms;
    request.source = CONTROL_MOTION_SOURCE_SDK;
    error = control_ingress_submit_servo_with_seq(&request, &command_seq);
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    watcher_sdk_motion_tracker_bind(&context->motion_tracker, command_seq, job_id, now_ms,
                                    target->duration_ms);
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_motion_set_target(watcher_sdk_context_t *context, bool has_pan, int pan_deg,
                                               bool has_tilt, int tilt_deg) {
    control_servo_request_t request = {
        .has_x = has_pan,
        .has_y = has_tilt,
        .x_deg = pan_deg,
        .y_deg = tilt_deg,
        .duration_ms = 1,
        .source = CONTROL_MOTION_SOURCE_SDK,
    };
    watcher_sdk_result_t result;
    esp_err_t error;

    if (!sdk_context_is_open(context) || (!has_pan && !has_tilt)) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    result = watcher_sdk_core_preempt_direct(&context->core, WATCHER_SDK_DOMAIN_MOTION);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    sdk_clear_motion_tracking(context);
    error = control_ingress_submit_servo(&request);
    if (error != ESP_OK) {
        return sdk_from_esp_err(error);
    }
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_motion_play_action(watcher_sdk_context_t *context, const char *action_id,
                                                watcher_sdk_job_id_t *out_job_id) {
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    esp_err_t error;

    if (!sdk_context_is_open(context) || action_id == NULL || action_id[0] == '\0' || out_job_id == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (!behavior_state_has_action(action_id)) {
        return WATCHER_SDK_RESULT_NOT_FOUND;
    }
    result = watcher_sdk_core_start_direct(&context->core, WATCHER_SDK_DOMAIN_MOTION, sdk_now_ms(), 0U, &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    error = behavior_state_set_with_resources_and_action("creator_mode", "", 0, "", "", action_id);
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    context->action_job_id = job_id;
    context->action_started_ms = sdk_now_ms();
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_motion_stop(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_cancel_domain(&context->core, WATCHER_SDK_DOMAIN_MOTION);
    sdk_clear_motion_tracking(context);
    context->action_job_id = WATCHER_SDK_JOB_INVALID;
    return sdk_from_esp_err(control_ingress_stop_manual(CONTROL_MOTION_SOURCE_SDK));
}

watcher_sdk_result_t watcher_audio_play(watcher_sdk_context_t *context, const char *sound_id,
                                        watcher_sdk_job_id_t *out_job_id) {
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    esp_err_t error;

    if (!sdk_context_is_open(context) || sound_id == NULL || sound_id[0] == '\0' || out_job_id == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    result = watcher_sdk_core_start_direct(&context->core, WATCHER_SDK_DOMAIN_AUDIO, sdk_now_ms(), 0U, &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    error = sfx_service_play(sound_id);
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    context->audio_job_id = job_id;
    context->audio_started_ms = sdk_now_ms();
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_audio_stop(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_cancel_domain(&context->core, WATCHER_SDK_DOMAIN_AUDIO);
    context->audio_job_id = WATCHER_SDK_JOB_INVALID;
    sfx_service_stop();
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_light_set_color(watcher_sdk_context_t *context, const watcher_light_color_t *color) {
    mcu_led_request_t request = {0};

    if (!sdk_context_is_open(context) || color == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (watcher_sdk_core_preempt_direct(&context->core, WATCHER_SDK_DOMAIN_LIGHT) != WATCHER_SDK_RESULT_OK) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    request.mode = MCU_LED_MODE_STATIC;
    request.zone = sdk_light_zone(color->zone);
    request.primary_red = color->red;
    request.primary_green = color->green;
    request.primary_blue = color->blue;
    request.brightness = sdk_brightness(color->brightness_percent);
    return sdk_from_esp_err(mcu_led_submit(&request));
}

watcher_sdk_result_t watcher_light_play_effect(watcher_sdk_context_t *context,
                                               const watcher_light_effect_options_t *options,
                                               watcher_sdk_job_id_t *out_job_id) {
    mcu_led_request_t request = {0};
    watcher_sdk_job_id_t job_id;
    watcher_sdk_result_t result;
    uint32_t duration_ms = 0U;
    esp_err_t error;

    if (!sdk_context_is_open(context) || options == NULL || options->effect == NULL || out_job_id == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (strcmp(options->effect, "blink") == 0) {
        request.effect_id = MCU_LED_EFFECT_BLINK;
    } else if (strcmp(options->effect, "breathing") == 0) {
        request.effect_id = MCU_LED_EFFECT_BREATHING;
    } else if (strcmp(options->effect, "rainbow") == 0) {
        request.effect_id = MCU_LED_EFFECT_RAINBOW;
    } else if (strcmp(options->effect, "status_pulse") == 0) {
        request.effect_id = MCU_LED_EFFECT_STATUS_PULSE;
    } else {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (options->repeat_count > 0U) {
        duration_ms = (uint32_t)options->period_ms * options->repeat_count;
    }
    result = watcher_sdk_core_start_direct(&context->core, WATCHER_SDK_DOMAIN_LIGHT, sdk_now_ms(), duration_ms,
                                           &job_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    request.mode = MCU_LED_MODE_EFFECT;
    request.zone = sdk_light_zone(options->color.zone);
    request.primary_red = options->color.red;
    request.primary_green = options->color.green;
    request.primary_blue = options->color.blue;
    request.brightness = sdk_brightness(options->color.brightness_percent);
    request.period_ms = options->period_ms;
    request.repeat_count = options->repeat_count;
    error = mcu_led_submit(&request);
    if (error != ESP_OK) {
        (void)watcher_sdk_core_fail(&context->core, job_id, error);
        return sdk_from_esp_err(error);
    }
    *out_job_id = job_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_light_off(watcher_sdk_context_t *context) {
    if (!sdk_context_is_open(context)) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_cancel_domain(&context->core, WATCHER_SDK_DOMAIN_LIGHT);
    return sdk_from_esp_err(sdk_light_submit_off());
}

static bool sdk_microphone_transport_ready(void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;
    return sdk_context_is_open(context) && context->microphone_session_id != WATCHER_SDK_SESSION_INVALID;
}

static int sdk_microphone_transport_send(const uint8_t *data, int len, void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;
    if (!sdk_microphone_transport_ready(context) || data == NULL || len <= 0) {
        return -1;
    }
    context->microphone.on_frame(data, (size_t)len, sdk_now_ms(), context->microphone.user_context);
    return 0;
}

static int sdk_microphone_transport_end(void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;
    if (!sdk_context_is_open(context)) {
        return -1;
    }
    if (context->microphone.on_end != NULL) {
        context->microphone.on_end(context->microphone.user_context);
    }
    return 0;
}

watcher_sdk_result_t watcher_microphone_open(watcher_sdk_context_t *context,
                                             const watcher_microphone_config_t *config,
                                             watcher_sdk_session_id_t *out_session_id) {
    voice_transport_t transport = {0};
    esp_err_t error;

    if (!sdk_context_is_open(context) || config == NULL || config->on_frame == NULL || out_session_id == NULL ||
        context->microphone_session_id != WATCHER_SDK_SESSION_INVALID) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (context->microphone_audio_release_pending) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    if ((config->sample_rate_hz != 0U && config->sample_rate_hz != 16000U) ||
        (config->channels != 0U && config->channels != 1U) ||
        (config->sample_width_bytes != 0U && config->sample_width_bytes != 2U)) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    context->microphone = *config;
    context->microphone.sample_rate_hz = 16000U;
    context->microphone.channels = 1U;
    context->microphone.sample_width_bytes = 2U;
    context->microphone_session_id = sdk_next_session_id(context);
    transport.is_ready = sdk_microphone_transport_ready;
    transport.send_audio = sdk_microphone_transport_send;
    transport.send_audio_end = sdk_microphone_transport_end;
    transport.user_ctx = context;
    voice_recorder_set_behavior_feedback_enabled(false);
    voice_recorder_set_transport(&transport);
    error = voice_recorder_request_open();
    if (error != ESP_OK) {
        voice_recorder_reset_transport();
        context->microphone_session_id = WATCHER_SDK_SESSION_INVALID;
        return sdk_from_esp_err(error);
    }
    *out_session_id = context->microphone_session_id;
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_microphone_close(watcher_sdk_context_t *context,
                                              watcher_sdk_session_id_t session_id) {
    esp_err_t error;
    if (!sdk_context_is_open(context) || session_id == WATCHER_SDK_SESSION_INVALID ||
        session_id != context->microphone_session_id) {
        return WATCHER_SDK_RESULT_NOT_FOUND;
    }
    error = voice_recorder_request_close();
    if (error == ESP_OK) {
        context->microphone_audio_release_pending = true;
    }
    voice_recorder_reset_transport();
    voice_recorder_set_behavior_feedback_enabled(true);
    context->microphone_session_id = WATCHER_SDK_SESSION_INVALID;
    memset(&context->microphone, 0, sizeof(context->microphone));
    return sdk_from_esp_err(error);
}

static void sdk_camera_frame(const uint8_t *jpeg, size_t size, uint32_t timestamp_ms, void *user_context) {
    watcher_sdk_context_t *context = (watcher_sdk_context_t *)user_context;
    if (!sdk_context_is_open(context) || context->camera_session_id == WATCHER_SDK_SESSION_INVALID || jpeg == NULL) {
        return;
    }
    context->camera.on_image(jpeg, size, timestamp_ms, context->camera.user_context);
}

watcher_sdk_result_t watcher_camera_capture(watcher_sdk_context_t *context,
                                            const watcher_camera_capture_config_t *config,
                                            watcher_sdk_session_id_t *out_session_id) {
    watcher_sdk_session_id_t assigned_session_id;

    if (!sdk_context_is_open(context) || config == NULL || config->on_image == NULL || out_session_id == NULL ||
        context->camera_capture_done == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (context->camera_session_id != WATCHER_SDK_SESSION_INVALID || context->camera_task != NULL) {
        return WATCHER_SDK_RESULT_BUSY;
    }
    if (config->width > 0 || config->height > 0) {
        if (config->width <= 0 || config->height <= 0) {
            return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
        }
    }
    if (xSemaphoreTake(context->camera_capture_done, 0) != pdTRUE) {
        return WATCHER_SDK_RESULT_BUSY;
    }
    context->camera = *config;
    if (context->camera.width == 0 && context->camera.height == 0) {
        context->camera.width = WATCHER_SDK_CAMERA_DEFAULT_WIDTH;
        context->camera.height = WATCHER_SDK_CAMERA_DEFAULT_HEIGHT;
    }
    assigned_session_id = sdk_next_session_id(context);
    context->camera_session_id = assigned_session_id;
    if (xTaskCreate(sdk_camera_capture_task, "sdk_camera", WATCHER_SDK_CAMERA_TASK_STACK, context,
                    WATCHER_SDK_CAMERA_TASK_PRIORITY, &context->camera_task) != pdPASS) {
        context->camera_session_id = WATCHER_SDK_SESSION_INVALID;
        memset(&context->camera, 0, sizeof(context->camera));
        xSemaphoreGive(context->camera_capture_done);
        return WATCHER_SDK_RESULT_NO_CAPACITY;
    }
    *out_session_id = assigned_session_id;
    return WATCHER_SDK_RESULT_OK;
}
