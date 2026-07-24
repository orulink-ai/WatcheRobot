#include "sdk_control_app.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ota_service.h"
#include "sdkconfig.h"
#include "sfx_service.h"
#include "voice_service.h"
#include "watcher_sdk.h"
#include "watcher_sdk_discovery.h"
#include "watcher_sdk_protocol.h"
#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "SDK_CONTROL_APP"
#define SDK_TEXT_QUEUE_DEPTH 8U
#define SDK_NACK_QUEUE_DEPTH 4U
#define SDK_INPUT_QUEUE_DEPTH 16U
#define SDK_TEXT_MESSAGE_MAX 1024U
#define SDK_RESPONSE_MAX 768U
#define SDK_COMMAND_CACHE_SIZE 8U
#define SDK_DISCOVERY_TIMEOUT_MS 5000U
#define SDK_HELLO_RESPONSE_TIMEOUT_MS 5000U
#define SDK_NETWORK_TASK_STACK 4096U
#define SDK_NETWORK_EXIT_TIMEOUT_MS 2000U
#define SDK_HANDLER_EXIT_TIMEOUT_MS 250U

#define SDK_NET_STOP_REQUESTED BIT0
#define SDK_NET_RESET_REQUESTED BIT1
#define SDK_NET_EXITED BIT2

typedef struct {
    char json[SDK_TEXT_MESSAGE_MAX];
} sdk_text_message_t;

typedef struct {
    char command_id[WATCHER_SDK_PROTOCOL_COMMAND_ID_MAX];
    char response[SDK_RESPONSE_MAX];
} sdk_command_cache_entry_t;

typedef struct {
    char message_type[WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX];
    char command_id[WATCHER_SDK_PROTOCOL_COMMAND_ID_MAX];
    char reason[32];
} sdk_nack_message_t;

static sdk_control_app_ui_t s_ui = {0};
static watcher_sdk_context_t *s_sdk = NULL;
static QueueHandle_t s_text_queue = NULL;
static QueueHandle_t s_nack_queue = NULL;
static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_network_task = NULL;
static EventGroupHandle_t s_network_events = NULL;
static bool s_authenticated = false;
static bool s_was_connected = false;
static bool s_session_reset_pending = false;
static int64_t s_hello_response_wait_started_us = 0;
static char s_pairing_code[7] = "000000";
static sdk_command_cache_entry_t s_command_cache[SDK_COMMAND_CACHE_SIZE] = {0};
static size_t s_command_cache_next = 0U;
static bool s_handler_accepting = false;
static unsigned s_handler_active = 0U;
static portMUX_TYPE s_handler_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_authorized_audio_stream_id = 0U;
static uint32_t s_authorized_audio_total_bytes = 0U;
static uint32_t s_authorized_audio_received_bytes = 0U;
static watcher_sdk_job_id_t s_display_job_id = WATCHER_SDK_JOB_INVALID;
static watcher_sdk_domain_t s_display_domain = WATCHER_SDK_DOMAIN_NONE;

static bool display_job_state_is_terminal(watcher_sdk_job_state_t state) {
    return state == WATCHER_SDK_JOB_COMPLETED || state == WATCHER_SDK_JOB_FAILED || state == WATCHER_SDK_JOB_CANCELLED;
}

static void sdk_control_restore_display_ui(void) {
    s_display_job_id = WATCHER_SDK_JOB_INVALID;
    s_display_domain = WATCHER_SDK_DOMAIN_NONE;
    if (s_authenticated && s_ui.restore_control_ui != NULL) {
        s_ui.restore_control_ui();
    }
}

static const char *sdk_input_action_name(watcher_sdk_input_action_t action) {
    switch (action) {
    case WATCHER_SDK_INPUT_ACTION_PRESS:
        return "press";
    case WATCHER_SDK_INPUT_ACTION_RELEASE:
        return "release";
    case WATCHER_SDK_INPUT_ACTION_LONG_PRESS:
        return "long";
    case WATCHER_SDK_INPUT_ACTION_TAP:
        return "tap";
    case WATCHER_SDK_INPUT_ACTION_ROTATE:
        return "rotate";
    case WATCHER_SDK_INPUT_ACTION_UNKNOWN:
    default:
        return "unknown";
    }
}

static void sdk_format_input_debug(const watcher_sdk_input_event_t *event, char *line, size_t line_size) {
    if (event == NULL || line == NULL || line_size == 0U) {
        return;
    }
    switch (event->source) {
    case WATCHER_SDK_INPUT_BACK_TOUCH:
        (void)snprintf(line, line_size, "Back: %s #%u", sdk_input_action_name(event->action),
                       (unsigned)event->touch_id);
        break;
    case WATCHER_SDK_INPUT_SCREEN_TOUCH:
        (void)snprintf(line, line_size, "Screen: x=%d y=%d", (int)event->x, (int)event->y);
        break;
    case WATCHER_SDK_INPUT_ROLLER:
        (void)snprintf(line, line_size, "Roller: %ld", (long)event->delta);
        break;
    default:
        (void)snprintf(line, line_size, "Input: %s", sdk_input_action_name(event->action));
        break;
    }
}

static void clear_audio_stream_authorization(void) {
    portENTER_CRITICAL(&s_handler_lock);
    s_authorized_audio_stream_id = 0U;
    s_authorized_audio_total_bytes = 0U;
    s_authorized_audio_received_bytes = 0U;
    portEXIT_CRITICAL(&s_handler_lock);
}

