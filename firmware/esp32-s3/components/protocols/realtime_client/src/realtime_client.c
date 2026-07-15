#include "realtime_client.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "realtime_frame_assembler.h"
#include "realtime_protocol.h"

#include <stdio.h>
#include <string.h>

#define TAG "REALTIME"
#define REALTIME_WS_BUFFER_SIZE 8192
#define REALTIME_RX_MESSAGE_MAX 16384
#define REALTIME_WS_TASK_STACK 12288
#define REALTIME_WS_TASK_PRIO 8
#define REALTIME_SEND_TIMEOUT_MS 2000
#define REALTIME_EVENT_TYPE_MAX 96
#define REALTIME_TEXT_MAX 512
#define REALTIME_AUDIO_DECODE_MAX 6144
#define REALTIME_AUDIO_B64_MAX 8192
#define REALTIME_TX_B64_MAX ((1920U * 4U / 3U) + 8U)
#define REALTIME_TX_MESSAGE_MAX 2700

typedef struct {
    esp_websocket_client_handle_t ws;
    SemaphoreHandle_t send_lock;
    realtime_client_state_t state;
    realtime_client_config_t config;
    realtime_client_callbacks_t callbacks;
    char url[REALTIME_CLIENT_URL_MAX];
    char api_key[REALTIME_CLIENT_API_KEY_MAX];
    char voice[REALTIME_CLIENT_VOICE_MAX];
    char instructions[REALTIME_CLIENT_INSTRUCTIONS_MAX];
    char headers[REALTIME_CLIENT_API_KEY_MAX + 32];
    char *rx_text;
    char *audio_b64;
    uint8_t *audio_decode;
    char *tx_b64;
    char *tx_message;
    realtime_frame_assembler_t rx_assembler;
    bool session_update_sent;
} realtime_client_ctx_t;

static realtime_client_ctx_t s_ctx = {0};

static void realtime_free_buffers(void) {
    heap_caps_free(s_ctx.rx_text);
    heap_caps_free(s_ctx.audio_b64);
    heap_caps_free(s_ctx.audio_decode);
    heap_caps_free(s_ctx.tx_b64);
    heap_caps_free(s_ctx.tx_message);
    s_ctx.rx_text = NULL;
    s_ctx.audio_b64 = NULL;
    s_ctx.audio_decode = NULL;
    s_ctx.tx_b64 = NULL;
    s_ctx.tx_message = NULL;
    realtime_frame_assembler_init(&s_ctx.rx_assembler, NULL, 0);
}