static bool sdk_audio_stream_guard(uint8_t flags, uint16_t stream_id, uint32_t sequence, size_t payload_len,
                                   void *context) {
    bool allowed;
    (void)context;

    portENTER_CRITICAL(&s_handler_lock);
    allowed = s_handler_accepting && s_authenticated && stream_id != 0U && stream_id == s_authorized_audio_stream_id &&
              ((flags & WS_FRAME_FLAG_FIRST) == 0U || sequence == 0U) &&
              payload_len <= s_authorized_audio_total_bytes - s_authorized_audio_received_bytes;
    if (allowed) {
        s_authorized_audio_received_bytes += (uint32_t)payload_len;
        if ((flags & WS_FRAME_FLAG_LAST) != 0U) {
            s_authorized_audio_stream_id = 0U;
        }
    }
    portEXIT_CRITICAL(&s_handler_lock);
    return allowed;
}

static watcher_sdk_result_t begin_audio_stream(const watcher_sdk_protocol_command_t *command) {
    watcher_sdk_result_t result = watcher_audio_stop(s_sdk);

    if (result != WATCHER_SDK_RESULT_OK) {
        return result;
    }
    ws_client_abort_tts_playback();
    portENTER_CRITICAL(&s_handler_lock);
    s_authorized_audio_stream_id = command->data.audio_stream.stream_id;
    s_authorized_audio_total_bytes = command->data.audio_stream.total_bytes;
    s_authorized_audio_received_bytes = 0U;
    portEXIT_CRITICAL(&s_handler_lock);
    return WATCHER_SDK_RESULT_OK;
}

static bool sdk_handler_begin(QueueHandle_t *out_text_queue, QueueHandle_t *out_nack_queue) {
    bool accepted = false;

    if (out_text_queue == NULL || out_nack_queue == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_handler_lock);
    if (s_handler_accepting && s_text_queue != NULL && s_nack_queue != NULL) {
        s_handler_active++;
        *out_text_queue = s_text_queue;
        *out_nack_queue = s_nack_queue;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_handler_lock);
    return accepted;
}

static void sdk_handler_end(void) {
    portENTER_CRITICAL(&s_handler_lock);
    if (s_handler_active > 0U) {
        s_handler_active--;
    }
    portEXIT_CRITICAL(&s_handler_lock);
}

static bool sdk_input_handler_begin(QueueHandle_t *out_input_queue) {
    bool accepted = false;

    if (out_input_queue == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_handler_lock);
    if (s_handler_accepting && s_authenticated && s_input_queue != NULL) {
        s_handler_active++;
        *out_input_queue = s_input_queue;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_handler_lock);
    return accepted;
}

static void sdk_session_set_authenticated(bool authenticated) {
    portENTER_CRITICAL(&s_handler_lock);
    s_authenticated = authenticated;
    portEXIT_CRITICAL(&s_handler_lock);
}

static bool queue_input_event(const watcher_sdk_input_event_t *event) {
    QueueHandle_t input_queue = NULL;
    watcher_sdk_input_event_t discarded;

    if (event == NULL || !sdk_input_handler_begin(&input_queue)) {
        return false;
    }
    if (xQueueSend(input_queue, event, 0) != pdTRUE) {
        const bool dropped_oldest = xQueueReceive(input_queue, &discarded, 0) == pdTRUE;
        const bool queued_after_drop = xQueueSend(input_queue, event, 0) == pdTRUE;
        if (dropped_oldest) {
            ESP_LOGW(TAG, "SDK input queue full; oldest event dropped");
        }
        if (!queued_after_drop) {
            ESP_LOGW(TAG, "SDK input event dropped during producer contention");
        }
    }
    sdk_handler_end();
    return true;
}

bool sdk_control_app_publish_back_touch(uint8_t touch_id, uint8_t event_code, uint32_t timestamp_ms) {
    watcher_sdk_input_action_t action = WATCHER_SDK_INPUT_ACTION_UNKNOWN;
    watcher_sdk_input_event_t event = {
        .source = WATCHER_SDK_INPUT_BACK_TOUCH,
        .timestamp_ms = timestamp_ms,
        .touch_id = touch_id,
    };

    switch (event_code) {
    case 1U:
        action = WATCHER_SDK_INPUT_ACTION_PRESS;
        break;
    case 2U:
        action = WATCHER_SDK_INPUT_ACTION_RELEASE;
        break;
    case 3U:
        action = WATCHER_SDK_INPUT_ACTION_LONG_PRESS;
        break;
    default:
        return false;
    }
    event.action = action;
    return queue_input_event(&event);
}

static void sdk_handler_start_accepting(void) {
    portENTER_CRITICAL(&s_handler_lock);
    s_handler_accepting = true;
    portEXIT_CRITICAL(&s_handler_lock);
}

static void sdk_handler_stop_accepting(void) {
    portENTER_CRITICAL(&s_handler_lock);
    s_handler_accepting = false;
    portEXIT_CRITICAL(&s_handler_lock);
}

static bool sdk_handler_wait_idle(uint32_t timeout_ms) {
    TickType_t started = xTaskGetTickCount();

    for (;;) {
        unsigned active;
        portENTER_CRITICAL(&s_handler_lock);
        active = s_handler_active;
        portEXIT_CRITICAL(&s_handler_lock);
        if (active == 0U) {
            return true;
        }
        if ((uint32_t)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS) >= timeout_ms) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void generate_pairing_code(void) {
    unsigned value = (unsigned)(esp_random() % 1000000U);
    (void)snprintf(s_pairing_code, sizeof(s_pairing_code), "%06u", value);
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    ESP_LOGW(TAG, "SDK_SMOKE pairing_code=%s", s_pairing_code);
#endif
}

static bool discovery_cancelled(void *context) {
    EventGroupHandle_t events = (EventGroupHandle_t)context;
    EventBits_t bits = events != NULL ? xEventGroupGetBits(events) : SDK_NET_STOP_REQUESTED;
    return (bits & (SDK_NET_STOP_REQUESTED | SDK_NET_RESET_REQUESTED)) != 0U;
}

static void network_cleanup(void) {
    ws_client_stop_for_resource_release();
    (void)ws_client_deinit();
    ws_client_set_session_pairing_code(NULL);
}

static void sdk_network_task(void *argument) {
    EventGroupHandle_t events = (EventGroupHandle_t)argument;

    for (;;) {
        watcher_sdk_gateway_info_t gateway = {0};
        char *url;
        EventBits_t bits = xEventGroupGetBits(events);

        if ((bits & SDK_NET_STOP_REQUESTED) != 0U) {
            break;
        }
        if ((bits & SDK_NET_RESET_REQUESTED) != 0U) {
            network_cleanup();
            (void)xEventGroupClearBits(events, SDK_NET_RESET_REQUESTED);
            continue;
        }
        if (ws_client_is_started()) {
            (void)xEventGroupWaitBits(events, SDK_NET_STOP_REQUESTED | SDK_NET_RESET_REQUESTED, pdFALSE, pdFALSE,
                                      pdMS_TO_TICKS(100));
            continue;
        }
        if (watcher_sdk_discovery_start(&gateway, s_pairing_code, SDK_DISCOVERY_TIMEOUT_MS, discovery_cancelled,
                                        events) != 0) {
            (void)xEventGroupWaitBits(events, SDK_NET_STOP_REQUESTED | SDK_NET_RESET_REQUESTED, pdFALSE, pdFALSE,
                                      pdMS_TO_TICKS(250));
            continue;
        }
        if ((xEventGroupGetBits(events) & SDK_NET_STOP_REQUESTED) != 0U) {
            break;
        }
        url = watcher_sdk_discovery_ws_url(&gateway);
        if (url == NULL) {
            continue;
        }
        ws_client_set_session_pairing_code(s_pairing_code);
        if (ws_client_set_server_url(url) != 0 || ws_client_init() != 0 || ws_client_start() != 0) {
            ESP_LOGW(TAG, "SDK gateway connection failed url=%s", url);
            network_cleanup();
        } else {
            ESP_LOGI(TAG, "SDK gateway selected url=%s", url);
        }
        free(url);
    }
    network_cleanup();
    s_network_task = NULL;
    (void)xEventGroupSetBits(events, SDK_NET_EXITED);
    vTaskDelete(NULL);
}

static bool message_is_sdk_control(const char *json, size_t json_len) {
    cJSON *root;
    cJSON *type;
    bool matches = false;

    if (json == NULL || json_len == 0U) {
        return false;
    }
    root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        return false;
    }
    type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring != NULL) {
        matches = watcher_sdk_protocol_supports_type(type->valuestring);
    }
    cJSON_Delete(root);
    return matches;
}

static void queue_protocol_nack(QueueHandle_t nack_queue, const char *json, size_t json_len, const char *reason) {
    watcher_sdk_protocol_command_t command = {0};
    sdk_nack_message_t nack = {0};
    sdk_nack_message_t discarded;

    if (nack_queue == NULL || reason == NULL) {
        return;
    }
    (void)watcher_sdk_protocol_parse(json, json_len, &command);
    if (command.message_type[0] == '\0' || command.command_id[0] == '\0') {
        ESP_LOGW(TAG, "Cannot correlate rejected SDK command");
        return;
    }
    (void)snprintf(nack.message_type, sizeof(nack.message_type), "%s", command.message_type);
    (void)snprintf(nack.command_id, sizeof(nack.command_id), "%s", command.command_id);
    (void)snprintf(nack.reason, sizeof(nack.reason), "%s", reason);
    if (xQueueSend(nack_queue, &nack, 0) != pdTRUE) {
        (void)xQueueReceive(nack_queue, &discarded, 0);
        if (xQueueSend(nack_queue, &nack, 0) != pdTRUE) {
            ESP_LOGW(TAG, "SDK diagnostic queue full");
        }
    }
}

static bool sdk_text_handler(const char *json, size_t json_len, void *context) {
    sdk_text_message_t message = {0};
    QueueHandle_t text_queue = NULL;
    QueueHandle_t nack_queue = NULL;
    (void)context;

    if (json == NULL || json_len == 0U || json_len >= sizeof(message.json) || !message_is_sdk_control(json, json_len)) {
        return false;
    }
    if (!sdk_handler_begin(&text_queue, &nack_queue)) {
        return true;
    }
    memcpy(message.json, json, json_len);
    message.json[json_len] = '\0';
    if (xQueueSend(text_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "SDK command queue full");
        queue_protocol_nack(nack_queue, json, json_len, "command_queue_full");
    }
    sdk_handler_end();
    return true;
}