static esp_err_t realtime_alloc_buffers(void) {
    realtime_free_buffers();

    s_ctx.rx_text = heap_caps_calloc(1, REALTIME_RX_MESSAGE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.audio_b64 = heap_caps_calloc(1, REALTIME_AUDIO_B64_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.audio_decode = heap_caps_calloc(1, REALTIME_AUDIO_DECODE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.tx_b64 = heap_caps_calloc(1, REALTIME_TX_B64_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ctx.tx_message = heap_caps_calloc(1, REALTIME_TX_MESSAGE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_ctx.rx_text == NULL || s_ctx.audio_b64 == NULL || s_ctx.audio_decode == NULL || s_ctx.tx_b64 == NULL ||
        s_ctx.tx_message == NULL) {
        realtime_free_buffers();
        return ESP_ERR_NO_MEM;
    }
    realtime_frame_assembler_init(&s_ctx.rx_assembler, s_ctx.rx_text, REALTIME_RX_MESSAGE_MAX + 1);
    return ESP_OK;
}

static const char *safe_string(const char *value, const char *fallback) {
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static void copy_config(const realtime_client_config_t *config) {
    const char *url = safe_string(config != NULL ? config->url : NULL, CONFIG_WATCHER_AGENT_REALTIME_URL);
    const char *api_key = safe_string(config != NULL ? config->api_key : NULL, CONFIG_WATCHER_AGENT_API_KEY);
    const char *voice = safe_string(config != NULL ? config->voice : NULL, CONFIG_WATCHER_AGENT_VOICE);
    const char *instructions =
        safe_string(config != NULL ? config->instructions : NULL,
                    "You are WatcheRobot's local voice agent. Answer briefly, warmly, and naturally.");

    safe_copy(s_ctx.url, sizeof(s_ctx.url), url);
    safe_copy(s_ctx.api_key, sizeof(s_ctx.api_key), api_key);
    safe_copy(s_ctx.voice, sizeof(s_ctx.voice), voice);
    safe_copy(s_ctx.instructions, sizeof(s_ctx.instructions), instructions);

    s_ctx.config.url = s_ctx.url;
    s_ctx.config.api_key = s_ctx.api_key;
    s_ctx.config.voice = s_ctx.voice;
    s_ctx.config.instructions = s_ctx.instructions;
    s_ctx.config.connect_timeout_ms = config != NULL && config->connect_timeout_ms > 0
                                          ? config->connect_timeout_ms
                                          : CONFIG_WATCHER_AGENT_CONNECT_TIMEOUT_MS;
    s_ctx.config.response_timeout_ms = config != NULL && config->response_timeout_ms > 0
                                           ? config->response_timeout_ms
                                           : CONFIG_WATCHER_AGENT_RESPONSE_TIMEOUT_MS;

    if (s_ctx.api_key[0] != '\0') {
        (void)snprintf(s_ctx.headers, sizeof(s_ctx.headers), "Authorization: Bearer %s\r\n", s_ctx.api_key);
    } else {
        s_ctx.headers[0] = '\0';
    }
}

static esp_err_t realtime_send_text_unlocked(const char *text) {
    int sent;

    if (text == NULL || s_ctx.ws == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sent = esp_websocket_client_send_text(s_ctx.ws, text, (int)strlen(text), pdMS_TO_TICKS(REALTIME_SEND_TIMEOUT_MS));
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t realtime_send_text(const char *text) {
    esp_err_t ret;

    if (s_ctx.send_lock != NULL && xSemaphoreTake(s_ctx.send_lock, pdMS_TO_TICKS(REALTIME_SEND_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    ret = realtime_send_text_unlocked(text);
    if (s_ctx.send_lock != NULL) {
        xSemaphoreGive(s_ctx.send_lock);
    }
    return ret;
}

static void realtime_notify_error(const char *message) {
    s_ctx.state = REALTIME_CLIENT_STATE_ERROR;
    if (s_ctx.callbacks.on_error != NULL) {
        s_ctx.callbacks.on_error(message != NULL ? message : "Realtime error", s_ctx.callbacks.user_ctx);
    }
}

static void realtime_mark_ready(void) {
    if (s_ctx.state == REALTIME_CLIENT_STATE_READY) {
        return;
    }
    s_ctx.state = REALTIME_CLIENT_STATE_READY;
    if (s_ctx.callbacks.on_ready != NULL) {
        s_ctx.callbacks.on_ready(s_ctx.callbacks.user_ctx);
    }
}

static esp_err_t realtime_send_session_update(const char *reason) {
    char message[1024];

    if (s_ctx.session_update_sent) {
        realtime_mark_ready();
        return ESP_OK;
    }
    if (realtime_protocol_build_session_update(&s_ctx.config, message, sizeof(message)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (realtime_send_text(message) != ESP_OK) {
        return ESP_FAIL;
    }

    s_ctx.session_update_sent = true;
    ESP_LOGI(TAG, "Realtime session.update sent (%s)", reason != NULL ? reason : "unspecified");
    realtime_mark_ready();
    return ESP_OK;
}

static void realtime_handle_session_created(void) {
    if (realtime_send_session_update("session.created") != ESP_OK) {
        realtime_notify_error("failed to send session.update");
    }
}

static void realtime_handle_text_event(const char *json) {
    char type[REALTIME_EVENT_TYPE_MAX] = {0};
    char text[REALTIME_TEXT_MAX] = {0};
    size_t audio_len = 0;

    if (realtime_protocol_parse_event_type(json, type, sizeof(type)) != ESP_OK) {
        ESP_LOGW(TAG, "Realtime event without type");
        return;
    }

    if (strcmp(type, "session.created") == 0) {
        realtime_handle_session_created();
        return;
    }
    if (strcmp(type, "conversation.item.input_audio_transcription.delta") == 0) {
        if (realtime_protocol_extract_string(json, "delta", text, sizeof(text)) == ESP_OK &&
            s_ctx.callbacks.on_transcript != NULL) {
            s_ctx.callbacks.on_transcript(text, false, s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
        if (realtime_protocol_extract_string(json, "transcript", text, sizeof(text)) == ESP_OK &&
            s_ctx.callbacks.on_transcript != NULL) {
            s_ctx.callbacks.on_transcript(text, true, s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "response.output_audio_transcript.done") == 0) {
        if (realtime_protocol_extract_string(json, "transcript", text, sizeof(text)) == ESP_OK &&
            s_ctx.callbacks.on_assistant_text != NULL) {
            s_ctx.callbacks.on_assistant_text(text, s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "response.output_audio.delta") == 0 || strcmp(type, "response.audio.delta") == 0) {
        esp_err_t ret;
        if (s_ctx.audio_b64 == NULL || s_ctx.audio_decode == NULL) {
            realtime_notify_error("Realtime audio buffer unavailable");
            return;
        }
        ret = realtime_protocol_decode_audio_delta_with_scratch(json, s_ctx.audio_b64, REALTIME_AUDIO_B64_MAX,
                                                                s_ctx.audio_decode, REALTIME_AUDIO_DECODE_MAX,
                                                                &audio_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Realtime audio delta decode failed: 0x%x", (unsigned)ret);
            realtime_notify_error("Realtime audio delta decode failed");
            return;
        }
        if (s_ctx.callbacks.on_audio != NULL) {
            s_ctx.callbacks.on_audio(s_ctx.audio_decode, audio_len, s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "response.output_audio.done") == 0) {
        if (s_ctx.callbacks.on_audio_done != NULL) {
            s_ctx.callbacks.on_audio_done(s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "response.done") == 0) {
        if (s_ctx.callbacks.on_response_done != NULL) {
            s_ctx.callbacks.on_response_done(s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
        ESP_LOGI(TAG, "Realtime server_vad speech started");
        if (s_ctx.callbacks.on_speech_started != NULL) {
            s_ctx.callbacks.on_speech_started(s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        ESP_LOGI(TAG, "Realtime server_vad speech stopped");
        if (s_ctx.callbacks.on_speech_stopped != NULL) {
            s_ctx.callbacks.on_speech_stopped(s_ctx.callbacks.user_ctx);
        }
        return;
    }
    if (strcmp(type, "error") == 0) {
        if (realtime_protocol_extract_string(json, "message", text, sizeof(text)) != ESP_OK) {
            safe_copy(text, sizeof(text), "Realtime protocol error");
        }
        realtime_notify_error(text);
    }
}

static void realtime_ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    (void)handler_args;
    (void)base;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Realtime connected: %s", s_ctx.url);
        s_ctx.state = REALTIME_CLIENT_STATE_CONNECTING;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Realtime disconnected");
        s_ctx.state = REALTIME_CLIENT_STATE_ERROR;
        if (s_ctx.callbacks.on_closed != NULL) {
            s_ctx.callbacks.on_closed(s_ctx.callbacks.user_ctx);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data != NULL && data->data_ptr != NULL && data->data_len > 0 && s_ctx.rx_text != NULL) {
            const char *complete_text = NULL;
            esp_err_t ret;
            bool is_text = data->op_code == WS_TRANSPORT_OPCODES_TEXT;
            bool is_continuation = data->op_code == WS_TRANSPORT_OPCODES_CONT;

            if (!is_text && !is_continuation) {
                ESP_LOGD(TAG, "Realtime ignored non-text frame op=%u", (unsigned)data->op_code);
                break;
            }
            ret = realtime_frame_assembler_append(&s_ctx.rx_assembler, data->data_ptr, (size_t)data->data_len,
                                                  (size_t)data->payload_len, (size_t)data->payload_offset,
                                                  data->fin != 0, is_text, is_continuation, &complete_text);
            if (ret == ESP_ERR_NO_MEM) {
                ESP_LOGW(TAG, "Realtime message too large op=%u len=%d payload=%d offset=%d fin=%d",
                         (unsigned)data->op_code, data->data_len, data->payload_len, data->payload_offset, data->fin);
                realtime_notify_error("Realtime message too large; lower TTS chunk size");
                break;
            }
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Realtime fragmented frame error ret=0x%x op=%u len=%d payload=%d offset=%d fin=%d",
                         (unsigned)ret, (unsigned)data->op_code, data->data_len, data->payload_len,
                         data->payload_offset, data->fin);
                realtime_notify_error("Realtime fragmented frame error");
                break;
            }
            if (complete_text != NULL) {
                realtime_handle_text_event(complete_text);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "Realtime websocket error");
        realtime_notify_error("Realtime websocket error");
        break;
    default:
        break;
    }
}

esp_err_t realtime_client_start(const realtime_client_config_t *config, const realtime_client_callbacks_t *callbacks) {
    esp_websocket_client_config_t ws_config = {0};
    esp_err_t ret;

    realtime_client_stop("restart");
    memset(&s_ctx, 0, sizeof(s_ctx));
    copy_config(config);
    if (callbacks != NULL) {
        s_ctx.callbacks = *callbacks;
    }

    ret = realtime_alloc_buffers();
    if (ret != ESP_OK) {
        s_ctx.state = REALTIME_CLIENT_STATE_ERROR;
        return ret;
    }

    s_ctx.send_lock = xSemaphoreCreateMutex();
    if (s_ctx.send_lock == NULL) {
        realtime_free_buffers();
        s_ctx.state = REALTIME_CLIENT_STATE_ERROR;
        return ESP_ERR_NO_MEM;
    }

    ws_config.uri = s_ctx.url;
    ws_config.headers = s_ctx.headers[0] != '\0' ? s_ctx.headers : NULL;
    ws_config.disable_auto_reconnect = true;
    ws_config.network_timeout_ms = (int)s_ctx.config.connect_timeout_ms;
    ws_config.buffer_size = REALTIME_WS_BUFFER_SIZE;
    ws_config.task_stack = REALTIME_WS_TASK_STACK;
    ws_config.task_prio = REALTIME_WS_TASK_PRIO;
    ws_config.keep_alive_enable = true;
    ws_config.keep_alive_idle = 5;
    ws_config.keep_alive_interval = 2;
    ws_config.keep_alive_count = 2;

    s_ctx.ws = esp_websocket_client_init(&ws_config);
    if (s_ctx.ws == NULL) {
        realtime_client_stop("init failed");
        return ESP_FAIL;
    }

    ret = esp_websocket_register_events(s_ctx.ws, WEBSOCKET_EVENT_ANY, realtime_ws_event_handler, NULL);
    if (ret != ESP_OK) {
        realtime_client_stop("register failed");
        return ret;
    }
    s_ctx.state = REALTIME_CLIENT_STATE_CONNECTING;
    ret = esp_websocket_client_start(s_ctx.ws);
    if (ret != ESP_OK) {
        realtime_client_stop("start failed");
        return ret;
    }

    ESP_LOGI(TAG, "Realtime client start requested: %s", s_ctx.url);
    return ESP_OK;
}

void realtime_client_stop(const char *reason) {
    if (s_ctx.ws != NULL) {
        ESP_LOGI(TAG, "Stopping Realtime client: %s", reason != NULL ? reason : "unspecified");
        esp_websocket_client_stop(s_ctx.ws);
        esp_websocket_client_destroy(s_ctx.ws);
        s_ctx.ws = NULL;
    }
    if (s_ctx.send_lock != NULL) {
        vSemaphoreDelete(s_ctx.send_lock);
        s_ctx.send_lock = NULL;
    }
    realtime_free_buffers();
    s_ctx.state = REALTIME_CLIENT_STATE_IDLE;
}

void realtime_client_tick(void) {
    if (s_ctx.state == REALTIME_CLIENT_STATE_CONNECTING && s_ctx.ws != NULL && !s_ctx.session_update_sent &&
        esp_websocket_client_is_connected(s_ctx.ws)) {
        if (realtime_send_session_update("connected poll") != ESP_OK) {
            realtime_notify_error("failed to send session.update");
        }
    }
}

bool realtime_client_is_ready(void) {
    return s_ctx.state == REALTIME_CLIENT_STATE_READY && s_ctx.ws != NULL && esp_websocket_client_is_connected(s_ctx.ws);
}

realtime_client_state_t realtime_client_get_state(void) {
    return s_ctx.state;
}

esp_err_t realtime_client_send_audio_pcm(const uint8_t *data, size_t len) {
    esp_err_t ret;

    if (!realtime_client_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.send_lock != NULL && xSemaphoreTake(s_ctx.send_lock, pdMS_TO_TICKS(REALTIME_SEND_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_ctx.tx_b64 == NULL || s_ctx.tx_message == NULL) {
        ret = ESP_ERR_NO_MEM;
    } else {
        ret = realtime_protocol_build_audio_append_with_scratch(data, len, s_ctx.tx_b64, REALTIME_TX_B64_MAX,
                                                                s_ctx.tx_message, REALTIME_TX_MESSAGE_MAX);
    }
    if (ret == ESP_OK) {
        ret = realtime_send_text_unlocked(s_ctx.tx_message);
    }
    if (s_ctx.send_lock != NULL) {
        xSemaphoreGive(s_ctx.send_lock);
    }
    return ret;
}

esp_err_t realtime_client_finish_turn(void) {
    static const uint8_t silence[1920] = {0};
    esp_err_t ret = ESP_OK;

    if (!realtime_client_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 17; ++i) {
        ret = realtime_client_send_audio_pcm(silence, sizeof(silence));
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t realtime_client_cancel_response(const char *reason) {
    char message[64];
    (void)reason;

    if (s_ctx.ws == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (realtime_protocol_build_event("response.cancel", message, sizeof(message)) != ESP_OK) {
        return ESP_FAIL;
    }
    return realtime_send_text(message);
}