static const char *cached_response(const char *command_id) {
    size_t index;
    for (index = 0U; index < SDK_COMMAND_CACHE_SIZE; ++index) {
        if (s_command_cache[index].command_id[0] != '\0' &&
            strcmp(s_command_cache[index].command_id, command_id) == 0) {
            return s_command_cache[index].response;
        }
    }
    return NULL;
}

static void cache_response(const char *command_id, const char *response) {
    sdk_command_cache_entry_t *entry = &s_command_cache[s_command_cache_next];
    (void)snprintf(entry->command_id, sizeof(entry->command_id), "%s", command_id);
    (void)snprintf(entry->response, sizeof(entry->response), "%s", response);
    s_command_cache_next = (s_command_cache_next + 1U) % SDK_COMMAND_CACHE_SIZE;
}

static void send_and_cache(const char *command_id, const char *response) {
    if (response == NULL || response[0] == '\0') {
        return;
    }
    (void)ws_client_send_text(response);
    if (command_id != NULL && command_id[0] != '\0') {
        cache_response(command_id, response);
    }
}

static void send_protocol_nack(const char *message_type, const char *command_id, const char *reason) {
    char response[SDK_RESPONSE_MAX];

    if (message_type == NULL || message_type[0] == '\0' || command_id == NULL || command_id[0] == '\0' ||
        reason == NULL) {
        return;
    }
    if (watcher_sdk_protocol_build_nack(message_type, command_id, reason, response, sizeof(response)) ==
        WATCHER_SDK_PROTOCOL_OK) {
        send_and_cache(command_id, response);
    }
}

static const char *result_reason(watcher_sdk_result_t result) {
    switch (result) {
    case WATCHER_SDK_RESULT_INVALID_ARGUMENT:
        return "invalid_argument";
    case WATCHER_SDK_RESULT_INVALID_STATE:
        return "invalid_state";
    case WATCHER_SDK_RESULT_BUSY:
        return "busy";
    case WATCHER_SDK_RESULT_NOT_FOUND:
        return "not_found";
    case WATCHER_SDK_RESULT_NO_CAPACITY:
        return "no_capacity";
    case WATCHER_SDK_RESULT_EXECUTOR_FAILED:
        return "executor_failed";
    case WATCHER_SDK_RESULT_OK:
    default:
        return "unknown";
    }
}

static void microphone_frame(const uint8_t *pcm, size_t size, uint32_t timestamp_ms, void *context) {
    (void)timestamp_ms;
    (void)context;
    (void)ws_send_audio(pcm, (int)size);
}

static void microphone_end(void *context) {
    (void)context;
    (void)ws_send_audio_end();
}

static void camera_image(const uint8_t *jpeg, size_t size, uint32_t timestamp_ms, void *context) {
    (void)timestamp_ms;
    (void)context;
    (void)ws_send_image_frame(jpeg, size);
}

static watcher_sdk_result_t prepare_animation_surface(void) {
    if (s_ui.prepare_animation == NULL || !s_ui.prepare_animation()) {
        ESP_LOGW(TAG, "SDK animation surface is unavailable");
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    return WATCHER_SDK_RESULT_OK;
}

static watcher_sdk_result_t execute_command(const watcher_sdk_protocol_command_t *command,
                                            watcher_sdk_job_id_t *out_job_id,
                                            watcher_sdk_session_id_t *out_session_id) {
    watcher_motion_target_t motion;
    watcher_light_color_t color;
    watcher_light_effect_options_t effect;
    watcher_microphone_config_t microphone;
    watcher_camera_capture_config_t camera;
    watcher_sdk_result_t result;

    *out_job_id = WATCHER_SDK_JOB_INVALID;
    *out_session_id = WATCHER_SDK_SESSION_INVALID;
    switch (command->type) {
    case WATCHER_SDK_PROTOCOL_JOB_CANCEL:
        return watcher_job_cancel(s_sdk, command->data.job_cancel.operation_id);
    case WATCHER_SDK_PROTOCOL_BEHAVIOR_PLAY:
        if (prepare_animation_surface() != WATCHER_SDK_RESULT_OK) {
            return WATCHER_SDK_RESULT_INVALID_STATE;
        }
        result = watcher_behavior_play(s_sdk, command->data.behavior_play.behavior_id,
                                       command->data.behavior_play.repeat_count, out_job_id);
        if (result == WATCHER_SDK_RESULT_OK) {
            s_display_job_id = *out_job_id;
            s_display_domain = WATCHER_SDK_DOMAIN_BEHAVIOR;
        } else {
            sdk_control_restore_display_ui();
        }
        return result;
    case WATCHER_SDK_PROTOCOL_BEHAVIOR_STOP:
        result = watcher_behavior_stop(s_sdk);
        if (result == WATCHER_SDK_RESULT_OK && s_display_domain == WATCHER_SDK_DOMAIN_BEHAVIOR) {
            sdk_control_restore_display_ui();
        }
        return result;
    case WATCHER_SDK_PROTOCOL_ANIMATION_PLAY:
        if (prepare_animation_surface() != WATCHER_SDK_RESULT_OK) {
            return WATCHER_SDK_RESULT_INVALID_STATE;
        }
        result = watcher_animation_play(s_sdk, command->data.resource.resource_id, out_job_id);
        if (result == WATCHER_SDK_RESULT_OK) {
            s_display_job_id = *out_job_id;
            s_display_domain = WATCHER_SDK_DOMAIN_ANIMATION;
        } else {
            sdk_control_restore_display_ui();
        }
        return result;
    case WATCHER_SDK_PROTOCOL_ANIMATION_STOP:
        result = watcher_animation_stop(s_sdk);
        if (result == WATCHER_SDK_RESULT_OK && s_display_domain == WATCHER_SDK_DOMAIN_ANIMATION) {
            sdk_control_restore_display_ui();
        }
        return result;
    case WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO:
        memset(&motion, 0, sizeof(motion));
        motion.pan_deg = command->data.motion.pan_deg;
        motion.tilt_deg = command->data.motion.tilt_deg;
        motion.duration_ms = command->data.motion.duration_ms;
        motion.ease_in_out = command->data.motion.ease_in_out;
        return watcher_motion_move_to(s_sdk, &motion, out_job_id);
    case WATCHER_SDK_PROTOCOL_MOTION_SET_TARGET:
        return watcher_motion_set_target(s_sdk, command->data.motion.has_pan, command->data.motion.pan_deg,
                                         command->data.motion.has_tilt, command->data.motion.tilt_deg);
    case WATCHER_SDK_PROTOCOL_MOTION_ACTION_PLAY:
        return watcher_motion_play_action(s_sdk, command->data.resource.resource_id, out_job_id);
    case WATCHER_SDK_PROTOCOL_MOTION_STOP:
        return watcher_motion_stop(s_sdk);
    case WATCHER_SDK_PROTOCOL_AUDIO_PLAY:
        clear_audio_stream_authorization();
        ws_client_abort_tts_playback();
        return watcher_audio_play(s_sdk, command->data.resource.resource_id, out_job_id);
    case WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN:
        return begin_audio_stream(command);
    case WATCHER_SDK_PROTOCOL_AUDIO_STOP:
        clear_audio_stream_authorization();
        ws_client_abort_tts_playback();
        return watcher_audio_stop(s_sdk);
    case WATCHER_SDK_PROTOCOL_LIGHT_SET:
        memset(&color, 0, sizeof(color));
        color.red = command->data.light.red;
        color.green = command->data.light.green;
        color.blue = command->data.light.blue;
        color.brightness_percent = command->data.light.brightness_percent;
        color.zone = command->data.light.zone;
        return watcher_light_set_color(s_sdk, &color);
    case WATCHER_SDK_PROTOCOL_LIGHT_EFFECT_PLAY:
        memset(&effect, 0, sizeof(effect));
        effect.effect = command->data.light.effect;
        effect.color.red = command->data.light.red;
        effect.color.green = command->data.light.green;
        effect.color.blue = command->data.light.blue;
        effect.color.brightness_percent = command->data.light.brightness_percent;
        effect.color.zone = command->data.light.zone;
        effect.period_ms = command->data.light.period_ms;
        effect.repeat_count = command->data.light.repeat_count;
        return watcher_light_play_effect(s_sdk, &effect, out_job_id);
    case WATCHER_SDK_PROTOCOL_LIGHT_OFF:
        return watcher_light_off(s_sdk);
    case WATCHER_SDK_PROTOCOL_MICROPHONE_OPEN:
        memset(&microphone, 0, sizeof(microphone));
        microphone.sample_rate_hz = command->data.microphone.sample_rate_hz;
        microphone.channels = 1U;
        microphone.sample_width_bytes = 2U;
        microphone.on_frame = microphone_frame;
        microphone.on_end = microphone_end;
        return watcher_microphone_open(s_sdk, &microphone, out_session_id);
    case WATCHER_SDK_PROTOCOL_MICROPHONE_CLOSE:
        return watcher_microphone_close(s_sdk, command->data.session.session_id);
    case WATCHER_SDK_PROTOCOL_CAMERA_CAPTURE:
        memset(&camera, 0, sizeof(camera));
        camera.width = command->data.camera.width;
        camera.height = command->data.camera.height;
        camera.quality = command->data.camera.quality;
        camera.on_image = camera_image;
        return watcher_camera_capture(s_sdk, &camera, out_session_id);
    case WATCHER_SDK_PROTOCOL_UNKNOWN:
    case WATCHER_SDK_PROTOCOL_AUTHENTICATE:
    default:
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
}

static void sdk_session_activate_after_hello(void) {
    char ready[SDK_RESPONSE_MAX];
    char device_id[32];
    char mac[18];

    if (s_authenticated || s_sdk == NULL || ws_client_is_session_ready() == 0) {
        return;
    }
    watcher_sdk_device_identity(device_id, sizeof(device_id), mac, sizeof(mac));
    if (watcher_sdk_protocol_build_ready(device_id, ota_service_get_fw_version(), ready, sizeof(ready)) !=
            WATCHER_SDK_PROTOCOL_OK ||
        ws_client_send_text(ready) < 0) {
        return;
    }
    sdk_session_set_authenticated(true);
    if (s_ui.show_connected != NULL) {
        s_ui.show_connected();
    }
}

static void process_message(const sdk_text_message_t *message) {
    watcher_sdk_protocol_command_t command;
    watcher_sdk_protocol_result_t parse_result;
    watcher_sdk_result_t result;
    watcher_sdk_job_id_t job_id;
    watcher_sdk_session_id_t session_id;
    char response[SDK_RESPONSE_MAX];
    const char *cached;

    parse_result = watcher_sdk_protocol_parse(message->json, strlen(message->json), &command);
    if (parse_result != WATCHER_SDK_PROTOCOL_OK) {
        ESP_LOGW(TAG, "Invalid SDK protocol message result=%d", (int)parse_result);
        if (command.message_type[0] != '\0' && command.command_id[0] != '\0') {
            send_protocol_nack(command.message_type, command.command_id,
                               parse_result == WATCHER_SDK_PROTOCOL_UNSUPPORTED ? "unsupported_command"
                                                                                : "invalid_argument");
        }
        return;
    }
    cached = cached_response(command.command_id);
    if (cached != NULL) {
        (void)ws_client_send_text(cached);
        return;
    }
    if (command.type == WATCHER_SDK_PROTOCOL_AUTHENTICATE) {
        send_protocol_nack(command.message_type, command.command_id, "unsupported_command");
        return;
    }
    if (!s_authenticated) {
        (void)watcher_sdk_protocol_build_nack(command.message_type, command.command_id, "not_authenticated", response,
                                              sizeof(response));
        send_and_cache(command.command_id, response);
        return;
    }
    result = execute_command(&command, &job_id, &session_id);
    if (result != WATCHER_SDK_RESULT_OK) {
        (void)watcher_sdk_protocol_build_nack(command.message_type, command.command_id, result_reason(result), response,
                                              sizeof(response));
    } else if (session_id != WATCHER_SDK_SESSION_INVALID) {
        (void)watcher_sdk_protocol_build_session_ack(command.message_type, command.command_id, session_id, response,
                                                     sizeof(response));
    } else {
        (void)watcher_sdk_protocol_build_ack(command.message_type, command.command_id, job_id, response,
                                             sizeof(response));
    }
    send_and_cache(command.command_id, response);
}

static bool reset_control_session(void) {
    watcher_sdk_config_t config = {.app_id = SDK_CONTROL_APP_ID};

    sdk_handler_stop_accepting();
    if (!sdk_handler_wait_idle(SDK_HANDLER_EXIT_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "SDK session reset waiting for active text handler");
        return false;
    }
    if (s_sdk != NULL) {
        watcher_sdk_close(s_sdk);
        s_sdk = NULL;
    }
    sdk_session_set_authenticated(false);
    s_display_job_id = WATCHER_SDK_JOB_INVALID;
    s_display_domain = WATCHER_SDK_DOMAIN_NONE;
    clear_audio_stream_authorization();
    memset(s_command_cache, 0, sizeof(s_command_cache));
    s_command_cache_next = 0U;
    if (s_text_queue != NULL) {
        (void)xQueueReset(s_text_queue);
    }
    if (s_nack_queue != NULL) {
        (void)xQueueReset(s_nack_queue);
    }
    if (s_input_queue != NULL) {
        (void)xQueueReset(s_input_queue);
    }
    if (watcher_sdk_open(&config, &s_sdk) != WATCHER_SDK_RESULT_OK) {
        ESP_LOGE(TAG, "SDK context reset failed");
        return false;
    }
    generate_pairing_code();
    if (s_ui.show_pairing_code != NULL) {
        s_ui.show_pairing_code(s_pairing_code, true);
    }
    sdk_handler_start_accepting();
    return true;
}

static bool sdk_hello_response_timed_out(void) {
    int64_t now_us;

    if (ws_client_is_connected() == 0 || ws_client_is_session_ready() != 0) {
        s_hello_response_wait_started_us = 0;
        return false;
    }
    now_us = esp_timer_get_time();
    if (s_hello_response_wait_started_us <= 0) {
        s_hello_response_wait_started_us = now_us;
        return false;
    }
    return (now_us - s_hello_response_wait_started_us) >= ((int64_t)SDK_HELLO_RESPONSE_TIMEOUT_MS * 1000LL);
}

static bool sdk_request_gateway_reset(const char *reason) {
    EventBits_t bits;

    if (s_network_events == NULL) {
        return false;
    }
    bits = xEventGroupGetBits(s_network_events);
    if ((bits & SDK_NET_RESET_REQUESTED) != 0U) {
        return true;
    }
    ESP_LOGW(TAG, "SDK gateway reset requested: %s", reason != NULL ? reason : "unknown");
    (void)xEventGroupSetBits(s_network_events, SDK_NET_RESET_REQUESTED);
    s_session_reset_pending = true;
    s_hello_response_wait_started_us = 0;
    s_was_connected = false;
    return true;
}

static void sdk_control_start_session(void) {
    watcher_sdk_config_t config = {.app_id = SDK_CONTROL_APP_ID};
    bool handler_idle;

    sdk_session_set_authenticated(false);
    s_display_job_id = WATCHER_SDK_JOB_INVALID;
    s_display_domain = WATCHER_SDK_DOMAIN_NONE;
    clear_audio_stream_authorization();
    s_was_connected = false;
    s_session_reset_pending = false;
    memset(s_command_cache, 0, sizeof(s_command_cache));
    s_command_cache_next = 0U;
    generate_pairing_code();
    s_text_queue = xQueueCreate(SDK_TEXT_QUEUE_DEPTH, sizeof(sdk_text_message_t));
    s_nack_queue = xQueueCreate(SDK_NACK_QUEUE_DEPTH, sizeof(sdk_nack_message_t));
    s_input_queue = xQueueCreate(SDK_INPUT_QUEUE_DEPTH, sizeof(watcher_sdk_input_event_t));
    s_network_events = xEventGroupCreate();
    if (s_text_queue == NULL || s_nack_queue == NULL || s_input_queue == NULL || s_network_events == NULL ||
        watcher_sdk_open(&config, &s_sdk) != WATCHER_SDK_RESULT_OK) {
        ESP_LOGE(TAG, "SDK Control initialization failed");
        goto fail;
    }
    (void)voice_recorder_start();
    voice_recorder_set_recording_permitted(true);
    voice_recorder_set_behavior_feedback_enabled(false);
    ws_client_set_behavior_feedback_enabled(false);
    sfx_service_set_voice_audio_busy(false);
    sdk_handler_start_accepting();
    ws_client_register_text_handler(sdk_text_handler, NULL);
    ws_client_register_tts_frame_guard(sdk_audio_stream_guard, NULL);
    if (s_ui.show_pairing_code != NULL) {
        s_ui.show_pairing_code(s_pairing_code, false);
    }
    if (xTaskCreate(sdk_network_task, "sdk_gateway", SDK_NETWORK_TASK_STACK, s_network_events, 4, &s_network_task) !=
        pdPASS) {
        ESP_LOGE(TAG, "SDK gateway task creation failed");
        s_network_task = NULL;
        goto fail;
    }
    return;

fail:
    ws_client_register_tts_frame_guard(NULL, NULL);
    ws_client_register_text_handler(NULL, NULL);
    sdk_handler_stop_accepting();
    handler_idle = sdk_handler_wait_idle(SDK_HANDLER_EXIT_TIMEOUT_MS);
    (void)voice_recorder_close();
    ws_client_set_behavior_feedback_enabled(true);
    if (s_sdk != NULL) {
        watcher_sdk_close(s_sdk);
        s_sdk = NULL;
    }
    if (s_text_queue != NULL && handler_idle) {
        vQueueDelete(s_text_queue);
        s_text_queue = NULL;
    }
    if (s_nack_queue != NULL && handler_idle) {
        vQueueDelete(s_nack_queue);
        s_nack_queue = NULL;
    }
    if (s_input_queue != NULL && handler_idle) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
    }
    if (s_network_events != NULL) {
        vEventGroupDelete(s_network_events);
        s_network_events = NULL;
    }
    if (s_ui.show_error != NULL) {
        s_ui.show_error(SDK_CONTROL_APP_UI_ERROR_INITIALIZATION);
    }
}

static void sdk_control_on_open(void) {
    sdk_handler_stop_accepting();
    if ((s_text_queue != NULL || s_nack_queue != NULL || s_input_queue != NULL) && !sdk_handler_wait_idle(0U)) {
        ESP_LOGE(TAG, "Previous SDK text handler has not exited");
        if (s_ui.show_error != NULL) {
            s_ui.show_error(SDK_CONTROL_APP_UI_ERROR_STOPPING);
        }
        return;
    }
    if (s_text_queue != NULL) {
        vQueueDelete(s_text_queue);
        s_text_queue = NULL;
    }
    if (s_nack_queue != NULL) {
        vQueueDelete(s_nack_queue);
        s_nack_queue = NULL;
    }
    if (s_input_queue != NULL) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
    }
    if (s_network_events != NULL && s_network_task == NULL &&
        (xEventGroupGetBits(s_network_events) & SDK_NET_EXITED) != 0U) {
        vEventGroupDelete(s_network_events);
        s_network_events = NULL;
    }
    if (s_network_task != NULL || s_network_events != NULL) {
        ESP_LOGE(TAG, "Previous SDK gateway task has not exited");
        if (s_ui.show_error != NULL) {
            s_ui.show_error(SDK_CONTROL_APP_UI_ERROR_STOPPING);
        }
        return;
    }

    sdk_control_start_session();
}

static void sdk_control_on_tick(void) {
    sdk_text_message_t message;
    sdk_nack_message_t nack;
    watcher_sdk_input_event_t input_event;
    watcher_sdk_event_t event;
    bool connected = ws_client_is_connected() != 0;
    bool reset_in_progress = false;

    if (ws_client_has_hello_rejected()) {
        reset_in_progress = sdk_request_gateway_reset("hello rejected");
    } else if (sdk_hello_response_timed_out()) {
        reset_in_progress = sdk_request_gateway_reset("hello response timeout");
    } else if (s_was_connected && !connected) {
        reset_in_progress = sdk_request_gateway_reset("connection closed");
    }
    if (!reset_in_progress) {
        s_was_connected = connected;
    }
    if (s_session_reset_pending && reset_control_session()) {
        s_session_reset_pending = false;
    }
    sdk_session_activate_after_hello();
    while (s_nack_queue != NULL && xQueueReceive(s_nack_queue, &nack, 0) == pdTRUE) {
        send_protocol_nack(nack.message_type, nack.command_id, nack.reason);
    }
    while (s_text_queue != NULL && xQueueReceive(s_text_queue, &message, 0) == pdTRUE) {
        process_message(&message);
    }
    while (s_input_queue != NULL && xQueueReceive(s_input_queue, &input_event, 0) == pdTRUE) {
        char json[SDK_RESPONSE_MAX];
        if (s_ui.show_input_debug != NULL) {
            char line[64];
            sdk_format_input_debug(&input_event, line, sizeof(line));
            s_ui.show_input_debug(line);
        }
        if (watcher_sdk_protocol_build_input_event(&input_event, json, sizeof(json)) == WATCHER_SDK_PROTOCOL_OK) {
            (void)ws_client_send_text(json);
        }
    }
    while (s_sdk != NULL && watcher_sdk_poll_event(s_sdk, &event)) {
        char json[SDK_RESPONSE_MAX];
        if (watcher_sdk_protocol_build_operation_event(&event, json, sizeof(json)) == WATCHER_SDK_PROTOCOL_OK) {
            (void)ws_client_send_text(json);
        }
        if (event.job_id == s_display_job_id && display_job_state_is_terminal(event.state)) {
            sdk_control_restore_display_ui();
        }
    }
    if (s_ui.tick_ui != NULL) {
        s_ui.tick_ui();
    }
}

static void sdk_control_on_close(void) {
    bool network_exited = s_network_events == NULL;
    bool handler_idle;

    ws_client_register_text_handler(NULL, NULL);
    ws_client_register_tts_frame_guard(NULL, NULL);
    sdk_handler_stop_accepting();
    handler_idle = sdk_handler_wait_idle(SDK_HANDLER_EXIT_TIMEOUT_MS);
    if (!handler_idle) {
        ESP_LOGE(TAG, "SDK text handler exit timed out; retaining its queues");
    }
    if (s_network_events != NULL) {
        EventBits_t bits;
        (void)xEventGroupSetBits(s_network_events, SDK_NET_STOP_REQUESTED | SDK_NET_RESET_REQUESTED);
        bits = xEventGroupWaitBits(s_network_events, SDK_NET_EXITED, pdFALSE, pdTRUE,
                                   pdMS_TO_TICKS(SDK_NETWORK_EXIT_TIMEOUT_MS));
        network_exited = (bits & SDK_NET_EXITED) != 0U;
        if (!network_exited) {
            ESP_LOGE(TAG, "SDK gateway task exit timed out; retaining its synchronization object");
        }
    }
    if (s_sdk != NULL) {
        watcher_sdk_close(s_sdk);
        s_sdk = NULL;
    }
    (void)voice_recorder_close();
    ws_client_set_behavior_feedback_enabled(true);
    if (s_text_queue != NULL && handler_idle) {
        vQueueDelete(s_text_queue);
        s_text_queue = NULL;
    }
    if (s_nack_queue != NULL && handler_idle) {
        vQueueDelete(s_nack_queue);
        s_nack_queue = NULL;
    }
    if (s_input_queue != NULL && handler_idle) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
    }
    if (s_network_events != NULL && network_exited) {
        vEventGroupDelete(s_network_events);
        s_network_events = NULL;
    }
    sdk_session_set_authenticated(false);
    s_display_job_id = WATCHER_SDK_JOB_INVALID;
    s_display_domain = WATCHER_SDK_DOMAIN_NONE;
    clear_audio_stream_authorization();
    s_was_connected = false;
    s_session_reset_pending = false;
    if (s_ui.close_ui != NULL) {
        s_ui.close_ui();
    }
}

static void sdk_control_on_button(void) {
    if (s_ui.on_button != NULL) {
        s_ui.on_button();
    }
}

static void sdk_control_on_touch(int16_t x, int16_t y) {
    watcher_sdk_input_event_t event = {
        .source = WATCHER_SDK_INPUT_SCREEN_TOUCH,
        .action = WATCHER_SDK_INPUT_ACTION_TAP,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
        .x = x,
        .y = y,
    };
    (void)queue_input_event(&event);
}

static void sdk_control_on_rotate(int32_t diff) {
    watcher_sdk_input_event_t event = {
        .source = WATCHER_SDK_INPUT_ROLLER,
        .action = WATCHER_SDK_INPUT_ACTION_ROTATE,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
        .delta = diff,
    };
    (void)queue_input_event(&event);
}

static const watcher_app_t s_app = {
    .id = SDK_CONTROL_APP_ID,
    .name = "Python SDK",
    .icon = "python-sdk",
    .theme_color = 0xA9DE2C,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources =
        WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_AUDIO | WATCHER_APP_RESOURCE_SET_MCU_RUNTIME,
    .lifecycle = WATCHER_APP_LIFECYCLE_PERSISTENT,
    .input_context = WATCHER_INPUT_CONTEXT_APP_EVENT,
    .on_open = sdk_control_on_open,
    .on_tick = sdk_control_on_tick,
    .on_close = sdk_control_on_close,
    .on_button = sdk_control_on_button,
    .on_touch = sdk_control_on_touch,
    .on_rotate = sdk_control_on_rotate,
};

void sdk_control_app_configure(const sdk_control_app_ui_t *ui) {
    if (ui == NULL) {
        memset(&s_ui, 0, sizeof(s_ui));
    } else {
        s_ui = *ui;
    }
}

const watcher_app_t *sdk_control_app_get(void) {
    return &s_app;
}

bool sdk_control_app_debug_log_pairing_code(void) {
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    if (s_sdk == NULL) {
        return false;
    }
    ESP_LOGW(TAG, "SDK_SMOKE pairing_code=%s", s_pairing_code);
    return true;
#else
    return false;
#endif
}
