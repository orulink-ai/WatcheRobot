/**
 * @file ws_client.c
 * @brief WebSocket client implementation (Watcher protocol v0.1.5)
 */

#include "ws_client.h"

#include "animation_registry.h"
#include "behavior_state_service.h"
#include "cJSON.h"
#include "control_ingress.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_audio.h"
#include "hal_opus.h"
#include "mbedtls/md.h"
#include "ota_service.h"
#include "sdkconfig.h"
#include "server_pairing.h"
#include "sfx_service.h"
#include "voice_service.h"
#include "ws_audio_codec_negotiation.h"
#include "ws_audio_uplink_policy.h"
#include "ws_event_ui_policy.h"
#include "ws_handlers.h"
#include "ws_router.h"
#include "ws_tts_buffer_policy.h"
#include "ws_tts_stream_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mem_monitor_snapshot(const char *stage);

#define TAG "WS_CLIENT"

#define WS_DEFAULT_URL "ws://[IP_ADDRESS]"
#define WS_NETWORK_TIMEOUT_MS 30000
#define WS_URL_MAX_LEN 128
#define WS_SESSION_PAIRING_CODE_LENGTH 6U
/* Match the proven Agent transport buffer and keep one complete 4096-byte
 * TTS payload plus its WSPK header in a single receive callback. */
#define WS_BUFFER_SIZE 8192
/* Keep the websocket client stack large enough for JSON parsing, routing, and event callbacks
 * without regressing internal-RAM headroom too far for LCD DMA. */
#define WS_TASK_STACK 12288
#define WS_TASK_PRIO 8
#define WS_SEND_TIMEOUT_MS 5000
#define WS_AUDIO_SEND_TIMEOUT_MS 4000
#define WS_SEND_LOCK_TIMEOUT_MS 3000
#define WS_TTS_STATUS_LOCK_TIMEOUT_MS 0
#define WS_TTS_STATUS_SEND_TIMEOUT_MS 20
#define WS_TTS_STATUS_ATTEMPT_MIN_INTERVAL_MS 100U
#define WS_RESPONSE_TIMEOUT_MS 30000
#define WS_BINARY_HEADER_LEN 16
#define WS_BINARY_LEGACY_HEADER_LEN 14
#define WS_BINARY_MAGIC "WSPK"
#define WS_DEVICE_ERROR_CODE_GENERIC 1501
#define WS_TEXT_LOG_MAX_CHARS 256
#ifdef CONFIG_WATCHER_WS_TEXT_MAX_PAYLOAD_BYTES
#define WS_TEXT_MAX_PAYLOAD_BYTES CONFIG_WATCHER_WS_TEXT_MAX_PAYLOAD_BYTES
#else
#define WS_TEXT_MAX_PAYLOAD_BYTES 8192
#endif
#define WS_AUDIO_FRAME_BYTES 1920
#define WS_AUDIO_UPLINK_BATCH_BYTES (WS_AUDIO_FRAME_BYTES * WS_AUDIO_UPLINK_MAX_BATCH_FRAMES)
_Static_assert(WS_AUDIO_UPLINK_BATCH_BYTES + WS_BINARY_LEGACY_HEADER_LEN <= WS_BUFFER_SIZE,
               "audio uplink batch must fit in one WebSocket transport write");
#ifdef CONFIG_WATCHER_WS_AUDIO_QUEUE_DEPTH
#define WS_AUDIO_QUEUE_DEPTH CONFIG_WATCHER_WS_AUDIO_QUEUE_DEPTH
#else
#define WS_AUDIO_QUEUE_DEPTH 128
#endif
_Static_assert(WS_AUDIO_QUEUE_DEPTH >= WS_AUDIO_UPLINK_MAX_BATCH_FRAMES * 2U,
               "audio queue must hold one inflight and one pending batch");
#ifdef CONFIG_WATCHER_WS_AUDIO_STATS_LOG_INTERVAL_FRAMES
#define WS_AUDIO_STATS_LOG_INTERVAL_FRAMES CONFIG_WATCHER_WS_AUDIO_STATS_LOG_INTERVAL_FRAMES
#else
#define WS_AUDIO_STATS_LOG_INTERVAL_FRAMES 60
#endif
#define WS_AUDIO_WORKER_STACK HAL_OPUS_MIN_TASK_STACK_BYTES
#define WS_AUDIO_WORKER_PRIO 6
#define WS_AUDIO_WORKER_WAIT_MS 20
#define WS_AUDIO_WORKER_EXIT_WAIT_MS 300
#define WS_AUDIO_SLOW_SEND_WARN_US 100000U
#define WS_AUDIO_INITIAL_SEND_LOG_BATCHES 3U
#define WS_TTS_FRAME_BYTES 4096
#define WS_TTS_WIRE_FRAME_BYTES (WS_TTS_FRAME_BYTES + WS_BINARY_HEADER_LEN)
_Static_assert(WS_TTS_WIRE_FRAME_BYTES <= WS_BUFFER_SIZE,
               "one server TTS frame must fit in one WebSocket receive buffer");
#ifdef CONFIG_WATCHER_WS_TTS_QUEUE_DEPTH
#define WS_TTS_QUEUE_DEPTH CONFIG_WATCHER_WS_TTS_QUEUE_DEPTH
#else
#define WS_TTS_QUEUE_DEPTH 16
#endif
#ifdef CONFIG_WATCHER_WS_TTS_MAX_PAYLOAD_BYTES
#define WS_TTS_MAX_PAYLOAD_BYTES CONFIG_WATCHER_WS_TTS_MAX_PAYLOAD_BYTES
#else
#define WS_TTS_MAX_PAYLOAD_BYTES 32768
#endif
#ifdef CONFIG_WATCHER_WS_TTS_RX_LOG_INTERVAL_FRAMES
#define WS_TTS_RX_LOG_INTERVAL_FRAMES CONFIG_WATCHER_WS_TTS_RX_LOG_INTERVAL_FRAMES
#else
#define WS_TTS_RX_LOG_INTERVAL_FRAMES 12
#endif
#define WS_TTS_WORKER_STACK 6144
#define WS_TTS_WORKER_PRIO 7
#define WS_TTS_WORKER_WAIT_MS 20
#define WS_TTS_WORKER_EXIT_WAIT_MS 500
#define WS_TTS_DMA_DRAIN_TIMEOUT_MS 1200U
#define WS_TTS_SERVER_EOS_FALLBACK_MS 1500U
#ifdef CONFIG_WATCHER_WS_TTS_START_BUFFER_FRAMES
#define WS_TTS_START_BUFFER_FRAMES CONFIG_WATCHER_WS_TTS_START_BUFFER_FRAMES
#else
#define WS_TTS_START_BUFFER_FRAMES 4U
#endif
#define WS_TTS_EFFECTIVE_START_BUFFER_FRAMES                                                                           \
    ((WS_TTS_START_BUFFER_FRAMES > WS_TTS_QUEUE_DEPTH) ? WS_TTS_QUEUE_DEPTH : WS_TTS_START_BUFFER_FRAMES)
#ifdef CONFIG_WATCHER_WS_TTS_REBUFFER_FRAMES
#define WS_TTS_REBUFFER_FRAMES CONFIG_WATCHER_WS_TTS_REBUFFER_FRAMES
#else
#define WS_TTS_REBUFFER_FRAMES 2U
#endif
#define WS_TTS_EFFECTIVE_REBUFFER_FRAMES                                                                               \
    ((WS_TTS_REBUFFER_FRAMES > WS_TTS_QUEUE_DEPTH) ? WS_TTS_QUEUE_DEPTH : WS_TTS_REBUFFER_FRAMES)
#define WS_TTS_START_BUFFER_BYTES ((uint32_t)WS_TTS_EFFECTIVE_START_BUFFER_FRAMES * WS_TTS_FRAME_BYTES)
#define WS_TTS_REBUFFER_BYTES ((uint32_t)WS_TTS_EFFECTIVE_REBUFFER_FRAMES * WS_TTS_FRAME_BYTES)
#ifdef CONFIG_WATCHER_WS_TTS_STARVATION_WARN_MS
#define WS_TTS_STARVATION_WARN_MS CONFIG_WATCHER_WS_TTS_STARVATION_WARN_MS
#else
#define WS_TTS_STARVATION_WARN_MS 60U
#endif
#ifdef CONFIG_WATCHER_WS_TTS_BUFFER_STATUS_INTERVAL_MS
#define WS_TTS_BUFFER_STATUS_INTERVAL_MS CONFIG_WATCHER_WS_TTS_BUFFER_STATUS_INTERVAL_MS
#else
#define WS_TTS_BUFFER_STATUS_INTERVAL_MS 200U
#endif
#define WS_TTS_BYTES_PER_SECOND (24000U * 2U)
#ifdef CONFIG_WATCHER_WS_TTS_ENQUEUE_TIMEOUT_MS
#define WS_TTS_ENQUEUE_TIMEOUT_MS CONFIG_WATCHER_WS_TTS_ENQUEUE_TIMEOUT_MS
#else
#define WS_TTS_ENQUEUE_TIMEOUT_MS 1500
#endif
#define WS_HELLO_UI_MIN_INTERNAL_FREE_BYTES (24U * 1024U)
#define WS_HELLO_UI_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)

static esp_websocket_client_handle_t s_ws_client = NULL;
static bool s_ws_started = false;
static bool s_socket_connected = false;
static bool s_hello_acknowledged = false;
static bool s_hello_rejected = false;
static bool s_tts_playing = false;
static bool s_waiting_for_response = false;
static bool s_audio_upload_active = false;
static ws_audio_uplink_codec_t s_audio_uplink_codec = WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
static bool s_ota_binary_nacked = false;
static bool s_behavior_feedback_enabled = true;
static bool s_allow_wake_word_resume = true;
static volatile bool s_ws_deferred_cleanup_pending = false;
static int s_timeout_display_count = 0;
static int64_t s_response_wait_start_time = 0;
static int64_t s_last_session_activity_us = 0;
static char s_ws_server_url[WS_URL_MAX_LEN] = WS_DEFAULT_URL;
static char s_session_pairing_code[WS_SESSION_PAIRING_CODE_LENGTH + 1U] = {0};
static SemaphoreHandle_t s_ws_send_lock = NULL;
static uint32_t s_frame_sequences[WS_FRAME_TYPE_APP_PACKAGE + 1] = {0};
static ws_app_package_handler_t s_app_package_handler = {0};
static ws_client_text_handler_t s_text_handler = NULL;
static void *s_text_handler_context = NULL;
static ws_client_tts_frame_guard_t s_tts_frame_guard = NULL;
static void *s_tts_frame_guard_context = NULL;
static bool s_default_router_ready = false;
static ws_client_media_send_stats_t s_last_media_send_stats = {0};
static ws_client_audio_queue_stats_t s_last_audio_queue_stats = {0};

typedef struct {
    uint8_t data[WS_AUDIO_FRAME_BYTES];
    uint16_t len;
    ws_audio_uplink_codec_t codec;
    uint64_t enqueued_us;
} ws_audio_frame_slot_t;

typedef struct {
    uint8_t data[WS_TTS_FRAME_BYTES];
    uint16_t len;
    uint64_t enqueued_us;
} ws_tts_frame_slot_t;

/* Audio queue payloads do not need DMA-capable internal RAM; keep them in PSRAM
 * so LCD/SPI and websocket runtime retain more internal headroom. */
EXT_RAM_BSS_ATTR static ws_audio_frame_slot_t s_audio_frame_pool[WS_AUDIO_QUEUE_DEPTH];
EXT_RAM_BSS_ATTR static uint8_t s_audio_uplink_batch[WS_AUDIO_UPLINK_BATCH_BYTES];
EXT_RAM_BSS_ATTR static ws_tts_frame_slot_t s_tts_frame_pool[WS_TTS_QUEUE_DEPTH];
static QueueHandle_t s_audio_free_slots = NULL;
static QueueHandle_t s_audio_pending_slots = NULL;
static SemaphoreHandle_t s_audio_slot_lock = NULL;
static SemaphoreHandle_t s_audio_state_lock = NULL;
static TaskHandle_t s_audio_worker_task = NULL;
static volatile bool s_audio_worker_running = false;
static uint8_t s_audio_inflight_slots[WS_AUDIO_UPLINK_MAX_BATCH_FRAMES] = {0};
static uint8_t s_audio_inflight_count = 0;
static uint32_t s_audio_session_generation = 1U;
static uint32_t s_audio_sent_batches = 0U;
static uint64_t s_audio_last_batch_elapsed_us = 0U;
static ws_audio_uplink_policy_t s_audio_uplink_policy = {0};
static bool s_audio_session_open = false;
static bool s_audio_first_frame_pending = false;
static bool s_audio_end_pending = false;
static bool s_audio_cancel_pending = false;
static uint32_t s_audio_queued_frames = 0;
static uint32_t s_audio_sent_frames = 0;
static uint32_t s_audio_dropped_frames = 0;
static uint32_t s_audio_high_watermark = 0;
static uint32_t s_audio_last_queue_delay_us = 0;
static QueueHandle_t s_tts_free_slots = NULL;
static QueueHandle_t s_tts_pending_slots = NULL;
static SemaphoreHandle_t s_tts_queue_lock = NULL;
/* Serializes I2S writes with local reset/stop so a replaced stream cannot leak one stale frame. */
static SemaphoreHandle_t s_tts_playback_lock = NULL;
static TaskHandle_t s_tts_worker_task = NULL;
static volatile bool s_tts_worker_running = false;
static bool s_tts_inflight_active = false;
static uint8_t s_tts_inflight_slot = 0;
static uint32_t s_tts_queue_generation = 0;
static bool s_tts_end_pending = false;
static int64_t s_tts_server_eos_deadline_us = 0;
static uint32_t s_tts_server_eos_generation = 0;
static uint16_t s_tts_server_eos_stream_id = 0;
static bool s_tts_session_stats_active = false;
static uint64_t s_tts_inbound_bytes = 0;
static uint64_t s_tts_enqueue_wait_total_ms = 0;
static uint32_t s_tts_enqueued_frames = 0;
static uint32_t s_tts_played_frames = 0;
static uint32_t s_tts_dropped_frames = 0;
static uint32_t s_tts_drop_timeout_frames = 0;
static uint32_t s_tts_high_watermark = 0;
static uint32_t s_tts_enqueue_wait_events = 0;
static uint32_t s_tts_enqueue_wait_max_ms = 0;
static bool s_tts_starvation_active = false;
static bool s_tts_starvation_warned = false;
static uint64_t s_tts_starvation_started_us = 0;
static uint64_t s_tts_starvation_total_ms = 0;
static uint32_t s_tts_starvation_events = 0;
static uint32_t s_tts_starvation_max_ms = 0;
static uint32_t s_tts_rx_frames = 0;
static uint32_t s_tts_rx_suppressed_logs = 0;
static uint64_t s_tts_buffer_status_last_us = 0;
static uint64_t s_tts_buffer_status_attempt_last_us = 0;
typedef enum {
    WS_TTS_DEFERRED_STATUS_NONE = 0,
    WS_TTS_DEFERRED_STATUS_ENQUEUE_TIMEOUT,
    WS_TTS_DEFERRED_STATUS_ENQUEUE_FAILED,
} ws_tts_deferred_status_t;
static ws_tts_deferred_status_t s_tts_deferred_status = WS_TTS_DEFERRED_STATUS_NONE;
static ws_tts_buffer_policy_t s_tts_buffer_policy = {0};
static bool s_tts_stream_seq_active = false;
static uint16_t s_tts_current_stream_id = 0;
static uint32_t s_tts_expected_rx_seq = 0;
static uint32_t s_tts_last_rx_seq = 0;
static uint32_t s_tts_seq_duplicate_frames = 0;
static uint32_t s_tts_seq_gap_frames = 0;
static uint32_t s_tts_last_gap_expected_seq = 0;
static uint32_t s_tts_last_gap_received_seq = 0;
static uint32_t s_tts_stale_stream_frames = 0;
static uint32_t s_tts_stream_replacements = 0;
static mbedtls_md_context_t s_tts_rx_sha256_ctx;
static bool s_tts_rx_sha256_initialized = false;
static bool s_tts_rx_sha256_failed = false;
static char s_tts_rx_audio_sha256[65] = "";

typedef struct {
    char *buffer;
    size_t total_len;
    size_t received_len;
    bool active;
    bool dropping;
} ws_text_fragment_state_t;

typedef struct {
    uint8_t *payload_buffer;
    size_t total_len;
    size_t received_total;
    size_t payload_len;
    size_t payload_received;
    size_t header_len;
    size_t parsed_header_len;
    uint16_t stream_id;
    uint32_t seq;
    uint8_t frame_type;
    uint8_t flags;
    bool active;
    bool header_parsed;
    bool audio_frame_accepted;
    uint8_t header[WS_BINARY_HEADER_LEN];
} ws_binary_fragment_state_t;

static ws_text_fragment_state_t s_text_fragment_state = {0};
static ws_binary_fragment_state_t s_binary_fragment_state = {0};
static int64_t s_last_send_block_log_us = 0;
static bool s_last_send_block_socket_connected = false;
static bool s_last_send_block_hello_acknowledged = false;
static bool s_last_send_block_allow_before_session = false;
static bool s_last_send_block_binary = false;
static int s_last_send_block_len = -1;

static esp_err_t ws_audio_queue_init(void);
static void ws_audio_queue_reset_locked(void);
static void ws_audio_update_stats_locked(void);
static void ws_audio_worker_task(void *arg);
static bool ws_wait_for_audio_worker_exit(uint32_t timeout_ms);
static void ws_audio_runtime_deinit(void);
static int ws_send_binary_packet(ws_frame_type_t frame_type, uint8_t flags, const uint8_t *payload, size_t len);
static int ws_send_audio_packet(const uint8_t *data, size_t len, uint8_t flags);
static bool ws_audio_session_ready(void);
static bool ws_client_has_hello_ui_headroom(void);
static bool ws_media_runtime_accepts_work(void);
static void ws_client_stop_internal(bool resume_wake_word);
void ws_handlers_set_app_package_handler(const ws_app_package_handler_t *handler);
static esp_err_t ws_tts_runtime_init(void);
static void ws_tts_runtime_deinit(void);
static void ws_tts_queue_reset_locked(void);
static void ws_tts_rx_sha256_reset_locked(void);
static bool ws_tts_rx_sha256_start_locked(void);
static void ws_tts_rx_sha256_update_locked(const uint8_t *data, size_t len);
static void ws_tts_rx_sha256_finish_locked(void);
static void ws_tts_worker_task(void *arg);
static bool ws_wait_for_tts_worker_exit(uint32_t timeout_ms);
static void ws_abort_tts_playback_internal(bool send_status, bool resume_wake_word);
static bool ws_tts_take_free_slot(uint8_t *slot_idx, uint32_t *waited_ms, uint32_t timeout_ms);
static void ws_tts_record_enqueue_wait_locked(uint32_t waited_ms);
static void ws_tts_start_starvation_locked(void);
static void ws_tts_finish_starvation_locked(void);
static bool ws_tts_should_warn_starvation_locked(uint32_t *starvation_ms, uint32_t *enqueued_frames,
                                                 uint32_t *played_frames, uint32_t *high_watermark);
static void ws_tts_log_session_stats(const char *reason);
static int ws_send_tts_buffer_status(const char *reason, bool force);
static int ws_send_tts_buffer_status_for_stream(const char *reason, bool force, uint16_t stream_id);
static int ws_maybe_send_tts_buffer_status(const char *reason);
static void ws_finish_tts_playback(uint32_t generation, uint16_t stream_id);
static bool ws_prepare_tts_playback(bool recovering_existing_stream);
static void ws_idle_audio_after_media(void);
static void ws_defer_wake_word_until_sleep(void);
static bool ws_tts_accept_incoming_frame(uint8_t flags, uint16_t stream_id, uint32_t seq);

static void ws_log_send_blocked(bool binary, int len, bool allow_before_session) {
    int64_t now_us = esp_timer_get_time();
    bool state_changed = s_last_send_block_socket_connected != s_socket_connected ||
                         s_last_send_block_hello_acknowledged != s_hello_acknowledged ||
                         s_last_send_block_allow_before_session != allow_before_session ||
                         s_last_send_block_binary != binary || s_last_send_block_len != len;

    if (!state_changed && (now_us - s_last_send_block_log_us) < 1000000LL) {
        return;
    }

    s_last_send_block_log_us = now_us;
    s_last_send_block_socket_connected = s_socket_connected;
    s_last_send_block_hello_acknowledged = s_hello_acknowledged;
    s_last_send_block_allow_before_session = allow_before_session;
    s_last_send_block_binary = binary;
    s_last_send_block_len = len;

    ESP_LOGW(TAG,
             "send blocked: binary=%d len=%d ws_client=%p socket_connected=%d hello_ack=%d allow_before_session=%d",
             binary, len, (void *)s_ws_client, s_socket_connected, s_hello_acknowledged, allow_before_session);
}

static bool ws_audio_session_ready(void) {
    return s_ws_client != NULL && s_socket_connected && s_hello_acknowledged;
}

static bool ws_client_has_hello_ui_headroom(void) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    return free_internal >= WS_HELLO_UI_MIN_INTERNAL_FREE_BYTES &&
           largest_internal >= WS_HELLO_UI_MIN_INTERNAL_LARGEST_BYTES;
}

static void *ws_alloc_rx_buffer(size_t size) {
    void *buffer = NULL;

    if (size == 0U) {
        return NULL;
    }

    buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return buffer;
}

static bool ws_should_log_tts_rx_frame(uint8_t flags) {
    if ((flags & WS_FRAME_FLAG_FIRST) != 0U) {
        s_tts_rx_frames = 0;
        s_tts_rx_suppressed_logs = 0;
    }

    s_tts_rx_frames++;

    if ((flags & (WS_FRAME_FLAG_FIRST | WS_FRAME_FLAG_LAST)) != 0U) {
        return true;
    }

#if WS_TTS_RX_LOG_INTERVAL_FRAMES > 0
    if ((s_tts_rx_frames % WS_TTS_RX_LOG_INTERVAL_FRAMES) == 0U) {
        return true;
    }
#endif

    s_tts_rx_suppressed_logs++;
    return false;
}

static void ws_tts_digest_to_hex(const unsigned char *digest, size_t digest_len, char *out, size_t out_len) {
    size_t offset = 0;
    size_t i;

    if (digest == NULL || out == NULL || out_len == 0U) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < digest_len && offset + 2U < out_len; ++i) {
        offset += (size_t)snprintf(out + offset, out_len - offset, "%02x", digest[i]);
    }
    out[out_len - 1U] = '\0';
}

static void ws_tts_rx_sha256_reset_locked(void) {
    if (s_tts_rx_sha256_initialized) {
        mbedtls_md_free(&s_tts_rx_sha256_ctx);
        s_tts_rx_sha256_initialized = false;
    }
    s_tts_rx_sha256_failed = false;
    s_tts_rx_audio_sha256[0] = '\0';
}

static bool ws_tts_rx_sha256_start_locked(void) {
    const mbedtls_md_info_t *info;

    ws_tts_rx_sha256_reset_locked();
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        s_tts_rx_sha256_failed = true;
        return false;
    }

    mbedtls_md_init(&s_tts_rx_sha256_ctx);
    if (mbedtls_md_setup(&s_tts_rx_sha256_ctx, info, 0) != 0 || mbedtls_md_starts(&s_tts_rx_sha256_ctx) != 0) {
        mbedtls_md_free(&s_tts_rx_sha256_ctx);
        s_tts_rx_sha256_failed = true;
        return false;
    }

    s_tts_rx_sha256_initialized = true;
    return true;
}

static void ws_tts_rx_sha256_update_locked(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0U || s_tts_rx_sha256_failed) {
        return;
    }
    if (!s_tts_rx_sha256_initialized && !ws_tts_rx_sha256_start_locked()) {
        return;
    }
    if (mbedtls_md_update(&s_tts_rx_sha256_ctx, data, len) != 0) {
        mbedtls_md_free(&s_tts_rx_sha256_ctx);
        s_tts_rx_sha256_initialized = false;
        s_tts_rx_sha256_failed = true;
        s_tts_rx_audio_sha256[0] = '\0';
    }
}

static void ws_tts_rx_sha256_finish_locked(void) {
    unsigned char digest[32];

    if (!s_tts_rx_sha256_initialized) {
        return;
    }
    if (mbedtls_md_finish(&s_tts_rx_sha256_ctx, digest) == 0) {
        ws_tts_digest_to_hex(digest, sizeof(digest), s_tts_rx_audio_sha256, sizeof(s_tts_rx_audio_sha256));
    } else {
        s_tts_rx_audio_sha256[0] = '\0';
        s_tts_rx_sha256_failed = true;
    }
    mbedtls_md_free(&s_tts_rx_sha256_ctx);
    s_tts_rx_sha256_initialized = false;
}

static void ws_tts_queue_reset_locked(void) {
    uint8_t slot_idx;
    bool inflight_active = s_tts_inflight_active;
    uint8_t inflight_slot = s_tts_inflight_slot;

    if (s_tts_free_slots == NULL || s_tts_pending_slots == NULL) {
        return;
    }

    s_tts_queue_generation++;
    xQueueReset(s_tts_free_slots);
    xQueueReset(s_tts_pending_slots);
    for (slot_idx = 0; slot_idx < WS_TTS_QUEUE_DEPTH; ++slot_idx) {
        if (!inflight_active || slot_idx != inflight_slot) {
            (void)xQueueSendToBack(s_tts_free_slots, &slot_idx, 0);
        }
    }

    s_tts_end_pending = false;
    s_tts_server_eos_deadline_us = 0;
    s_tts_server_eos_generation = 0;
    s_tts_server_eos_stream_id = 0;
    s_tts_session_stats_active = false;
    s_tts_inbound_bytes = 0;
    s_tts_enqueue_wait_total_ms = 0;
    s_tts_enqueued_frames = 0;
    s_tts_played_frames = 0;
    s_tts_dropped_frames = 0;
    s_tts_drop_timeout_frames = 0;
    s_tts_high_watermark = 0;
    s_tts_enqueue_wait_events = 0;
    s_tts_enqueue_wait_max_ms = 0;
    s_tts_starvation_active = false;
    s_tts_starvation_warned = false;
    s_tts_starvation_started_us = 0;
    s_tts_starvation_total_ms = 0;
    s_tts_starvation_events = 0;
    s_tts_starvation_max_ms = 0;
    s_tts_rx_frames = 0;
    s_tts_rx_suppressed_logs = 0;
    s_tts_buffer_status_last_us = 0;
    s_tts_buffer_status_attempt_last_us = 0;
    s_tts_deferred_status = WS_TTS_DEFERRED_STATUS_NONE;
    s_tts_stream_seq_active = false;
    s_tts_current_stream_id = 0;
    s_tts_expected_rx_seq = 0;
    s_tts_last_rx_seq = 0;
    s_tts_seq_duplicate_frames = 0;
    s_tts_seq_gap_frames = 0;
    s_tts_last_gap_expected_seq = 0;
    s_tts_last_gap_received_seq = 0;
    s_tts_stale_stream_frames = 0;
    s_tts_stream_replacements = 0;
    ws_tts_buffer_policy_reset(&s_tts_buffer_policy);
    ws_tts_rx_sha256_reset_locked();
}

static void ws_tts_record_enqueue_wait_locked(uint32_t waited_ms) {
    if (waited_ms == 0U) {
        return;
    }

    s_tts_enqueue_wait_events++;
    s_tts_enqueue_wait_total_ms += waited_ms;
    if (waited_ms > s_tts_enqueue_wait_max_ms) {
        s_tts_enqueue_wait_max_ms = waited_ms;
    }
}

static void ws_tts_start_starvation_locked(void) {
    if (!s_tts_session_stats_active || s_tts_starvation_active) {
        return;
    }

    s_tts_starvation_active = true;
    s_tts_starvation_warned = false;
    s_tts_starvation_started_us = (uint64_t)esp_timer_get_time();
}

static uint32_t ws_tts_current_starvation_ms_locked(void) {
    uint64_t now_us;

    if (!s_tts_starvation_active || s_tts_starvation_started_us == 0U) {
        return 0U;
    }

    now_us = (uint64_t)esp_timer_get_time();
    if (now_us <= s_tts_starvation_started_us) {
        return 0U;
    }

    return (uint32_t)((now_us - s_tts_starvation_started_us) / 1000U);
}

static void ws_tts_finish_starvation_locked(void) {
    uint32_t starvation_ms = ws_tts_current_starvation_ms_locked();

    if (!s_tts_starvation_active) {
        return;
    }

    if (starvation_ms >= (uint32_t)WS_TTS_STARVATION_WARN_MS) {
        s_tts_starvation_events++;
        s_tts_starvation_total_ms += starvation_ms;
        if (starvation_ms > s_tts_starvation_max_ms) {
            s_tts_starvation_max_ms = starvation_ms;
        }
    }

    s_tts_starvation_active = false;
    s_tts_starvation_warned = false;
    s_tts_starvation_started_us = 0;
}

static bool ws_tts_should_warn_starvation_locked(uint32_t *starvation_ms, uint32_t *enqueued_frames,
                                                 uint32_t *played_frames, uint32_t *high_watermark) {
    uint32_t elapsed_ms;

    if (starvation_ms == NULL || enqueued_frames == NULL || played_frames == NULL || high_watermark == NULL ||
        !s_tts_starvation_active || s_tts_starvation_warned) {
        return false;
    }

    elapsed_ms = ws_tts_current_starvation_ms_locked();
    if (elapsed_ms < (uint32_t)WS_TTS_STARVATION_WARN_MS) {
        return false;
    }

    s_tts_starvation_warned = true;
    *starvation_ms = elapsed_ms;
    *enqueued_frames = s_tts_enqueued_frames;
    *played_frames = s_tts_played_frames;
    *high_watermark = s_tts_high_watermark;
    return true;
}

static bool ws_tts_take_free_slot(uint8_t *slot_idx, uint32_t *waited_ms, uint32_t timeout_ms) {
    TickType_t start_ticks;
    TickType_t timeout_ticks;
    TickType_t poll_ticks;

    if (slot_idx == NULL || waited_ms == NULL || s_tts_queue_lock == NULL || s_tts_free_slots == NULL) {
        return false;
    }

    start_ticks = xTaskGetTickCount();
    timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    poll_ticks = pdMS_TO_TICKS(WS_TTS_WORKER_WAIT_MS);
    if (poll_ticks == 0) {
        poll_ticks = 1;
    }

    while (true) {
        TickType_t elapsed_ticks;

        TickType_t wait_ticks = 0;

        if (timeout_ticks > 0) {
            elapsed_ticks = xTaskGetTickCount() - start_ticks;
            if (elapsed_ticks >= timeout_ticks) {
                *waited_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
                return false;
            }
            wait_ticks = timeout_ticks - elapsed_ticks;
            if (wait_ticks > poll_ticks) {
                wait_ticks = poll_ticks;
            }
        }

        if (xSemaphoreTake(s_tts_queue_lock, wait_ticks) == pdTRUE) {
            if (xQueueReceive(s_tts_free_slots, slot_idx, 0) == pdTRUE) {
                elapsed_ticks = xTaskGetTickCount() - start_ticks;
                *waited_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
                /* Keep the queue lock held so reset/abort cannot reclaim this reserved slot before enqueue. */
                return true;
            }
            xSemaphoreGive(s_tts_queue_lock);
        }

        elapsed_ticks = xTaskGetTickCount() - start_ticks;
        if (elapsed_ticks >= timeout_ticks) {
            *waited_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
            return false;
        }

        if (timeout_ticks == 0) {
            *waited_ms = 0;
            return false;
        }

        {
            TickType_t delay_ticks = timeout_ticks - elapsed_ticks;
            if (delay_ticks > poll_ticks) {
                delay_ticks = poll_ticks;
            }
            vTaskDelay(delay_ticks);
        }
    }
}

static void ws_tts_log_session_stats(const char *reason) {
    uint64_t inbound_bytes;
    uint64_t wait_total_ms;
    uint32_t enqueued_frames;
    uint32_t played_frames;
    uint32_t dropped_frames;
    uint32_t drop_timeout_frames;
    uint32_t high_watermark;
    uint32_t wait_events;
    uint32_t wait_max_ms;
    uint64_t starvation_total_ms;
    uint32_t starvation_events;
    uint32_t starvation_max_ms;
    uint32_t rx_frames;
    uint32_t seq_duplicate_frames;
    uint32_t seq_gap_frames;
    uint32_t stale_stream_frames;
    uint32_t stream_replacements;
    char audio_sha256[65];

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (!s_tts_session_stats_active) {
        xSemaphoreGive(s_tts_queue_lock);
        return;
    }

    ws_tts_finish_starvation_locked();
    ws_tts_rx_sha256_finish_locked();
    inbound_bytes = s_tts_inbound_bytes;
    wait_total_ms = s_tts_enqueue_wait_total_ms;
    enqueued_frames = s_tts_enqueued_frames;
    played_frames = s_tts_played_frames;
    dropped_frames = s_tts_dropped_frames;
    drop_timeout_frames = s_tts_drop_timeout_frames;
    high_watermark = s_tts_high_watermark;
    wait_events = s_tts_enqueue_wait_events;
    wait_max_ms = s_tts_enqueue_wait_max_ms;
    starvation_total_ms = s_tts_starvation_total_ms;
    starvation_events = s_tts_starvation_events;
    starvation_max_ms = s_tts_starvation_max_ms;
    rx_frames = s_tts_rx_frames;
    seq_duplicate_frames = s_tts_seq_duplicate_frames;
    seq_gap_frames = s_tts_seq_gap_frames;
    stale_stream_frames = s_tts_stale_stream_frames;
    stream_replacements = s_tts_stream_replacements;
    snprintf(audio_sha256, sizeof(audio_sha256), "%s", s_tts_rx_audio_sha256);
    s_tts_session_stats_active = false;
    xSemaphoreGive(s_tts_queue_lock);

    ESP_LOGI(TAG,
             "TTS session %s: inbound_bytes=%llu enqueued_frames=%lu played_frames=%lu dropped_frames=%lu "
             "drop_timeout=%lu high=%lu enqueue_wait_events=%lu enqueue_wait_total_ms=%llu "
             "enqueue_wait_max_ms=%lu queue_starve_events=%lu queue_starve_total_ms=%llu "
             "queue_starve_max_ms=%lu rx_frames=%lu seq_dup=%lu seq_gap=%lu stale_stream=%lu "
             "stream_replacements=%lu audio_sha256=%s start_buffer=%u rebuffer=%u queue_depth=%u",
             reason, (unsigned long long)inbound_bytes, (unsigned long)enqueued_frames, (unsigned long)played_frames,
             (unsigned long)dropped_frames, (unsigned long)drop_timeout_frames, (unsigned long)high_watermark,
             (unsigned long)wait_events, (unsigned long long)wait_total_ms, (unsigned long)wait_max_ms,
             (unsigned long)starvation_events, (unsigned long long)starvation_total_ms,
             (unsigned long)starvation_max_ms, (unsigned long)rx_frames, (unsigned long)seq_duplicate_frames,
             (unsigned long)seq_gap_frames, (unsigned long)stale_stream_frames, (unsigned long)stream_replacements,
             audio_sha256[0] != '\0' ? audio_sha256 : "none", (unsigned int)WS_TTS_EFFECTIVE_START_BUFFER_FRAMES,
             (unsigned int)WS_TTS_EFFECTIVE_REBUFFER_FRAMES, (unsigned int)WS_TTS_QUEUE_DEPTH);
}

static esp_err_t ws_tts_runtime_init(void) {
    bool need_reset = false;

    if (!ws_media_runtime_accepts_work()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_tts_queue_lock == NULL) {
        s_tts_queue_lock = xSemaphoreCreateMutex();
        if (s_tts_queue_lock == NULL) {
            ESP_LOGE(TAG, "failed to create tts queue lock");
            ws_tts_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_tts_playback_lock == NULL) {
        s_tts_playback_lock = xSemaphoreCreateMutex();
        if (s_tts_playback_lock == NULL) {
            ESP_LOGE(TAG, "failed to create tts playback lock");
            ws_tts_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_tts_free_slots == NULL) {
        s_tts_free_slots = xQueueCreate(WS_TTS_QUEUE_DEPTH, sizeof(uint8_t));
        if (s_tts_free_slots == NULL) {
            ESP_LOGE(TAG, "failed to create tts free slot queue");
            ws_tts_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_tts_pending_slots == NULL) {
        s_tts_pending_slots = xQueueCreate(WS_TTS_QUEUE_DEPTH, sizeof(uint8_t));
        if (s_tts_pending_slots == NULL) {
            ESP_LOGE(TAG, "failed to create tts pending queue");
            ws_tts_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_tts_worker_task == NULL) {
        s_tts_worker_running = true;
        BaseType_t task_ret;

        task_ret = xTaskCreate(ws_tts_worker_task, "ws_tts_play", WS_TTS_WORKER_STACK, NULL, WS_TTS_WORKER_PRIO,
                               &s_tts_worker_task);
        if (task_ret != pdPASS) {
            s_tts_worker_running = false;
            ESP_LOGE(TAG, "failed to create tts playback worker");
            ws_tts_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    } else if (!s_tts_worker_running) {
        s_tts_worker_running = true;
        need_reset = true;
    }

    if (need_reset && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        ws_tts_queue_reset_locked();
        xSemaphoreGive(s_tts_queue_lock);
    }

    return ESP_OK;
}

static bool ws_tts_stream_id_is_newer(uint16_t candidate, uint16_t current) {
    uint16_t distance;

    if (candidate == 0U || current == 0U || candidate == current) {
        return false;
    }

    distance = (uint16_t)(candidate - current);
    return distance < 0x8000U;
}

static void ws_tts_mark_stream_sequence_locked(uint16_t stream_id, uint32_t seq) {
    s_tts_stream_seq_active = true;
    s_tts_current_stream_id = stream_id;
    s_tts_last_rx_seq = seq;
    s_tts_expected_rx_seq = seq + 1U;
}

static uint32_t ws_tts_pending_frames_locked(void) {
    return s_tts_pending_slots != NULL ? (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots) : 0U;
}

static bool ws_tts_replace_existing_stream(uint16_t stream_id, uint32_t seq) {
    bool should_replace = false;
    bool should_continue = false;
    bool was_playing;
    uint32_t pending_frames = 0;
    uint32_t enqueued_frames = 0;
    uint32_t played_frames = 0;
    uint32_t expected_seq = 0;
    uint16_t current_stream_id = 0;

    if (ws_tts_runtime_init() != ESP_OK) {
        return false;
    }

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    pending_frames = ws_tts_pending_frames_locked();
    enqueued_frames = s_tts_enqueued_frames;
    played_frames = s_tts_played_frames;
    current_stream_id = s_tts_current_stream_id;
    if (s_tts_stream_seq_active && current_stream_id != 0U && stream_id == current_stream_id &&
        seq < s_tts_expected_rx_seq) {
        expected_seq = s_tts_expected_rx_seq;
        s_tts_seq_duplicate_frames++;
        s_tts_dropped_frames++;
        xSemaphoreGive(s_tts_queue_lock);
        ESP_LOGW(TAG, "dropping duplicate TTS FIRST frame: stream_id=%u seq=%lu expected=%lu", (unsigned int)stream_id,
                 (unsigned long)seq, (unsigned long)expected_seq);
        (void)ws_send_tts_buffer_status("stale_frame", true);
        return false;
    }
    if (s_tts_stream_seq_active && current_stream_id != 0U &&
        (stream_id == 0U ||
         (stream_id != current_stream_id && !ws_tts_stream_id_is_newer(stream_id, current_stream_id)))) {
        s_tts_stale_stream_frames++;
        s_tts_dropped_frames++;
        xSemaphoreGive(s_tts_queue_lock);
        ESP_LOGW(TAG, "dropping stale TTS FIRST frame: stream_id=%u current_stream_id=%u seq=%lu",
                 (unsigned int)stream_id, (unsigned int)current_stream_id, (unsigned long)seq);
        (void)ws_send_tts_buffer_status("stale_stream", true);
        return false;
    }
    should_continue = ws_tts_stream_should_continue(current_stream_id, stream_id, s_tts_end_pending, s_tts_playing,
                                                    pending_frames, s_tts_inflight_active);
    if (should_continue) {
        s_tts_end_pending = false;
        ws_tts_mark_stream_sequence_locked(stream_id, seq);
        xSemaphoreGive(s_tts_queue_lock);
        ESP_LOGI(TAG,
                 "continuing active TTS playout on segmented stream: stream_id=%u previous_stream_id=%u "
                 "seq=%lu pending=%lu playing=%d",
                 (unsigned int)stream_id, (unsigned int)current_stream_id, (unsigned long)seq,
                 (unsigned long)pending_frames, s_tts_playing ? 1 : 0);
        return true;
    }
    should_replace = s_tts_session_stats_active || s_tts_end_pending || pending_frames > 0U || s_tts_playing ||
                     sfx_service_is_cloud_audio_busy();
    if (!should_replace) {
        ws_tts_mark_stream_sequence_locked(stream_id, seq);
        xSemaphoreGive(s_tts_queue_lock);
        return true;
    }
    xSemaphoreGive(s_tts_queue_lock);

    ESP_LOGW(TAG,
             "replacing active TTS stream on new FIRST: stream_id=%u previous_stream_id=%u seq=%lu "
             "pending=%lu enqueued=%lu played=%lu playing=%d",
             (unsigned int)stream_id, (unsigned int)current_stream_id, (unsigned long)seq,
             (unsigned long)pending_frames, (unsigned long)enqueued_frames, (unsigned long)played_frames,
             s_tts_playing ? 1 : 0);

    if (s_tts_playback_lock != NULL) {
        (void)xSemaphoreTake(s_tts_playback_lock, portMAX_DELAY);
    }

    ws_tts_log_session_stats("replaced_by_new_stream");
    was_playing = s_tts_playing || hal_audio_is_playback_mode();
    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        ws_tts_queue_reset_locked();
        s_tts_stream_replacements = 1U;
        ws_tts_mark_stream_sequence_locked(stream_id, seq);
        xSemaphoreGive(s_tts_queue_lock);
    }

    if (was_playing) {
#ifdef CONFIG_ENABLE_WAKE_WORD
        hal_audio_set_playback_mode(false);
#endif
        ws_idle_audio_after_media();
        s_tts_playing = false;
    }
    sfx_service_set_cloud_audio_busy(false);
    if (s_tts_playback_lock != NULL) {
        xSemaphoreGive(s_tts_playback_lock);
    }
    (void)ws_send_tts_buffer_status_for_stream("replaced_by_new_stream", true, current_stream_id);
    return true;
}

static bool ws_tts_accept_incoming_frame(uint8_t flags, uint16_t stream_id, uint32_t seq) {
    bool accepted = true;
    bool log_drop = false;
    bool log_gap = false;
    bool log_stale_stream = false;
    uint32_t expected_seq = 0;
    uint16_t current_stream_id = 0;

    if ((flags & WS_FRAME_FLAG_FIRST) != 0U) {
        return ws_tts_replace_existing_stream(stream_id, seq);
    }

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "dropping TTS audio frame because queue lock timed out: stream_id=%u seq=%lu flags=0x%02x",
                 (unsigned int)stream_id, (unsigned long)seq, flags);
        (void)ws_send_tts_buffer_status("queue_lock_timeout", true);
        return false;
    }

    if (!s_tts_stream_seq_active) {
        s_tts_dropped_frames++;
        log_drop = true;
        accepted = false;
    } else if (s_tts_current_stream_id != 0U && stream_id != s_tts_current_stream_id) {
        current_stream_id = s_tts_current_stream_id;
        s_tts_stale_stream_frames++;
        s_tts_dropped_frames++;
        log_stale_stream = true;
        accepted = false;
    } else if (seq == s_tts_expected_rx_seq) {
        ws_tts_mark_stream_sequence_locked(s_tts_current_stream_id, seq);
    } else if (seq < s_tts_expected_rx_seq) {
        expected_seq = s_tts_expected_rx_seq;
        s_tts_seq_duplicate_frames++;
        s_tts_dropped_frames++;
        log_drop = true;
        accepted = false;
    } else {
        expected_seq = s_tts_expected_rx_seq;
        s_tts_seq_gap_frames++;
        s_tts_last_gap_expected_seq = s_tts_expected_rx_seq;
        s_tts_last_gap_received_seq = seq;
        log_gap = true;
        ws_tts_mark_stream_sequence_locked(s_tts_current_stream_id, seq);
    }

    xSemaphoreGive(s_tts_queue_lock);

    if (log_stale_stream) {
        ESP_LOGW(TAG, "dropping stale TTS stream frame: stream_id=%u current_stream_id=%u seq=%lu flags=0x%02x",
                 (unsigned int)stream_id, (unsigned int)current_stream_id, (unsigned long)seq, flags);
        (void)ws_send_tts_buffer_status("stale_stream", true);
    } else if (log_drop) {
        ESP_LOGW(TAG, "dropping stale TTS audio frame: seq=%lu expected=%lu flags=0x%02x", (unsigned long)seq,
                 (unsigned long)expected_seq, flags);
        (void)ws_send_tts_buffer_status("stale_frame", true);
    } else if (log_gap) {
        ESP_LOGW(TAG, "TTS audio frame sequence gap: seq=%lu expected=%lu flags=0x%02x", (unsigned long)seq,
                 (unsigned long)expected_seq, flags);
        (void)ws_send_tts_buffer_status("sequence_gap", true);
    }

    return accepted;
}

static void ws_audio_update_stats_locked(void) {
    if (s_audio_state_lock == NULL) {
        memset(&s_last_audio_queue_stats, 0, sizeof(s_last_audio_queue_stats));
        return;
    }

    s_last_audio_queue_stats.valid = true;
    s_last_audio_queue_stats.queued_frames = s_audio_queued_frames;
    s_last_audio_queue_stats.sent_frames = s_audio_sent_frames;
    s_last_audio_queue_stats.dropped_frames = s_audio_dropped_frames;
    s_last_audio_queue_stats.pending_frames =
        s_audio_pending_slots != NULL ? (uint32_t)uxQueueMessagesWaiting(s_audio_pending_slots) : 0U;
    s_last_audio_queue_stats.inflight_frames = s_audio_inflight_count;
    s_last_audio_queue_stats.resident_frames =
        s_last_audio_queue_stats.pending_frames + s_last_audio_queue_stats.inflight_frames;
    s_last_audio_queue_stats.high_watermark = s_audio_high_watermark;
    s_last_audio_queue_stats.last_queue_delay_us = s_audio_last_queue_delay_us;
    s_last_audio_queue_stats.session_open = s_audio_session_open;
    s_last_audio_queue_stats.first_frame_pending = s_audio_first_frame_pending;
    s_last_audio_queue_stats.end_pending = s_audio_end_pending || s_audio_cancel_pending;
}

static void ws_audio_queue_reset_locked(void) {
    uint8_t slot_idx;

    if (s_audio_state_lock == NULL || s_audio_free_slots == NULL || s_audio_pending_slots == NULL) {
        return;
    }

    s_audio_session_generation++;
    if (s_audio_session_generation == 0U) {
        s_audio_session_generation = 1U;
    }

    xQueueReset(s_audio_free_slots);
    xQueueReset(s_audio_pending_slots);
    for (slot_idx = 0; slot_idx < WS_AUDIO_QUEUE_DEPTH; ++slot_idx) {
        bool inflight = false;

        for (uint8_t inflight_idx = 0; inflight_idx < s_audio_inflight_count; ++inflight_idx) {
            if (s_audio_inflight_slots[inflight_idx] == slot_idx) {
                inflight = true;
                break;
            }
        }
        if (!inflight) {
            (void)xQueueSendToBack(s_audio_free_slots, &slot_idx, 0);
        }
    }

    s_audio_session_open = false;
    s_audio_first_frame_pending = false;
    s_audio_end_pending = false;
    s_audio_cancel_pending = false;
    s_audio_upload_active = false;
    s_audio_last_queue_delay_us = 0;
    s_audio_sent_batches = 0U;
    s_audio_last_batch_elapsed_us = 0U;
    ws_audio_uplink_policy_reset(&s_audio_uplink_policy);
    ws_audio_update_stats_locked();
}

static esp_err_t ws_audio_queue_init(void) {
    bool need_reset = false;

    if (!ws_media_runtime_accepts_work()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_slot_lock == NULL) {
        s_audio_slot_lock = xSemaphoreCreateMutex();
        if (s_audio_slot_lock == NULL) {
            ESP_LOGE(TAG, "failed to create audio slot lock");
            ws_audio_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_audio_state_lock == NULL) {
        s_audio_state_lock = xSemaphoreCreateMutex();
        if (s_audio_state_lock == NULL) {
            ESP_LOGE(TAG, "failed to create audio state lock");
            ws_audio_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_audio_free_slots == NULL) {
        s_audio_free_slots = xQueueCreate(WS_AUDIO_QUEUE_DEPTH, sizeof(uint8_t));
        if (s_audio_free_slots == NULL) {
            ESP_LOGE(TAG, "failed to create audio free slot queue");
            ws_audio_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_audio_pending_slots == NULL) {
        s_audio_pending_slots = xQueueCreate(WS_AUDIO_QUEUE_DEPTH, sizeof(uint8_t));
        if (s_audio_pending_slots == NULL) {
            ESP_LOGE(TAG, "failed to create audio pending queue");
            ws_audio_runtime_deinit();
            return ESP_FAIL;
        }
        need_reset = true;
    }

    if (s_audio_worker_task == NULL) {
        s_audio_worker_running = true;
        BaseType_t task_ret;
        bool stack_in_psram = false;
#ifdef CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
        task_ret = xTaskCreateWithCaps(ws_audio_worker_task, "ws_audio_send", WS_AUDIO_WORKER_STACK, NULL,
                                       WS_AUDIO_WORKER_PRIO, &s_audio_worker_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        stack_in_psram = task_ret == pdPASS;
        if (task_ret != pdPASS) {
            ESP_LOGW(TAG, "failed to create audio worker in PSRAM, retrying internal RAM");
            task_ret = xTaskCreate(ws_audio_worker_task, "ws_audio_send", WS_AUDIO_WORKER_STACK, NULL,
                                   WS_AUDIO_WORKER_PRIO, &s_audio_worker_task);
        }
#else
        task_ret = xTaskCreate(ws_audio_worker_task, "ws_audio_send", WS_AUDIO_WORKER_STACK, NULL, WS_AUDIO_WORKER_PRIO,
                               &s_audio_worker_task);
#endif
        if (task_ret != pdPASS) {
            s_audio_worker_running = false;
            ESP_LOGE(TAG, "failed to create audio worker task");
            ws_audio_runtime_deinit();
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "audio worker created: stack=%u memory=%s", (unsigned)WS_AUDIO_WORKER_STACK,
                 stack_in_psram ? "psram" : "internal");
        need_reset = true;
    } else {
        if (!s_audio_worker_running) {
            s_audio_worker_running = true;
            need_reset = true;
        }
    }

    if (need_reset && xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
        ws_audio_queue_reset_locked();
        xSemaphoreGive(s_audio_state_lock);
    }

    return ESP_OK;
}

static int ws_send_audio_packet(const uint8_t *data, size_t len, uint8_t flags) {
    return ws_send_binary_packet(WS_FRAME_TYPE_AUDIO, flags, data, len);
}

static void ws_audio_worker_task(void *arg) {
    (void)arg;

    while (s_audio_worker_running) {
        uint8_t batch_slots[WS_AUDIO_UPLINK_MAX_BATCH_FRAMES] = {0};
        uint8_t batch_count = 0;
        bool ready = false;
        bool first_pending = false;
        ws_audio_uplink_codec_t batch_codec = WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
        uint64_t oldest_enqueued_us = 0U;
        uint32_t batch_generation = 0U;

        if (s_audio_slot_lock == NULL ||
            xSemaphoreTake(s_audio_slot_lock, pdMS_TO_TICKS(WS_AUDIO_WORKER_WAIT_MS)) != pdTRUE) {
            continue;
        }

        if (s_audio_state_lock != NULL && xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
            size_t pending_frames =
                s_audio_pending_slots != NULL ? (size_t)uxQueueMessagesWaiting(s_audio_pending_slots) : 0U;
            uint8_t oldest_slot = 0U;
            uint64_t oldest_age_us = 0U;

            if (pending_frames > 0U && xQueuePeek(s_audio_pending_slots, &oldest_slot, 0) == pdTRUE) {
                int64_t now_us = esp_timer_get_time();
                oldest_enqueued_us = s_audio_frame_pool[oldest_slot].enqueued_us;
                oldest_age_us =
                    (uint64_t)((now_us > (int64_t)oldest_enqueued_us) ? (now_us - (int64_t)oldest_enqueued_us) : 0);
            }

            bool end_pending = s_audio_end_pending || s_audio_cancel_pending;
            bool opus_pending =
                pending_frames > 0U && s_audio_frame_pool[oldest_slot].codec == WS_AUDIO_UPLINK_CODEC_OPUS;

            if (opus_pending ||
                ws_audio_uplink_should_flush(&s_audio_uplink_policy, pending_frames, end_pending, oldest_age_us)) {
                size_t target_frames = opus_pending
                                           ? 1U
                                           : ws_audio_uplink_batch_frames(&s_audio_uplink_policy, pending_frames,
                                                                          end_pending, oldest_age_us);

                ready = ws_audio_session_ready();
                first_pending = s_audio_first_frame_pending;
                batch_codec = opus_pending ? WS_AUDIO_UPLINK_CODEC_OPUS : WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
                batch_generation = s_audio_session_generation;
                while (batch_count < target_frames &&
                       xQueueReceive(s_audio_pending_slots, &batch_slots[batch_count], 0) == pdTRUE) {
                    s_audio_inflight_slots[batch_count] = batch_slots[batch_count];
                    batch_count++;
                }
                s_audio_inflight_count = batch_count;
            }
            xSemaphoreGive(s_audio_state_lock);
        }

        xSemaphoreGive(s_audio_slot_lock);

        if (batch_count > 0U) {
            size_t batch_len = 0U;
            uint8_t flags = WS_FRAME_FLAG_NONE;
            bool encode_failed = false;
            int send_ret = -1;
            int64_t now_us = 0;
            int64_t send_started_us = 0;
            uint64_t batch_elapsed_us = 0U;
            ws_client_media_send_stats_t batch_send_stats = {0};

            if (batch_codec == WS_AUDIO_UPLINK_CODEC_OPUS) {
                ws_audio_frame_slot_t *slot = &s_audio_frame_pool[batch_slots[0]];
                int encoded_len;

                if (batch_count != 1U || slot->codec != WS_AUDIO_UPLINK_CODEC_OPUS ||
                    slot->len != WS_AUDIO_FRAME_BYTES) {
                    ready = false;
                    encode_failed = true;
                } else {
                    encoded_len =
                        hal_opus_encode(slot->data, slot->len, s_audio_uplink_batch, sizeof(s_audio_uplink_batch));
                    if (encoded_len <= 0) {
                        ready = false;
                        encode_failed = true;
                    } else {
                        batch_len = (size_t)encoded_len;
                    }
                }
            } else {
                for (uint8_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
                    ws_audio_frame_slot_t *slot = &s_audio_frame_pool[batch_slots[batch_idx]];

                    if (slot->codec != batch_codec) {
                        ready = false;
                        break;
                    }
                    if (batch_len + slot->len > sizeof(s_audio_uplink_batch)) {
                        ready = false;
                        break;
                    }
                    memcpy(s_audio_uplink_batch + batch_len, slot->data, slot->len);
                    batch_len += slot->len;
                }
            }

            if (ready && batch_len > 0U) {
                if (first_pending) {
                    flags |= WS_FRAME_FLAG_FIRST;
                }

                send_started_us = esp_timer_get_time();
                send_ret = ws_send_audio_packet(s_audio_uplink_batch, batch_len, flags);
                now_us = esp_timer_get_time();
                batch_elapsed_us = (uint64_t)((now_us > send_started_us) ? (now_us - send_started_us) : 0);
                ws_client_get_media_send_stats(&batch_send_stats);
            } else {
                now_us = esp_timer_get_time();
            }

            if (s_audio_slot_lock != NULL && xSemaphoreTake(s_audio_slot_lock, portMAX_DELAY) == pdTRUE) {
                if (s_audio_state_lock != NULL && xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
                    bool generation_matches = batch_generation == s_audio_session_generation;

                    if (send_ret == 0) {
                        s_audio_sent_frames += batch_count;
                        s_audio_sent_batches++;
                        if (generation_matches) {
                            s_audio_first_frame_pending = false;
                        }
                    } else {
                        s_audio_dropped_frames += batch_count;
                        if (generation_matches && batch_codec == WS_AUDIO_UPLINK_CODEC_OPUS) {
                            uint8_t discarded_slot = 0U;
                            uint32_t discarded_pending = 0U;

                            while (s_audio_pending_slots != NULL &&
                                   xQueueReceive(s_audio_pending_slots, &discarded_slot, 0) == pdTRUE) {
                                if (s_audio_free_slots != NULL) {
                                    (void)xQueueSendToBack(s_audio_free_slots, &discarded_slot, 0);
                                }
                                discarded_pending++;
                            }
                            s_audio_dropped_frames += discarded_pending;
                            s_audio_end_pending = false;
                            s_audio_cancel_pending = true;
                            s_audio_upload_active = false;
                            ESP_LOGE(TAG, "Opus packet %s failed; cancelling turn and discarding %lu pending packets",
                                     encode_failed ? "encode" : "send", (unsigned long)discarded_pending);
                        }
                    }
                    s_audio_last_batch_elapsed_us = batch_elapsed_us;
                    ws_audio_uplink_observe_send(
                        &s_audio_uplink_policy, batch_count,
                        s_audio_pending_slots != NULL ? (size_t)uxQueueMessagesWaiting(s_audio_pending_slots) : 0U,
                        60000U, send_ret == 0 ? batch_elapsed_us : UINT64_MAX);
                    if (generation_matches) {
                        s_audio_last_queue_delay_us =
                            (uint32_t)((now_us > (int64_t)oldest_enqueued_us) ? (now_us - (int64_t)oldest_enqueued_us)
                                                                              : 0);
                    }
                    s_audio_inflight_count = 0U;
                    memset(s_audio_inflight_slots, 0, sizeof(s_audio_inflight_slots));
                    ws_audio_update_stats_locked();
                    xSemaphoreGive(s_audio_state_lock);
                }

                for (uint8_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
                    if (s_audio_free_slots != NULL) {
                        (void)xQueueSendToBack(s_audio_free_slots, &batch_slots[batch_idx], 0);
                    }
                }
                xSemaphoreGive(s_audio_slot_lock);
            }

            if (batch_send_stats.valid && (s_audio_sent_batches <= WS_AUDIO_INITIAL_SEND_LOG_BATCHES ||
                                           batch_send_stats.total_us >= WS_AUDIO_SLOW_SEND_WARN_US)) {
                ESP_LOGW(TAG,
                         "audio uplink batch=%lu frames=%u bytes=%u ret=%d lock_wait_us=%lu client_send_us=%lu "
                         "total_us=%lu measured_us=%llu codec=%s pressure=%d realtime=%d",
                         (unsigned long)s_audio_sent_batches, (unsigned int)batch_count, (unsigned int)batch_len,
                         send_ret, (unsigned long)batch_send_stats.lock_wait_us,
                         (unsigned long)batch_send_stats.send_us, (unsigned long)batch_send_stats.total_us,
                         (unsigned long long)batch_elapsed_us,
                         batch_codec == WS_AUDIO_UPLINK_CODEC_OPUS ? "opus" : "pcm_s16le",
                         s_audio_uplink_policy.pressure ? 1 : 0,
                         ws_audio_uplink_can_keep_up(batch_count, 60000U, batch_elapsed_us) ? 1 : 0);
            }

            if (send_ret == 0 && WS_AUDIO_STATS_LOG_INTERVAL_FRAMES > 0 &&
                s_audio_sent_frames % WS_AUDIO_STATS_LOG_INTERVAL_FRAMES == 0U) {
                ws_client_media_send_stats_t send_stats = {0};
                ws_client_get_media_send_stats(&send_stats);
                ESP_LOGI(TAG,
                         "audio_send sent=%lu queued=%lu drop=%lu pending=%u inflight=%u resident=%u high=%u "
                         "delay_us=%lu send_us=%lu/%lu packet=%u batch=%u realtime=%d",
                         (unsigned long)s_audio_sent_frames, (unsigned long)s_audio_queued_frames,
                         (unsigned long)s_audio_dropped_frames, (unsigned int)s_last_audio_queue_stats.pending_frames,
                         (unsigned int)s_last_audio_queue_stats.inflight_frames,
                         (unsigned int)s_last_audio_queue_stats.resident_frames,
                         (unsigned int)s_last_audio_queue_stats.high_watermark,
                         (unsigned long)s_audio_last_queue_delay_us, (unsigned long)send_stats.send_us,
                         (unsigned long)send_stats.total_us, (unsigned int)send_stats.packet_len,
                         (unsigned int)batch_count,
                         ws_audio_uplink_can_keep_up(batch_count, 60000U, s_audio_last_batch_elapsed_us) ? 1 : 0);
            }
            continue;
        }

        if (s_audio_state_lock != NULL && xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
            bool ready = ws_audio_session_ready();
            bool send_cancel = s_audio_cancel_pending && ready;
            bool send_end = !send_cancel && s_audio_end_pending && ready;
            bool first_pending = s_audio_first_frame_pending;
            bool session_open = s_audio_session_open;
            uint32_t end_generation = s_audio_session_generation;
            xSemaphoreGive(s_audio_state_lock);

            if (send_cancel || send_end) {
                uint8_t flags = WS_FRAME_FLAG_LAST;
                int send_ret;

                if (send_cancel) {
                    flags |= WS_FRAME_FLAG_CANCEL;
                } else if (first_pending || !session_open) {
                    flags |= WS_FRAME_FLAG_FIRST;
                }

                send_ret = ws_send_audio_packet(NULL, 0, flags);
                if (send_ret == 0 && s_audio_state_lock != NULL &&
                    xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
                    bool generation_matches = end_generation == s_audio_session_generation;

                    if (generation_matches) {
                        s_audio_session_open = false;
                        s_audio_first_frame_pending = false;
                        s_audio_end_pending = false;
                        s_audio_cancel_pending = false;
                        s_audio_upload_active = false;
                        s_audio_last_queue_delay_us = 0;
                        ws_audio_update_stats_locked();
                    }
                    xSemaphoreGive(s_audio_state_lock);
                    if (generation_matches && s_audio_uplink_codec == WS_AUDIO_UPLINK_CODEC_OPUS &&
                        hal_opus_reset() != 0) {
                        ESP_LOGE(TAG, "failed to reset Opus encoder after audio terminal marker");
                    }
                    if (generation_matches && !send_cancel) {
                        s_waiting_for_response = true;
                        s_timeout_display_count = 0;
                        s_response_wait_start_time = esp_timer_get_time();
                        ESP_LOGI(TAG, "audio end marker sent, waiting for server response (%dms)",
                                 WS_RESPONSE_TIMEOUT_MS);
                    } else if (generation_matches) {
                        ESP_LOGW(TAG, "audio upload cancel marker sent");
                    }
                }
            }

            if (!send_cancel && !send_end) {
                vTaskDelay(pdMS_TO_TICKS(WS_AUDIO_WORKER_WAIT_MS));
            }
        }
    }

    s_audio_worker_task = NULL;
    s_audio_worker_running = false;
    vTaskDelete(NULL);
}

static bool ws_wait_for_audio_worker_exit(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (s_audio_worker_task != NULL && waited_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }

    return s_audio_worker_task == NULL;
}

static bool ws_wait_for_tts_worker_exit(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (s_tts_worker_task != NULL && waited_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }

    return s_tts_worker_task == NULL;
}

static void ws_audio_runtime_deinit(void) {
    if (s_audio_worker_task != NULL) {
        s_audio_worker_running = false;
        if (!ws_wait_for_audio_worker_exit(WS_AUDIO_WORKER_EXIT_WAIT_MS)) {
            ESP_LOGW(TAG, "audio worker did not exit within %u ms; keeping audio runtime allocated",
                     (unsigned)WS_AUDIO_WORKER_EXIT_WAIT_MS);
            return;
        }
    }

    if (s_audio_state_lock != NULL && s_audio_free_slots != NULL && s_audio_pending_slots != NULL) {
        if (xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            ws_audio_queue_reset_locked();
            xSemaphoreGive(s_audio_state_lock);
        }
    }

    if (s_audio_free_slots != NULL) {
        vQueueDelete(s_audio_free_slots);
        s_audio_free_slots = NULL;
    }
    if (s_audio_pending_slots != NULL) {
        vQueueDelete(s_audio_pending_slots);
        s_audio_pending_slots = NULL;
    }
    if (s_audio_slot_lock != NULL) {
        vSemaphoreDelete(s_audio_slot_lock);
        s_audio_slot_lock = NULL;
    }
    if (s_audio_state_lock != NULL) {
        vSemaphoreDelete(s_audio_state_lock);
        s_audio_state_lock = NULL;
    }

    s_audio_worker_running = false;
    s_audio_inflight_count = 0;
    memset(s_audio_inflight_slots, 0, sizeof(s_audio_inflight_slots));
    s_audio_session_open = false;
    s_audio_first_frame_pending = false;
    s_audio_end_pending = false;
    s_audio_cancel_pending = false;
    s_audio_upload_active = false;
    s_audio_last_queue_delay_us = 0;
    memset(&s_last_audio_queue_stats, 0, sizeof(s_last_audio_queue_stats));
}

static void ws_tts_runtime_deinit(void) {
    ws_abort_tts_playback_internal(false, false);

    if (s_tts_worker_task != NULL) {
        s_tts_worker_running = false;
        if (!ws_wait_for_tts_worker_exit(WS_TTS_WORKER_EXIT_WAIT_MS)) {
            ESP_LOGW(TAG, "tts worker did not exit within %u ms; keeping tts runtime allocated",
                     (unsigned)WS_TTS_WORKER_EXIT_WAIT_MS);
            return;
        }
    }

    if (s_tts_queue_lock != NULL && s_tts_free_slots != NULL && s_tts_pending_slots != NULL) {
        if (xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            ws_tts_queue_reset_locked();
            xSemaphoreGive(s_tts_queue_lock);
        }
    }

    if (s_tts_free_slots != NULL) {
        vQueueDelete(s_tts_free_slots);
        s_tts_free_slots = NULL;
    }
    if (s_tts_pending_slots != NULL) {
        vQueueDelete(s_tts_pending_slots);
        s_tts_pending_slots = NULL;
    }
    if (s_tts_playback_lock != NULL) {
        vSemaphoreDelete(s_tts_playback_lock);
        s_tts_playback_lock = NULL;
    }
    if (s_tts_queue_lock != NULL) {
        vSemaphoreDelete(s_tts_queue_lock);
        s_tts_queue_lock = NULL;
    }

    s_tts_worker_running = false;
    s_tts_inflight_active = false;
    s_tts_inflight_slot = 0;
    s_tts_playing = false;
    ws_tts_rx_sha256_reset_locked();
}

static void ws_tts_worker_task(void *arg) {
    uint8_t slot_idx = 0;

    (void)arg;

    while (s_tts_worker_running) {
        bool have_slot = false;
        bool should_finish = false;
        bool playback_write_failed = false;
        bool log_starvation = false;
        bool log_last_pcm_written = false;
        bool report_buffering = false;
        const char *deferred_status_reason = NULL;
        uint32_t slot_generation = 0;
        uint32_t finish_generation = 0;
        uint16_t finish_stream_id = 0;
        uint16_t failed_stream_id = 0;
        uint32_t starvation_ms = 0;
        uint32_t starvation_enqueued_frames = 0;
        uint32_t starvation_played_frames = 0;
        uint32_t starvation_high_watermark = 0;
        uint32_t starvation_pending_bytes = 0;

        if (s_tts_queue_lock != NULL &&
            xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(WS_TTS_WORKER_WAIT_MS)) == pdTRUE) {
            if (s_tts_pending_slots != NULL) {
                uint32_t pending = (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots);

                if (pending > 0U) {
                    if (ws_tts_buffer_policy_ready(&s_tts_buffer_policy, s_tts_playing, s_tts_end_pending,
                                                   WS_TTS_START_BUFFER_BYTES, WS_TTS_REBUFFER_BYTES)) {
                        ws_tts_finish_starvation_locked();
                        have_slot = (xQueueReceive(s_tts_pending_slots, &slot_idx, 0) == pdTRUE);
                        if (have_slot) {
                            ws_tts_buffer_policy_dequeue(&s_tts_buffer_policy, s_tts_frame_pool[slot_idx].len);
                            ws_tts_buffer_policy_mark_resumed(&s_tts_buffer_policy);
                            s_tts_inflight_active = true;
                            s_tts_inflight_slot = slot_idx;
                            slot_generation = s_tts_queue_generation;
                        }
                    } else {
                        report_buffering = true;
                    }
                } else if (ws_tts_buffer_policy_should_finish(s_tts_end_pending, pending, s_tts_inflight_active)) {
                    ws_tts_finish_starvation_locked();
                    s_tts_end_pending = false;
                    should_finish = true;
                    finish_generation = s_tts_queue_generation;
                    finish_stream_id = s_tts_current_stream_id;
                } else if (s_tts_playing) {
                    ws_tts_buffer_policy_mark_starved(&s_tts_buffer_policy);
                    ws_tts_start_starvation_locked();
                    log_starvation =
                        ws_tts_should_warn_starvation_locked(&starvation_ms, &starvation_enqueued_frames,
                                                             &starvation_played_frames, &starvation_high_watermark);
                    if (log_starvation) {
                        starvation_pending_bytes = s_tts_buffer_policy.pending_bytes;
                    }
                }
            }

            switch (s_tts_deferred_status) {
            case WS_TTS_DEFERRED_STATUS_ENQUEUE_TIMEOUT:
                deferred_status_reason = "enqueue_timeout";
                break;
            case WS_TTS_DEFERRED_STATUS_ENQUEUE_FAILED:
                deferred_status_reason = "enqueue_failed";
                break;
            case WS_TTS_DEFERRED_STATUS_NONE:
            default:
                break;
            }
            s_tts_deferred_status = WS_TTS_DEFERRED_STATUS_NONE;
            xSemaphoreGive(s_tts_queue_lock);
        }

        if (deferred_status_reason != NULL) {
            (void)ws_send_tts_buffer_status(deferred_status_reason, true);
        }

        if (report_buffering) {
            (void)ws_maybe_send_tts_buffer_status("buffering");
        }

        if (log_starvation) {
            ESP_LOGW(TAG,
                     "tts playback queue starved: empty_ms=%lu enqueued_frames=%lu played_frames=%lu high=%lu "
                     "pending_bytes=%lu start_buffer=%u rebuffer=%u queue_depth=%u",
                     (unsigned long)starvation_ms, (unsigned long)starvation_enqueued_frames,
                     (unsigned long)starvation_played_frames, (unsigned long)starvation_high_watermark,
                     (unsigned long)starvation_pending_bytes, (unsigned int)WS_TTS_EFFECTIVE_START_BUFFER_FRAMES,
                     (unsigned int)WS_TTS_EFFECTIVE_REBUFFER_FRAMES, (unsigned int)WS_TTS_QUEUE_DEPTH);
            (void)ws_send_tts_buffer_status("starved", true);
        }

        if (have_slot) {
            ws_tts_frame_slot_t *slot = &s_tts_frame_pool[slot_idx];
            int written;
            bool stale_slot = false;
            bool playback_lock_taken = false;

            if (s_tts_playback_lock != NULL && xSemaphoreTake(s_tts_playback_lock, portMAX_DELAY) == pdTRUE) {
                playback_lock_taken = true;
            }

            if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) != pdTRUE) {
                stale_slot = true;
            } else {
                stale_slot = slot_generation != s_tts_queue_generation;
                xSemaphoreGive(s_tts_queue_lock);
            }
            if (stale_slot) {
                if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
                    if (s_tts_inflight_active && s_tts_inflight_slot == slot_idx) {
                        s_tts_inflight_active = false;
                    }
                    if (s_tts_free_slots != NULL) {
                        (void)xQueueSendToBack(s_tts_free_slots, &slot_idx, 0);
                    }
                    xSemaphoreGive(s_tts_queue_lock);
                }
                if (playback_lock_taken) {
                    xSemaphoreGive(s_tts_playback_lock);
                }
                continue;
            }

            if (!s_tts_playing || !hal_audio_is_running() || !hal_audio_is_playback_mode()) {
                if (!ws_prepare_tts_playback(s_tts_playing)) {
                    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
                        if (s_tts_inflight_active && s_tts_inflight_slot == slot_idx) {
                            s_tts_inflight_active = false;
                        }
                        if (s_tts_free_slots != NULL) {
                            (void)xQueueSendToBack(s_tts_free_slots, &slot_idx, 0);
                        }
                        xSemaphoreGive(s_tts_queue_lock);
                    }
                    if (playback_lock_taken) {
                        xSemaphoreGive(s_tts_playback_lock);
                    }
                    continue;
                }
            }

            written = hal_audio_write(slot->data, slot->len);
            if (written != (int)slot->len) {
                ESP_LOGW(TAG, "TTS playback incomplete: %d/%u", written, (unsigned int)slot->len);
                playback_write_failed = true;
            }

            if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
                stale_slot = slot_generation != s_tts_queue_generation;
                if (!stale_slot) {
                    s_tts_played_frames++;
                    failed_stream_id = s_tts_current_stream_id;
                    log_last_pcm_written = s_tts_end_pending && ws_tts_pending_frames_locked() == 0U;
                }
                if (s_tts_inflight_active && s_tts_inflight_slot == slot_idx) {
                    s_tts_inflight_active = false;
                }
                if (s_tts_free_slots != NULL) {
                    (void)xQueueSendToBack(s_tts_free_slots, &slot_idx, 0);
                }
                xSemaphoreGive(s_tts_queue_lock);
            }
            if (playback_lock_taken) {
                xSemaphoreGive(s_tts_playback_lock);
            }
            if (log_last_pcm_written) {
                ESP_LOGI(TAG, "TTS playout event=last_pcm_written stream_id=%u generation=%lu",
                         (unsigned int)failed_stream_id, (unsigned long)slot_generation);
            }
            if (playback_write_failed && !stale_slot) {
                ws_abort_tts_playback_internal(false, true);
                (void)ws_send_tts_buffer_status_for_stream("playback_write_failed", true, failed_stream_id);
                continue;
            }
            (void)ws_maybe_send_tts_buffer_status("playback");
            continue;
        }

        if (should_finish) {
            ws_finish_tts_playback(finish_generation, finish_stream_id);
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(WS_TTS_WORKER_WAIT_MS));
    }

    s_tts_worker_task = NULL;
    s_tts_worker_running = false;
    vTaskDelete(NULL);
}

static void ws_write_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint16_t ws_read_u16_le(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8));
}

static uint32_t ws_read_u32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void ws_reset_text_fragment_state(void) {
    free(s_text_fragment_state.buffer);
    memset(&s_text_fragment_state, 0, sizeof(s_text_fragment_state));
}

static void ws_reset_binary_fragment_state(void) {
    free(s_binary_fragment_state.payload_buffer);
    memset(&s_binary_fragment_state, 0, sizeof(s_binary_fragment_state));
}

static void ws_reset_rx_fragment_states(void) {
    ws_reset_text_fragment_state();
    ws_reset_binary_fragment_state();
}

static void ws_reset_media_state(void) {
    bool slot_lock_taken = false;
    bool send_lock_taken = false;

    memset(s_frame_sequences, 0, sizeof(s_frame_sequences));
    memset(&s_last_media_send_stats, 0, sizeof(s_last_media_send_stats));
    s_audio_upload_active = false;
    s_ota_binary_nacked = false;
    sfx_service_set_cloud_audio_busy(false);

    if (s_tts_playback_lock != NULL) {
        (void)xSemaphoreTake(s_tts_playback_lock, portMAX_DELAY);
    }
    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        ws_tts_queue_reset_locked();
        xSemaphoreGive(s_tts_queue_lock);
    }
    if (s_tts_playback_lock != NULL) {
        xSemaphoreGive(s_tts_playback_lock);
    }

    if (s_audio_state_lock != NULL && s_audio_free_slots != NULL && s_audio_pending_slots != NULL) {
        if (s_audio_slot_lock != NULL && xSemaphoreTake(s_audio_slot_lock, portMAX_DELAY) == pdTRUE) {
            slot_lock_taken = true;
        }
        if (s_ws_send_lock != NULL && xSemaphoreTake(s_ws_send_lock, portMAX_DELAY) == pdTRUE) {
            send_lock_taken = true;
        }
        if (xSemaphoreTake(s_audio_state_lock, portMAX_DELAY) == pdTRUE) {
            ws_audio_queue_reset_locked();
            xSemaphoreGive(s_audio_state_lock);
        }
        if (send_lock_taken) {
            xSemaphoreGive(s_ws_send_lock);
        }
        if (slot_lock_taken) {
            xSemaphoreGive(s_audio_slot_lock);
        }
    } else {
        memset(&s_last_audio_queue_stats, 0, sizeof(s_last_audio_queue_stats));
    }
}

static void ws_reset_session_state(void) {
    s_socket_connected = false;
    s_hello_acknowledged = false;
    s_hello_rejected = false;
    s_waiting_for_response = false;
    s_timeout_display_count = 0;
    s_response_wait_start_time = 0;
    s_last_session_activity_us = 0;
    ws_reset_rx_fragment_states();
    ws_reset_media_state();
}

/*
 * WebSocket events may be dispatched synchronously by a send call.  Keep the
 * error path strictly lock-free: the sender can still own s_ws_send_lock, so a
 * full media reset here would deadlock on the same non-recursive mutex.
 * Teardown performs the complete reset after the send stack has unwound.
 */
static void ws_mark_session_unavailable_from_callback(void) {
    s_socket_connected = false;
    s_hello_acknowledged = false;
    s_hello_rejected = false;
    s_waiting_for_response = false;
    s_timeout_display_count = 0;
    s_response_wait_start_time = 0;
    s_last_session_activity_us = 0;
    s_audio_upload_active = false;
}

static void ws_mark_session_activity(void) {
    s_last_session_activity_us = esp_timer_get_time();
}

static bool ws_media_runtime_accepts_work(void) {
    return s_allow_wake_word_resume;
}

static void ws_idle_audio_after_media(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (hal_audio_enter_app_idle() != 0) {
        ESP_LOGW(TAG, "Failed to idle audio path after media");
    }
#else
    hal_audio_stop();
#endif
}

static void ws_defer_wake_word_until_sleep(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (!ws_media_runtime_accepts_work()) {
        ESP_LOGI(TAG, "wake word sleep resume skipped while media runtime is closing");
        return;
    }
    ESP_LOGI(TAG, "Wake word resume deferred until Voice sleep standby");
#endif
}

static bool ws_prepare_tts_playback(bool recovering_existing_stream) {
    if (!ws_media_runtime_accepts_work()) {
        ESP_LOGI(TAG, "TTS playback skipped while media runtime is closing");
        sfx_service_set_cloud_audio_busy(false);
        return false;
    }

    if (recovering_existing_stream) {
        ESP_LOGW(TAG, "TTS audio path lost mid-stream, recovering playback (running=%d playback=%d)",
                 hal_audio_is_running(), hal_audio_is_playback_mode());
    } else {
        ESP_LOGI(TAG, "TTS started, preparing playback");
    }

    s_waiting_for_response = false;
    sfx_service_set_cloud_audio_busy(true);

#ifdef CONFIG_ENABLE_WAKE_WORD
    voice_recorder_pause_wake_word();
#endif

    hal_audio_set_playback_mode(true);
    hal_audio_set_sample_rate(24000);
    if (hal_audio_start() != 0) {
        ESP_LOGW(TAG, "Failed to start playback for TTS");
        ws_idle_audio_after_media();
        sfx_service_set_cloud_audio_busy(false);
        ws_defer_wake_word_until_sleep();
        return false;
    }

    if (!s_tts_playing) {
        s_tts_playing = true;
        behavior_state_set_with_resources("speaking", "", 0, NULL, "");
    }

    return true;
}

static void ws_abort_tts_playback_internal(bool send_status, bool resume_wake_word) {
    uint16_t aborted_stream_id = 0U;
    s_waiting_for_response = false;

    if (s_tts_playback_lock != NULL) {
        (void)xSemaphoreTake(s_tts_playback_lock, portMAX_DELAY);
    }

    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        aborted_stream_id = s_tts_current_stream_id;
        xSemaphoreGive(s_tts_queue_lock);
    }
    ws_tts_log_session_stats("aborted");

    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        ws_tts_queue_reset_locked();
        xSemaphoreGive(s_tts_queue_lock);
    }

    if (!s_tts_playing) {
        sfx_service_set_cloud_audio_busy(false);
        if (resume_wake_word) {
            ws_defer_wake_word_until_sleep();
        }
        if (s_tts_playback_lock != NULL) {
            xSemaphoreGive(s_tts_playback_lock);
        }
        if (send_status) {
            (void)ws_send_tts_buffer_status_for_stream("aborted", true, aborted_stream_id);
        }
        return;
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    hal_audio_set_playback_mode(false);
#endif
    ws_idle_audio_after_media();
    s_tts_playing = false;
    sfx_service_set_cloud_audio_busy(false);
    if (resume_wake_word) {
        ws_defer_wake_word_until_sleep();
    }
    if (s_tts_playback_lock != NULL) {
        xSemaphoreGive(s_tts_playback_lock);
    }
    if (send_status) {
        (void)ws_send_tts_buffer_status_for_stream("aborted", true, aborted_stream_id);
    }
}

static void ws_abort_tts_playback(void) {
    ws_abort_tts_playback_internal(true, true);
}

void ws_client_abort_tts_playback(void) {
    ws_abort_tts_playback();
}

bool ws_client_is_tts_playing(void) {
    return s_tts_playing || sfx_service_is_cloud_audio_busy();
}

static bool ws_tts_generation_matches(uint32_t generation, uint16_t stream_id) {
    bool matches = false;

    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        matches = generation == s_tts_queue_generation && stream_id != 0U && stream_id == s_tts_current_stream_id;
        xSemaphoreGive(s_tts_queue_lock);
    }
    return matches;
}

static void ws_finish_tts_playback(uint32_t generation, uint16_t stream_id) {
    int drain_result = 0;

    s_waiting_for_response = false;

    if (s_tts_playback_lock != NULL) {
        (void)xSemaphoreTake(s_tts_playback_lock, portMAX_DELAY);
    }
    if (!ws_tts_generation_matches(generation, stream_id)) {
        ESP_LOGI(TAG, "Ignoring stale TTS completion: stream_id=%u generation=%lu", (unsigned int)stream_id,
                 (unsigned long)generation);
        if (s_tts_playback_lock != NULL) {
            xSemaphoreGive(s_tts_playback_lock);
        }
        return;
    }

    ws_tts_log_session_stats("complete");
    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_tts_stream_seq_active = false;
        xSemaphoreGive(s_tts_queue_lock);
    }
    (void)ws_send_tts_buffer_status_for_stream("draining", true, stream_id);

    if (s_tts_playing) {
        ESP_LOGI(TAG, "TTS playout event=queue_drained stream_id=%u generation=%lu", (unsigned int)stream_id,
                 (unsigned long)generation);
        drain_result = hal_audio_drain_playback(WS_TTS_DMA_DRAIN_TIMEOUT_MS);
        ESP_LOGI(TAG, "TTS playout event=dma_drained stream_id=%u generation=%lu result=%d", (unsigned int)stream_id,
                 (unsigned long)generation, drain_result);
        if (!ws_tts_generation_matches(generation, stream_id)) {
            if (s_tts_playback_lock != NULL) {
                xSemaphoreGive(s_tts_playback_lock);
            }
            return;
        }
#ifdef CONFIG_ENABLE_WAKE_WORD
        hal_audio_set_playback_mode(false);
#endif
        ws_idle_audio_after_media();
        const char *completion_state = control_ingress_tts_completion_state();
        const bool foreground_lease_active = control_ingress_has_foreground_ai_lease();
        if (ws_event_ui_should_apply_tts_completion(completion_state, s_behavior_feedback_enabled,
                                                    foreground_lease_active)) {
            const bool silent_foreground_state = strcmp(completion_state, "processing") == 0 ||
                                                 strcmp(completion_state, "custom2") == 0 ||
                                                 strcmp(completion_state, "custom3") == 0;
            const char *completion_sound = silent_foreground_state ? "" : NULL;
            behavior_state_set_with_resources(completion_state, NULL, 0, NULL, completion_sound);
            ESP_LOGI(TAG, "TTS playout event=speaking_released stream_id=%u next_state=%s", (unsigned int)stream_id,
                     completion_state);
        } else {
            ESP_LOGI(TAG, "Skipping TTS completion presentation: state=%s feedback=%d foreground=%d", completion_state,
                     s_behavior_feedback_enabled, foreground_lease_active);
        }
        s_tts_playing = false;
    }

    sfx_service_set_cloud_audio_busy(false);
    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (generation == s_tts_queue_generation && stream_id == s_tts_current_stream_id) {
            s_tts_current_stream_id = 0U;
        }
        xSemaphoreGive(s_tts_queue_lock);
    }
    if (s_tts_playback_lock != NULL) {
        xSemaphoreGive(s_tts_playback_lock);
    }
    (void)ws_send_tts_buffer_status_for_stream("complete", true, stream_id);
    ws_defer_wake_word_until_sleep();
    mem_monitor_snapshot("after_tts_playback");
}

static int ws_client_lock_and_send_with_timeout(bool binary, const void *payload, int len, bool allow_before_session,
                                                uint32_t lock_timeout_ms, uint32_t send_timeout_ms,
                                                bool log_lock_timeout) {
    int sent = -1;
    int64_t start_us = 0;
    int64_t lock_acquired_us = 0;
    int64_t send_done_us = 0;
    TickType_t lock_timeout_ticks = pdMS_TO_TICKS(lock_timeout_ms);
    TickType_t send_timeout_ticks = pdMS_TO_TICKS(send_timeout_ms);

    if (s_ws_client == NULL || payload == NULL || len < 0 || !s_socket_connected) {
        ws_log_send_blocked(binary, len, allow_before_session);
        return -1;
    }

    if (!allow_before_session && !s_hello_acknowledged) {
        ws_log_send_blocked(binary, len, allow_before_session);
        return -1;
    }

    if (s_ws_send_lock == NULL) {
        s_ws_send_lock = xSemaphoreCreateMutex();
        if (s_ws_send_lock == NULL) {
            ESP_LOGE(TAG, "create ws send lock failed");
            return -1;
        }
    }

    if (send_timeout_ms > 0U && send_timeout_ticks == 0) {
        send_timeout_ticks = 1;
    }

    start_us = esp_timer_get_time();
    if (xSemaphoreTake(s_ws_send_lock, lock_timeout_ticks) != pdTRUE) {
        if (log_lock_timeout) {
            ESP_LOGW(TAG, "ws send lock timeout");
        }
        return -1;
    }
    lock_acquired_us = esp_timer_get_time();

    sent = binary ? esp_websocket_client_send_bin(s_ws_client, (const char *)payload, len, send_timeout_ticks)
                  : esp_websocket_client_send_text(s_ws_client, (const char *)payload, len, send_timeout_ticks);
    send_done_us = esp_timer_get_time();

    xSemaphoreGive(s_ws_send_lock);

    s_last_media_send_stats.valid = false;
    s_last_media_send_stats.binary = binary;
    s_last_media_send_stats.lock_wait_us = (uint32_t)(lock_acquired_us - start_us);
    s_last_media_send_stats.send_us = (uint32_t)(send_done_us - lock_acquired_us);
    s_last_media_send_stats.total_us = (uint32_t)(send_done_us - start_us);
    s_last_media_send_stats.timestamp_us = (uint64_t)send_done_us;

    return sent;
}

static int ws_client_lock_and_send(bool binary, const void *payload, int len, bool allow_before_session) {
    return ws_client_lock_and_send_with_timeout(binary, payload, len, allow_before_session, WS_SEND_LOCK_TIMEOUT_MS,
                                                WS_SEND_TIMEOUT_MS, true);
}

static int ws_send_text_internal(const char *text, bool allow_before_session) {
    if (text == NULL) {
        return -1;
    }

    return ws_client_lock_and_send(false, text, (int)strlen(text), allow_before_session);
}

static int ws_send_text_internal_with_timeout(const char *text, bool allow_before_session, uint32_t lock_timeout_ms,
                                              uint32_t send_timeout_ms, bool log_lock_timeout) {
    if (text == NULL) {
        return -1;
    }

    return ws_client_lock_and_send_with_timeout(false, text, (int)strlen(text), allow_before_session, lock_timeout_ms,
                                                send_timeout_ms, log_lock_timeout);
}

static int ws_send_json_root_with_timeout(cJSON *root, bool allow_before_session, uint32_t lock_timeout_ms,
                                          uint32_t send_timeout_ms, bool log_lock_timeout) {
    char *json = NULL;
    int sent = -1;

    if (root == NULL) {
        return -1;
    }

    json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        ESP_LOGE(TAG, "json encode failed");
        return -1;
    }

    sent = ws_send_text_internal_with_timeout(json, allow_before_session, lock_timeout_ms, send_timeout_ms,
                                              log_lock_timeout);
    cJSON_free(json);
    return sent;
}

static int ws_send_json_envelope_with_timeout(const char *type, int code, cJSON *data, bool allow_before_session,
                                              uint32_t lock_timeout_ms, uint32_t send_timeout_ms,
                                              bool log_lock_timeout) {
    cJSON *root = NULL;
    int sent = -1;

    if (type == NULL) {
        return -1;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddNumberToObject(root, "code", code);
    if (data != NULL) {
        cJSON_AddItemToObject(root, "data", data);
    } else {
        cJSON_AddNullToObject(root, "data");
    }

    sent =
        ws_send_json_root_with_timeout(root, allow_before_session, lock_timeout_ms, send_timeout_ms, log_lock_timeout);
    cJSON_Delete(root);
    return sent;
}

static int ws_send_json_envelope(const char *type, int code, cJSON *data, bool allow_before_session) {
    return ws_send_json_envelope_with_timeout(type, code, data, allow_before_session, WS_SEND_LOCK_TIMEOUT_MS,
                                              WS_SEND_TIMEOUT_MS, true);
}

static int ws_send_tts_buffer_status_for_stream(const char *reason, bool force, uint16_t reported_stream_id) {
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint32_t pending_frames = 0;
    uint32_t pending_bytes = 0;
    uint32_t free_frames = 0;
    uint32_t pending_ms = 0;
    uint32_t enqueued_frames = 0;
    uint32_t played_frames = 0;
    uint32_t dropped_frames = 0;
    uint32_t drop_timeout_frames = 0;
    uint32_t high_watermark = 0;
    uint32_t wait_events = 0;
    uint64_t wait_total_ms = 0;
    uint32_t wait_max_ms = 0;
    uint32_t starvation_events = 0;
    uint64_t starvation_total_ms = 0;
    uint32_t starvation_max_ms = 0;
    uint32_t rx_frames = 0;
    uint16_t current_stream_id = 0;
    uint32_t expected_rx_seq = 0;
    uint32_t last_rx_seq = 0;
    uint32_t seq_duplicate_frames = 0;
    uint32_t seq_gap_frames = 0;
    uint32_t last_gap_expected_seq = 0;
    uint32_t last_gap_received_seq = 0;
    uint32_t stale_stream_frames = 0;
    uint32_t stream_replacements = 0;
    char audio_sha256[65] = "";
    bool session_active = false;
    bool playing = false;
    bool stream_active = false;
    bool bypass_attempt_throttle = false;
    cJSON *data = NULL;
    int sent;

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return -1;
    }

    if (!force && s_tts_buffer_status_last_us != 0U &&
        now_us - s_tts_buffer_status_last_us < ((uint64_t)WS_TTS_BUFFER_STATUS_INTERVAL_MS * 1000ULL)) {
        xSemaphoreGive(s_tts_queue_lock);
        return 0;
    }

    bypass_attempt_throttle =
        force && reason != NULL &&
        (strcmp(reason, "complete") == 0 || strcmp(reason, "aborted") == 0 || strcmp(reason, "handoff") == 0 ||
         strcmp(reason, "draining") == 0 || strcmp(reason, "replaced_by_new_stream") == 0 ||
         strcmp(reason, "enqueue_timeout") == 0 || strcmp(reason, "enqueue_failed") == 0 ||
         strcmp(reason, "queue_lock_timeout") == 0 || strcmp(reason, "sequence_gap") == 0 ||
         strcmp(reason, "stale_stream") == 0 || strcmp(reason, "stale_frame") == 0 ||
         strcmp(reason, "playback_write_failed") == 0);

    if (!bypass_attempt_throttle && s_tts_buffer_status_attempt_last_us != 0U &&
        now_us - s_tts_buffer_status_attempt_last_us < ((uint64_t)WS_TTS_STATUS_ATTEMPT_MIN_INTERVAL_MS * 1000ULL)) {
        xSemaphoreGive(s_tts_queue_lock);
        return 0;
    }
    s_tts_buffer_status_attempt_last_us = now_us;

    if (s_tts_pending_slots != NULL) {
        pending_frames = (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots);
    }
    if (s_tts_free_slots != NULL) {
        free_frames = (uint32_t)uxQueueMessagesWaiting(s_tts_free_slots);
    }
    pending_ms = ws_tts_buffer_policy_pending_ms(&s_tts_buffer_policy, WS_TTS_BYTES_PER_SECOND);
    pending_bytes = s_tts_buffer_policy.pending_bytes;
    enqueued_frames = s_tts_enqueued_frames;
    played_frames = s_tts_played_frames;
    dropped_frames = s_tts_dropped_frames;
    drop_timeout_frames = s_tts_drop_timeout_frames;
    high_watermark = s_tts_high_watermark;
    wait_events = s_tts_enqueue_wait_events;
    wait_total_ms = s_tts_enqueue_wait_total_ms;
    wait_max_ms = s_tts_enqueue_wait_max_ms;
    starvation_events = s_tts_starvation_events;
    starvation_total_ms = s_tts_starvation_total_ms;
    starvation_max_ms = s_tts_starvation_max_ms;
    rx_frames = s_tts_rx_frames;
    stream_active = s_tts_stream_seq_active;
    current_stream_id = s_tts_current_stream_id;
    if (reported_stream_id != 0U) {
        current_stream_id = reported_stream_id;
    }
    expected_rx_seq = s_tts_expected_rx_seq;
    last_rx_seq = s_tts_last_rx_seq;
    seq_duplicate_frames = s_tts_seq_duplicate_frames;
    seq_gap_frames = s_tts_seq_gap_frames;
    last_gap_expected_seq = s_tts_last_gap_expected_seq;
    last_gap_received_seq = s_tts_last_gap_received_seq;
    stale_stream_frames = s_tts_stale_stream_frames;
    stream_replacements = s_tts_stream_replacements;
    snprintf(audio_sha256, sizeof(audio_sha256), "%s", s_tts_rx_audio_sha256);
    session_active = s_tts_session_stats_active;
    playing = s_tts_playing;
    xSemaphoreGive(s_tts_queue_lock);

    data = cJSON_CreateObject();
    if (data == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(data, "reason", (reason != NULL && reason[0] != '\0') ? reason : "periodic");
    cJSON_AddBoolToObject(data, "playing", playing);
    cJSON_AddBoolToObject(data, "session_active", session_active);
    cJSON_AddBoolToObject(data, "stream_active", stream_active);
    cJSON_AddNumberToObject(data, "pending_frames", (double)pending_frames);
    cJSON_AddNumberToObject(data, "free_frames", (double)free_frames);
    cJSON_AddNumberToObject(data, "queue_depth", (double)WS_TTS_QUEUE_DEPTH);
    cJSON_AddNumberToObject(data, "frame_bytes", (double)WS_TTS_FRAME_BYTES);
    cJSON_AddNumberToObject(data, "pending_ms", (double)pending_ms);
    cJSON_AddNumberToObject(data, "pending_bytes", (double)pending_bytes);
    cJSON_AddNumberToObject(data, "enqueued_frames", (double)enqueued_frames);
    cJSON_AddNumberToObject(data, "played_frames", (double)played_frames);
    cJSON_AddNumberToObject(data, "dropped_frames", (double)dropped_frames);
    cJSON_AddNumberToObject(data, "drop_timeout_frames", (double)drop_timeout_frames);
    cJSON_AddNumberToObject(data, "high_watermark", (double)high_watermark);
    cJSON_AddNumberToObject(data, "queue_starve_events", (double)starvation_events);
    cJSON_AddNumberToObject(data, "queue_starve_total_ms", (double)starvation_total_ms);
    cJSON_AddNumberToObject(data, "queue_starve_max_ms", (double)starvation_max_ms);
    cJSON_AddNumberToObject(data, "enqueue_wait_events", (double)wait_events);
    cJSON_AddNumberToObject(data, "enqueue_wait_total_ms", (double)wait_total_ms);
    cJSON_AddNumberToObject(data, "enqueue_wait_max_ms", (double)wait_max_ms);
    cJSON_AddNumberToObject(data, "rx_frames", (double)rx_frames);
    cJSON_AddNumberToObject(data, "stream_id", (double)current_stream_id);
    cJSON_AddNumberToObject(data, "expected_rx_seq", (double)expected_rx_seq);
    cJSON_AddNumberToObject(data, "last_rx_seq", (double)last_rx_seq);
    cJSON_AddNumberToObject(data, "seq_duplicate_frames", (double)seq_duplicate_frames);
    cJSON_AddNumberToObject(data, "seq_gap_frames", (double)seq_gap_frames);
    cJSON_AddNumberToObject(data, "last_gap_expected_seq", (double)last_gap_expected_seq);
    cJSON_AddNumberToObject(data, "last_gap_received_seq", (double)last_gap_received_seq);
    cJSON_AddNumberToObject(data, "stale_stream_frames", (double)stale_stream_frames);
    cJSON_AddNumberToObject(data, "stream_replacements", (double)stream_replacements);
    if (audio_sha256[0] != '\0') {
        cJSON_AddStringToObject(data, "audio_sha256", audio_sha256);
    }
    cJSON_AddNumberToObject(data, "start_buffer_frames", (double)WS_TTS_EFFECTIVE_START_BUFFER_FRAMES);
    cJSON_AddNumberToObject(data, "rebuffer_frames", (double)WS_TTS_EFFECTIVE_REBUFFER_FRAMES);

    sent = ws_send_json_envelope_with_timeout("evt.audio.buffer_status", 0, data, false, WS_TTS_STATUS_LOCK_TIMEOUT_MS,
                                              WS_TTS_STATUS_SEND_TIMEOUT_MS, false);
    if (sent >= 0 && s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_tts_buffer_status_last_us = now_us;
        xSemaphoreGive(s_tts_queue_lock);
    }
    return sent;
}

static int ws_maybe_send_tts_buffer_status(const char *reason) {
    return ws_send_tts_buffer_status(reason, false);
}

static uint32_t ws_next_frame_seq(ws_frame_type_t frame_type) {
    if (frame_type <= 0 || frame_type > WS_FRAME_TYPE_APP_PACKAGE) {
        return 0;
    }

    s_frame_sequences[frame_type] += 1U;
    if (s_frame_sequences[frame_type] == 0U) {
        s_frame_sequences[frame_type] = 1U;
    }

    return s_frame_sequences[frame_type];
}

static int ws_send_binary_packet(ws_frame_type_t frame_type, uint8_t flags, const uint8_t *payload, size_t len) {
    uint8_t *packet = NULL;
    size_t packet_len = WS_BINARY_LEGACY_HEADER_LEN + len;
    uint32_t seq;
    uint32_t lock_timeout_ms;
    uint32_t send_timeout_ms;
    int sent;
    bool ok;

    if (frame_type <= 0 || frame_type > WS_FRAME_TYPE_APP_PACKAGE) {
        return -1;
    }
    if (len > 0 && payload == NULL) {
        return -1;
    }

    seq = ws_next_frame_seq(frame_type);
    if (seq == 0U) {
        return -1;
    }

    packet = (uint8_t *)heap_caps_malloc(packet_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (packet == NULL) {
        packet = (uint8_t *)heap_caps_malloc(packet_len, MALLOC_CAP_8BIT);
    }
    if (packet == NULL) {
        ESP_LOGE(TAG, "binary packet alloc failed, len=%u", (unsigned int)packet_len);
        return -1;
    }

    memcpy(packet, WS_BINARY_MAGIC, 4);
    packet[4] = (uint8_t)frame_type;
    packet[5] = flags;
    /* Keep ESP32 -> server frames on the legacy 14-byte header so updated
       firmware remains compatible with older desktop/server builds. */
    ws_write_u32_le(packet + 6, seq);
    ws_write_u32_le(packet + 10, (uint32_t)len);
    if (len > 0) {
        memcpy(packet + WS_BINARY_LEGACY_HEADER_LEN, payload, len);
    }

    lock_timeout_ms = frame_type == WS_FRAME_TYPE_AUDIO ? WS_AUDIO_SEND_TIMEOUT_MS : WS_SEND_LOCK_TIMEOUT_MS;
    send_timeout_ms = frame_type == WS_FRAME_TYPE_AUDIO ? WS_AUDIO_SEND_TIMEOUT_MS : WS_SEND_TIMEOUT_MS;
    sent = ws_client_lock_and_send_with_timeout(true, packet, (int)packet_len, false, lock_timeout_ms, send_timeout_ms,
                                                true);
    free(packet);
    ok = (sent == (int)packet_len);

    s_last_media_send_stats.valid = ok;
    s_last_media_send_stats.frame_type = (uint8_t)frame_type;
    s_last_media_send_stats.payload_len = len;
    s_last_media_send_stats.packet_len = (uint32_t)packet_len;

    if (!ok) {
        ESP_LOGW(TAG, "binary packet send incomplete: %d/%u", sent, (unsigned int)packet_len);
        return -1;
    }

    return 0;
}

static void ws_get_mac_string(char *out, size_t out_size) {
    uint8_t mac[6] = {0};

    if (out == NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        return;
    }

    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void ws_get_device_id(char *out, size_t out_size) {
    uint8_t mac[6] = {0};

    if (out == NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        return;
    }

    snprintf(out, out_size, "watcher-%02X%02X", mac[4], mac[5]);
}

static int ws_send_client_hello(void) {
    cJSON *data = cJSON_CreateObject();
    cJSON *capabilities = NULL;
    cJSON *audio_uplink = NULL;
    cJSON *audio_codecs = NULL;
    cJSON *registry = NULL;
    cJSON *names = NULL;
    server_pairing_config_t pairing = {0};
    char mac[18];
    char device_id[32];

    if (data == NULL) {
        return -1;
    }

    ws_get_mac_string(mac, sizeof(mac));
    ws_get_device_id(device_id, sizeof(device_id));
    cJSON_AddStringToObject(data, "role", "hardware");
    cJSON_AddStringToObject(data, "fw_version", ota_service_get_fw_version());
    if (device_id[0] != '\0') {
        cJSON_AddStringToObject(data, "device_id", device_id);
    }
    if (mac[0] != '\0') {
        cJSON_AddStringToObject(data, "mac", mac);
    }
    if (s_session_pairing_code[0] != '\0') {
        cJSON_AddStringToObject(data, "pairing_code", s_session_pairing_code);
    }
    capabilities = cJSON_AddObjectToObject(data, "capabilities");
    audio_uplink = capabilities != NULL ? cJSON_AddObjectToObject(capabilities, "audio_uplink") : NULL;
    if (audio_uplink != NULL) {
        cJSON_AddNumberToObject(audio_uplink, "version", 1);
        audio_codecs = cJSON_AddArrayToObject(audio_uplink, "codecs");
    }
    if (audio_codecs != NULL) {
#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
        if (hal_opus_is_available()) {
            cJSON *opus = cJSON_CreateObject();
            if (opus != NULL) {
                cJSON_AddStringToObject(opus, "codec", "opus");
                cJSON_AddNumberToObject(opus, "sample_rate", 16000);
                cJSON_AddNumberToObject(opus, "channels", 1);
                cJSON_AddNumberToObject(opus, "frame_duration_ms", 60);
                cJSON_AddStringToObject(opus, "packetization", "one_opus_packet_per_wspk");
                cJSON_AddItemToArray(audio_codecs, opus);
            }
        }
#endif
        cJSON *pcm = cJSON_CreateObject();
        if (pcm != NULL) {
            cJSON_AddStringToObject(pcm, "codec", "pcm_s16le");
            cJSON_AddNumberToObject(pcm, "sample_rate", 16000);
            cJSON_AddNumberToObject(pcm, "channels", 1);
            cJSON_AddNumberToObject(pcm, "frame_duration_ms", 60);
            cJSON_AddStringToObject(pcm, "packetization", "pcm_s16le_stream");
            cJSON_AddItemToArray(audio_codecs, pcm);
        }
    }
    registry = capabilities != NULL ? cJSON_AddObjectToObject(capabilities, "animation_registry") : NULL;
    if (registry != NULL) {
        cJSON_AddNumberToObject(registry, "count", ANIMATION_REGISTRY_COUNT);
        cJSON_AddStringToObject(registry, "fingerprint", ANIMATION_REGISTRY_FINGERPRINT);
        cJSON_AddNumberToObject(registry, "manifest_version", 2);
        names = cJSON_AddArrayToObject(registry, "names");
        if (names != NULL) {
            for (int index = 0; index < ANIMATION_REGISTRY_COUNT; ++index) {
                cJSON *name_item = cJSON_CreateString(animation_registry_name((emoji_anim_type_t)index));
                if (name_item == NULL || !cJSON_AddItemToArray(names, name_item)) {
                    cJSON_Delete(name_item);
                    break;
                }
            }
        }
    }
    if (server_pairing_load(&pairing) == ESP_OK && pairing.configured) {
        cJSON_AddStringToObject(data, "server_id", pairing.server_id);
        cJSON_AddStringToObject(data, "pairing_id", pairing.pairing_id);
    }
    return ws_send_json_envelope("sys.client.hello", 0, data, true);
}

static bool ws_event_is_fragmented(const esp_websocket_event_data_t *data) {
    return (data != NULL) && (data->payload_offset != 0 || data->payload_len != data->data_len);
}

static void ws_handle_text_message(const char *msg) {
    ws_msg_type_t msg_type;

    if (msg == NULL || msg[0] == '\0') {
        return;
    }

    if (s_text_handler != NULL && s_text_handler(msg, strlen(msg), s_text_handler_context)) {
        ws_client_mark_server_response();
        return;
    }

    size_t msg_len = strlen(msg);
    if (msg_len > WS_TEXT_LOG_MAX_CHARS) {
        ESP_LOGI(TAG, "WS received len=%u head=\"%.*s\"", (unsigned int)msg_len, WS_TEXT_LOG_MAX_CHARS, msg);
    } else {
        ESP_LOGI(TAG, "WS received: %s", msg);
    }

    msg_type = ws_route_message(msg);
    switch (msg_type) {
    case WS_MSG_SYS_ACK:
        if (strstr(msg, "\"sys.client.hello\"") != NULL) {
            s_hello_rejected = false;
        }
        ws_client_mark_server_response();
        break;
    case WS_MSG_SYS_NACK:
        if (strstr(msg, "\"sys.client.hello\"") != NULL) {
            ESP_LOGW(TAG, "Hello handshake rejected by server");
            s_hello_rejected = true;
        }
        ws_client_mark_server_response();
        break;
    case WS_MSG_EVT_ASR_RESULT:
    case WS_MSG_EVT_AI_STATUS:
    case WS_MSG_EVT_AI_THINKING:
    case WS_MSG_EVT_AI_REPLY:
        ws_client_mark_server_response();
        break;
    default:
        break;
    }
}

static int ws_send_tts_buffer_status(const char *reason, bool force) {
    return ws_send_tts_buffer_status_for_stream(reason, force, 0U);
}

void ws_client_register_text_handler(ws_client_text_handler_t handler, void *context) {
    s_text_handler = handler;
    s_text_handler_context = handler != NULL ? context : NULL;
}

void ws_client_register_tts_frame_guard(ws_client_tts_frame_guard_t guard, void *context) {
    s_tts_frame_guard = guard;
    s_tts_frame_guard_context = guard != NULL ? context : NULL;
}

static bool ws_tts_frame_is_allowed(uint8_t flags, uint16_t stream_id, uint32_t seq, size_t payload_len) {
    if (s_tts_frame_guard == NULL) {
        return true;
    }
    return s_tts_frame_guard(flags, stream_id, seq, payload_len, s_tts_frame_guard_context);
}

static void ws_handle_text_frame(const esp_websocket_event_data_t *data) {
    char *msg = NULL;

    if (data == NULL || data->data_ptr == NULL || data->data_len <= 0) {
        return;
    }

    if (ws_event_is_fragmented(data)) {
        size_t total_len;
        size_t chunk_len;
        size_t chunk_end;

        total_len = (size_t)data->payload_len;
        chunk_len = (size_t)data->data_len;
        chunk_end = (size_t)data->payload_offset + chunk_len;

        if (data->payload_offset == 0) {
            ws_reset_text_fragment_state();
            if (total_len == 0U) {
                ESP_LOGW(TAG, "fragmented text frame has zero total length");
                return;
            }

            if (total_len > WS_TEXT_MAX_PAYLOAD_BYTES) {
                ESP_LOGW(TAG, "dropping oversized fragmented text frame: len=%u max=%u", (unsigned int)total_len,
                         (unsigned int)WS_TEXT_MAX_PAYLOAD_BYTES);
                ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "text_payload_too_large");
                s_text_fragment_state.active = true;
                s_text_fragment_state.dropping = true;
                s_text_fragment_state.total_len = total_len;
            } else {
                s_text_fragment_state.buffer = (char *)ws_alloc_rx_buffer(total_len + 1U);
                if (s_text_fragment_state.buffer == NULL) {
                    ESP_LOGE(TAG, "fragmented text frame alloc failed: %u", (unsigned int)total_len);
                    return;
                }

                s_text_fragment_state.active = true;
                s_text_fragment_state.total_len = total_len;
            }
        } else if (!s_text_fragment_state.active || s_text_fragment_state.total_len != total_len) {
            ESP_LOGW(TAG, "fragmented text frame state mismatch: offset=%d chunk=%d total=%d", data->payload_offset,
                     data->data_len, data->payload_len);
            ws_reset_text_fragment_state();
            return;
        }

        if ((size_t)data->payload_offset != s_text_fragment_state.received_len ||
            chunk_end > s_text_fragment_state.total_len) {
            ESP_LOGW(TAG, "fragmented text frame out of order: offset=%d chunk=%d total=%d received=%u",
                     data->payload_offset, data->data_len, data->payload_len,
                     (unsigned int)s_text_fragment_state.received_len);
            ws_reset_text_fragment_state();
            return;
        }

        if (s_text_fragment_state.dropping) {
            s_text_fragment_state.received_len = chunk_end;
            if (s_text_fragment_state.received_len >= s_text_fragment_state.total_len) {
                ws_reset_text_fragment_state();
            }
            return;
        }

        memcpy(s_text_fragment_state.buffer + data->payload_offset, data->data_ptr, chunk_len);
        s_text_fragment_state.received_len = chunk_end;
        if (s_text_fragment_state.received_len < s_text_fragment_state.total_len) {
            return;
        }

        s_text_fragment_state.buffer[s_text_fragment_state.total_len] = '\0';
        ws_handle_text_message(s_text_fragment_state.buffer);
        ws_reset_text_fragment_state();
        return;
    }

    if ((size_t)data->data_len > WS_TEXT_MAX_PAYLOAD_BYTES) {
        ESP_LOGW(TAG, "dropping oversized text frame: len=%d max=%u", data->data_len,
                 (unsigned int)WS_TEXT_MAX_PAYLOAD_BYTES);
        ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "text_payload_too_large");
        return;
    }

    msg = (char *)malloc((size_t)data->data_len + 1U);
    if (msg == NULL) {
        return;
    }

    memcpy(msg, data->data_ptr, (size_t)data->data_len);
    msg[data->data_len] = '\0';
    ws_handle_text_message(msg);

    free(msg);
}

static bool ws_parse_binary_header(const uint8_t *frame, size_t frame_len, uint8_t *frame_type, uint8_t *flags,
                                   uint16_t *stream_id, uint32_t *seq, size_t *payload_len, size_t *parsed_header_len) {
    size_t new_payload_len;
    size_t legacy_payload_len;

    if (frame == NULL || frame_type == NULL || flags == NULL || stream_id == NULL || seq == NULL ||
        payload_len == NULL || parsed_header_len == NULL) {
        return false;
    }

    if (frame_len < WS_BINARY_LEGACY_HEADER_LEN) {
        return false;
    }

    if (memcmp(frame, WS_BINARY_MAGIC, 4) != 0) {
        ESP_LOGW(TAG, "invalid binary magic");
        return false;
    }

    *frame_type = frame[4];
    *flags = frame[5];
    if (frame_len >= WS_BINARY_HEADER_LEN) {
        new_payload_len = (size_t)ws_read_u32_le(frame + 12);
        if (new_payload_len <= frame_len - WS_BINARY_HEADER_LEN &&
            new_payload_len + WS_BINARY_HEADER_LEN == frame_len) {
            *stream_id = ws_read_u16_le(frame + 6);
            *seq = ws_read_u32_le(frame + 8);
            *payload_len = new_payload_len;
            *parsed_header_len = WS_BINARY_HEADER_LEN;
            return true;
        }
    }

    legacy_payload_len = (size_t)ws_read_u32_le(frame + 10);
    if (legacy_payload_len > frame_len - WS_BINARY_LEGACY_HEADER_LEN ||
        legacy_payload_len + WS_BINARY_LEGACY_HEADER_LEN != frame_len) {
        ESP_LOGW(TAG, "invalid binary payload len=%u packet=%u", (unsigned int)legacy_payload_len,
                 (unsigned int)frame_len);
        return false;
    }
    *stream_id = 0U;
    *seq = ws_read_u32_le(frame + 6);
    *payload_len = legacy_payload_len;
    *parsed_header_len = WS_BINARY_LEGACY_HEADER_LEN;
    return true;
}

static bool ws_parse_binary_frame(const uint8_t *frame, size_t frame_len, uint8_t *frame_type, uint8_t *flags,
                                  uint16_t *stream_id, uint32_t *seq, const uint8_t **payload, size_t *payload_len) {
    size_t parsed_header_len = 0;

    if (payload == NULL) {
        return false;
    }

    if (!ws_parse_binary_header(frame, frame_len, frame_type, flags, stream_id, seq, payload_len, &parsed_header_len)) {
        return false;
    }

    *payload = frame + parsed_header_len;
    return true;
}

static void ws_dispatch_binary_frame(uint8_t frame_type, uint8_t flags, uint16_t stream_id, uint32_t seq,
                                     const uint8_t *payload, size_t payload_len) {
    switch (frame_type) {
    case WS_FRAME_TYPE_AUDIO:
        if (!ws_tts_frame_is_allowed(flags, stream_id, seq, payload_len)) {
            ESP_LOGW(TAG, "inbound TTS frame rejected by application guard: stream_id=%u seq=%lu",
                     (unsigned int)stream_id, (unsigned long)seq);
            ws_client_mark_server_response();
            break;
        }
        if (!ws_tts_accept_incoming_frame(flags, stream_id, seq)) {
            ws_client_mark_server_response();
            break;
        }
        if (payload_len > WS_TTS_MAX_PAYLOAD_BYTES) {
            ESP_LOGW(TAG, "dropping oversized tts frame: payload=%u max=%u flags=0x%02x", (unsigned int)payload_len,
                     (unsigned int)WS_TTS_MAX_PAYLOAD_BYTES, flags);
            ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "tts_payload_too_large");
            ws_client_mark_server_response();
            if ((flags & WS_FRAME_FLAG_LAST) != 0U) {
                ws_tts_complete();
            }
            break;
        }
        if (payload_len > 0U) {
            ws_handle_tts_binary(payload, (int)payload_len);
        }
        ws_client_mark_server_response();
        if ((flags & WS_FRAME_FLAG_LAST) != 0U) {
            ws_tts_complete();
        }
        break;
    case WS_FRAME_TYPE_OTA:
        ESP_LOGW(TAG, "OTA binary frame not supported");
        if (!s_ota_binary_nacked) {
            s_ota_binary_nacked = true;
            ws_send_ota_progress(0, "rejected", "not_supported");
            ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "ota_binary_not_supported");
            ws_send_sys_nack("xfer.ota.handshake", NULL, "not_supported");
        }
        break;
    case WS_FRAME_TYPE_APP_PACKAGE:
        ws_client_mark_server_response();
        if (s_app_package_handler.write_frame != NULL) {
            if (s_app_package_handler.write_frame(flags, payload, payload_len) != 0) {
                ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "app_package_write_failed");
            }
        } else {
            ESP_LOGW(TAG, "App package binary frame received without handler");
            ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "app_package_handler_missing");
            ws_send_sys_nack("app.package.transfer.begin", NULL, "handler_missing");
        }
        break;
    default:
        ESP_LOGW(TAG, "unexpected binary frame type=%u len=%u", frame_type, (unsigned int)payload_len);
        break;
    }
}

static void ws_handle_binary_frame(const esp_websocket_event_data_t *data) {
    const uint8_t *frame = NULL;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint16_t stream_id = 0;
    uint32_t seq = 0;
    uint8_t frame_type;
    uint8_t flags;

    if (data == NULL || data->data_ptr == NULL || data->data_len <= 0) {
        return;
    }

    if (ws_event_is_fragmented(data)) {
        const uint8_t *chunk = (const uint8_t *)data->data_ptr;
        size_t chunk_len = (size_t)data->data_len;
        size_t total_len = (size_t)data->payload_len;
        size_t chunk_end = (size_t)data->payload_offset + chunk_len;

        if (data->payload_offset == 0) {
            ws_reset_binary_fragment_state();
            s_binary_fragment_state.active = true;
            s_binary_fragment_state.total_len = total_len;
        } else if (!s_binary_fragment_state.active || s_binary_fragment_state.total_len != total_len) {
            ESP_LOGW(TAG, "fragmented binary frame state mismatch: offset=%d chunk=%d total=%d", data->payload_offset,
                     data->data_len, data->payload_len);
            ws_reset_binary_fragment_state();
            return;
        }

        if ((size_t)data->payload_offset != s_binary_fragment_state.received_total ||
            chunk_end > s_binary_fragment_state.total_len) {
            ESP_LOGW(TAG, "fragmented binary frame out of order: offset=%d chunk=%d total=%d received=%u",
                     data->payload_offset, data->data_len, data->payload_len,
                     (unsigned int)s_binary_fragment_state.received_total);
            ws_reset_binary_fragment_state();
            return;
        }

        s_binary_fragment_state.received_total = chunk_end;

        if (!s_binary_fragment_state.header_parsed) {
            size_t header_needed = WS_BINARY_HEADER_LEN - s_binary_fragment_state.header_len;
            size_t header_copy = chunk_len < header_needed ? chunk_len : header_needed;
            size_t parsed_header_len = 0;

            memcpy(s_binary_fragment_state.header + s_binary_fragment_state.header_len, chunk, header_copy);
            s_binary_fragment_state.header_len += header_copy;
            chunk += header_copy;
            chunk_len -= header_copy;

            if (s_binary_fragment_state.header_len >= WS_BINARY_LEGACY_HEADER_LEN) {
                size_t legacy_payload_len;

                if (memcmp(s_binary_fragment_state.header, WS_BINARY_MAGIC, 4) != 0) {
                    ESP_LOGW(TAG, "invalid fragmented binary magic");
                    ws_reset_binary_fragment_state();
                    return;
                }

                s_binary_fragment_state.frame_type = s_binary_fragment_state.header[4];
                s_binary_fragment_state.flags = s_binary_fragment_state.header[5];
                legacy_payload_len = (size_t)ws_read_u32_le(s_binary_fragment_state.header + 10);
                if (legacy_payload_len + WS_BINARY_LEGACY_HEADER_LEN == s_binary_fragment_state.total_len) {
                    s_binary_fragment_state.stream_id = 0U;
                    s_binary_fragment_state.seq = ws_read_u32_le(s_binary_fragment_state.header + 6);
                    s_binary_fragment_state.payload_len = legacy_payload_len;
                    parsed_header_len = WS_BINARY_LEGACY_HEADER_LEN;
                } else {
                    size_t new_payload_len;

                    if (s_binary_fragment_state.header_len < WS_BINARY_HEADER_LEN) {
                        return;
                    }
                    new_payload_len = (size_t)ws_read_u32_le(s_binary_fragment_state.header + 12);
                    if (new_payload_len + WS_BINARY_HEADER_LEN != s_binary_fragment_state.total_len) {
                        ESP_LOGW(TAG, "fragmented binary total mismatch: new_payload=%u legacy_payload=%u total=%u",
                                 (unsigned int)new_payload_len, (unsigned int)legacy_payload_len,
                                 (unsigned int)s_binary_fragment_state.total_len);
                        ws_reset_binary_fragment_state();
                        return;
                    }
                    s_binary_fragment_state.stream_id = ws_read_u16_le(s_binary_fragment_state.header + 6);
                    s_binary_fragment_state.seq = ws_read_u32_le(s_binary_fragment_state.header + 8);
                    s_binary_fragment_state.payload_len = new_payload_len;
                    parsed_header_len = WS_BINARY_HEADER_LEN;
                }
                s_binary_fragment_state.parsed_header_len = parsed_header_len;

                if (s_binary_fragment_state.payload_len + s_binary_fragment_state.parsed_header_len !=
                    s_binary_fragment_state.total_len) {
                    ESP_LOGW(TAG, "fragmented binary total mismatch: payload=%u total=%u",
                             (unsigned int)s_binary_fragment_state.payload_len,
                             (unsigned int)s_binary_fragment_state.total_len);
                    ws_reset_binary_fragment_state();
                    return;
                }

                if (s_binary_fragment_state.header_len > s_binary_fragment_state.parsed_header_len) {
                    size_t overflow_len =
                        s_binary_fragment_state.header_len - s_binary_fragment_state.parsed_header_len;
                    chunk -= overflow_len;
                    chunk_len += overflow_len;
                }

                s_binary_fragment_state.header_parsed = true;
                if (s_binary_fragment_state.frame_type == WS_FRAME_TYPE_AUDIO) {
                    if (s_binary_fragment_state.payload_len > WS_TTS_MAX_PAYLOAD_BYTES) {
                        ESP_LOGW(TAG, "dropping oversized fragmented tts frame: payload=%u max=%u flags=0x%02x",
                                 (unsigned int)s_binary_fragment_state.payload_len,
                                 (unsigned int)WS_TTS_MAX_PAYLOAD_BYTES, s_binary_fragment_state.flags);
                        ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "tts_payload_too_large");
                        ws_client_mark_server_response();
                        ws_reset_binary_fragment_state();
                        return;
                    }
                    s_binary_fragment_state.audio_frame_accepted =
                        ws_tts_frame_is_allowed(s_binary_fragment_state.flags, s_binary_fragment_state.stream_id,
                                                s_binary_fragment_state.seq, s_binary_fragment_state.payload_len) &&
                        ws_tts_accept_incoming_frame(s_binary_fragment_state.flags, s_binary_fragment_state.stream_id,
                                                     s_binary_fragment_state.seq);
                    if (!s_binary_fragment_state.audio_frame_accepted) {
                        ws_client_mark_server_response();
                    } else if (s_binary_fragment_state.payload_len > 0U) {
                        /*
                         * The ESP WebSocket receive buffer is smaller than one 4096-byte WSPK audio frame.
                         * Reassemble that protocol frame in PSRAM before queueing it; otherwise each 1 KB
                         * transport fragment consumes a 4 KB playback slot and corrupts buffer accounting.
                         */
                        s_binary_fragment_state.payload_buffer =
                            (uint8_t *)ws_alloc_rx_buffer(s_binary_fragment_state.payload_len);
                        if (s_binary_fragment_state.payload_buffer == NULL) {
                            ESP_LOGE(TAG, "fragmented TTS reassembly alloc failed: len=%u",
                                     (unsigned int)s_binary_fragment_state.payload_len);
                            ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "tts_reassembly_alloc_failed");
                            ws_reset_binary_fragment_state();
                            return;
                        }
                    }
                    if (ws_should_log_tts_rx_frame(s_binary_fragment_state.flags)) {
                        uint32_t pending = 0U;
                        if (s_tts_pending_slots != NULL) {
                            pending = (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots);
                        }
                        ESP_LOGI(TAG,
                                 "streaming fragmented audio frame: payload=%u chunks=%u flags=0x%02x rx=%lu "
                                 "suppressed=%lu pending=%u high=%lu dropped=%lu",
                                 (unsigned int)s_binary_fragment_state.payload_len,
                                 (unsigned int)((s_binary_fragment_state.payload_len + WS_TTS_FRAME_BYTES - 1U) /
                                                WS_TTS_FRAME_BYTES),
                                 s_binary_fragment_state.flags, (unsigned long)s_tts_rx_frames,
                                 (unsigned long)s_tts_rx_suppressed_logs, (unsigned int)pending,
                                 (unsigned long)s_tts_high_watermark, (unsigned long)s_tts_dropped_frames);
                    }
                    ws_client_mark_server_response();
                } else if (s_binary_fragment_state.payload_len > 0U) {
                    s_binary_fragment_state.payload_buffer =
                        (uint8_t *)ws_alloc_rx_buffer(s_binary_fragment_state.payload_len);
                    if (s_binary_fragment_state.payload_buffer == NULL) {
                        ESP_LOGE(TAG, "fragmented binary payload alloc failed: type=%u len=%u",
                                 s_binary_fragment_state.frame_type, (unsigned int)s_binary_fragment_state.payload_len);
                        ws_reset_binary_fragment_state();
                        return;
                    }
                }
            }
        }

        if (!s_binary_fragment_state.header_parsed) {
            return;
        }

        if (chunk_len > 0U) {
            if (s_binary_fragment_state.payload_received + chunk_len > s_binary_fragment_state.payload_len) {
                ESP_LOGW(TAG, "fragmented binary payload overflow: type=%u received=%u chunk=%u payload=%u",
                         s_binary_fragment_state.frame_type, (unsigned int)s_binary_fragment_state.payload_received,
                         (unsigned int)chunk_len, (unsigned int)s_binary_fragment_state.payload_len);
                ws_reset_binary_fragment_state();
                return;
            }

            if (s_binary_fragment_state.payload_buffer != NULL) {
                memcpy(s_binary_fragment_state.payload_buffer + s_binary_fragment_state.payload_received, chunk,
                       chunk_len);
            }

            s_binary_fragment_state.payload_received += chunk_len;
        }

        if (s_binary_fragment_state.received_total < s_binary_fragment_state.total_len) {
            return;
        }

        if (s_binary_fragment_state.payload_received != s_binary_fragment_state.payload_len) {
            ESP_LOGW(TAG, "fragmented binary payload incomplete: type=%u received=%u payload=%u",
                     s_binary_fragment_state.frame_type, (unsigned int)s_binary_fragment_state.payload_received,
                     (unsigned int)s_binary_fragment_state.payload_len);
            ws_reset_binary_fragment_state();
            return;
        }

        if (s_binary_fragment_state.frame_type == WS_FRAME_TYPE_AUDIO) {
            if (s_binary_fragment_state.audio_frame_accepted) {
                if (s_binary_fragment_state.payload_received > 0U) {
                    ws_handle_tts_binary(s_binary_fragment_state.payload_buffer,
                                         (int)s_binary_fragment_state.payload_received);
                }
                if ((s_binary_fragment_state.flags & WS_FRAME_FLAG_LAST) != 0U) {
                    ws_tts_complete();
                }
            }
        } else {
            ws_dispatch_binary_frame(s_binary_fragment_state.frame_type, s_binary_fragment_state.flags,
                                     s_binary_fragment_state.stream_id, s_binary_fragment_state.seq,
                                     s_binary_fragment_state.payload_buffer, s_binary_fragment_state.payload_received);
        }

        ws_reset_binary_fragment_state();
        return;
    }

    frame = (const uint8_t *)data->data_ptr;
    if (!ws_parse_binary_frame(frame, (size_t)data->data_len, &frame_type, &flags, &stream_id, &seq, &payload,
                               &payload_len)) {
        return;
    }

    ws_dispatch_binary_frame(frame_type, flags, stream_id, seq, payload, payload_len);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)handler_args;
    (void)base;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        s_ws_deferred_cleanup_pending = false;
        s_ws_started = true;
        s_socket_connected = true;
        s_hello_acknowledged = false;
        s_hello_rejected = false;
        s_audio_uplink_codec = WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
        s_waiting_for_response = false;
        s_timeout_display_count = 0;
        s_response_wait_start_time = 0;
        ws_mark_session_activity();
        ws_reset_media_state();
        if (ws_send_client_hello() < 0) {
            ESP_LOGE(TAG, "failed to send sys.client.hello");
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        s_ws_started = false;
        s_audio_uplink_codec = WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
        hal_opus_deinit();
#endif
        ws_mark_session_unavailable_from_callback();
        s_ws_deferred_cleanup_pending = true;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data == NULL) {
            break;
        }
        if (!ws_media_runtime_accepts_work()) {
            ws_reset_rx_fragment_states();
            break;
        }
        ws_mark_session_activity();
        if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            ws_handle_text_frame(data);
        } else if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
            ws_handle_binary_frame(data);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_NONE) {
                ESP_LOGE(TAG, "WebSocket error: type=NONE (structured transport details unavailable)");
            } else {
                ESP_LOGE(TAG, "WebSocket error: type=%d esp_err=%s tls_code=%d tls_flags=%d sock_errno=%d",
                         data->error_handle.error_type, esp_err_to_name(data->error_handle.esp_tls_last_esp_err),
                         data->error_handle.esp_tls_stack_err, data->error_handle.esp_tls_cert_verify_flags,
                         data->error_handle.esp_transport_sock_errno);
            }
        } else {
            ESP_LOGE(TAG, "WebSocket error");
        }
        ws_mark_session_unavailable_from_callback();
        s_ws_deferred_cleanup_pending = true;
        break;

    default:
        break;
    }
}

static void ws_client_ensure_default_router(void) {
    ws_router_t router;

    if (s_default_router_ready) {
        return;
    }
    ws_handlers_init();
    router = ws_handlers_get_router();
    ws_router_init(&router);
    s_default_router_ready = true;
    ESP_LOGI(TAG, "Default WebSocket protocol router initialized");
}

int ws_client_init(void) {
    esp_websocket_client_config_t cfg = {
        .uri = s_ws_server_url,
        .disable_auto_reconnect = true,
        .network_timeout_ms = WS_NETWORK_TIMEOUT_MS,
        .task_prio = WS_TASK_PRIO,
        .buffer_size = WS_BUFFER_SIZE,
        .task_stack = WS_TASK_STACK,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 2,
        .keep_alive_count = 2,
    };

    ws_client_ensure_default_router();

    if (s_ws_client != NULL) {
        return 0;
    }

    s_ws_client = esp_websocket_client_init(&cfg);
    if (s_ws_client == NULL) {
        ESP_LOGE(TAG, "failed to init WebSocket client");
        return -1;
    }

    if (s_ws_send_lock == NULL) {
        s_ws_send_lock = xSemaphoreCreateMutex();
        if (s_ws_send_lock == NULL) {
            ESP_LOGE(TAG, "failed to create WebSocket send lock");
            ws_client_deinit();
            return -1;
        }
    }

    ws_reset_session_state();
    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    ESP_LOGI(TAG, "WebSocket client initialized (URL: %s)", s_ws_server_url);
    return 0;
}

int ws_client_set_server_url(const char *url) {
    if (url == NULL || strlen(url) >= WS_URL_MAX_LEN) {
        ESP_LOGE(TAG, "invalid URL or URL too long");
        return -1;
    }
    if (s_ws_client != NULL) {
        ESP_LOGW(TAG, "cannot set URL after client initialized");
        return -1;
    }

    strncpy(s_ws_server_url, url, WS_URL_MAX_LEN - 1);
    s_ws_server_url[WS_URL_MAX_LEN - 1] = '\0';
    ESP_LOGI(TAG, "server URL set to: %s", s_ws_server_url);
    return 0;
}

void ws_client_set_session_pairing_code(const char *pairing_code) {
    size_t index;

    s_session_pairing_code[0] = '\0';
    if (pairing_code == NULL || strlen(pairing_code) != WS_SESSION_PAIRING_CODE_LENGTH) {
        return;
    }
    for (index = 0U; index < WS_SESSION_PAIRING_CODE_LENGTH; ++index) {
        if (!isdigit((unsigned char)pairing_code[index])) {
            return;
        }
    }
    memcpy(s_session_pairing_code, pairing_code, WS_SESSION_PAIRING_CODE_LENGTH);
    s_session_pairing_code[WS_SESSION_PAIRING_CODE_LENGTH] = '\0';
}

const char *ws_client_get_server_url(void) {
    return s_ws_server_url;
}

int ws_client_start(void) {
    esp_err_t ret;

    if (s_ws_client == NULL) {
        ESP_LOGE(TAG, "WebSocket not initialized");
        return -1;
    }

    if (s_ws_started) {
        ESP_LOGI(TAG, "WebSocket client already started");
        return 0;
    }

    ESP_LOGI(TAG, "Starting WebSocket client (URL: %s)", s_ws_server_url);
    s_allow_wake_word_resume = true;
    ret = esp_websocket_client_start(s_ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket: %s", esp_err_to_name(ret));
        return -1;
    }

    s_ws_started = true;
    ESP_LOGI(TAG, "WebSocket start requested");
    return 0;
}

static void ws_client_stop_internal(bool resume_wake_word) {
    bool worker_stopped = true;

    s_allow_wake_word_resume = resume_wake_word;

    if (s_ws_client == NULL) {
        ws_reset_session_state();
        s_ws_started = false;
        s_ws_deferred_cleanup_pending = false;
        return;
    }

    (void)ws_camera_runtime_stop();

    ws_abort_tts_playback_internal(false, resume_wake_word);

    s_audio_worker_running = false;

    if (s_ws_started) {
        esp_err_t err = esp_websocket_client_stop(s_ws_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WebSocket stop returned: %s", esp_err_to_name(err));
        }
    }

    if (s_audio_worker_task != NULL && !ws_wait_for_audio_worker_exit(WS_AUDIO_WORKER_EXIT_WAIT_MS)) {
        worker_stopped = false;
        ESP_LOGW(TAG, "audio worker did not exit within %u ms", (unsigned)WS_AUDIO_WORKER_EXIT_WAIT_MS);
    }

    if (worker_stopped && s_audio_state_lock != NULL && s_audio_free_slots != NULL && s_audio_pending_slots != NULL) {
        if (xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            ws_audio_queue_reset_locked();
            xSemaphoreGive(s_audio_state_lock);
        }
    }

    s_ws_started = false;
    if (worker_stopped) {
        ws_reset_session_state();
        s_ws_deferred_cleanup_pending = false;
    } else {
        s_socket_connected = false;
        s_hello_acknowledged = false;
        s_waiting_for_response = false;
        s_timeout_display_count = 0;
        s_response_wait_start_time = 0;
        ws_reset_rx_fragment_states();
    }
}

void ws_client_stop(void) {
    ws_client_stop_internal(true);
}

void ws_client_stop_for_resource_release(void) {
    ws_client_stop_internal(false);
}

esp_err_t ws_client_deinit(void) {
    ws_client_stop_internal(false);
    esp_err_t camera_ret = ws_camera_runtime_deinit();
    ws_audio_runtime_deinit();
    ws_tts_runtime_deinit();

    if (camera_ret != ESP_OK || s_audio_worker_task != NULL || s_tts_worker_task != NULL) {
        s_ws_started = false;
        ESP_LOGW(TAG, "media runtime still active; postponing WebSocket handle destroy (camera=%s)",
                 esp_err_to_name(camera_ret));
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ws_client != NULL) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    if (s_ws_send_lock != NULL) {
        vSemaphoreDelete(s_ws_send_lock);
        s_ws_send_lock = NULL;
    }

    s_ws_started = false;
    ws_reset_session_state();
    return ESP_OK;
}

int ws_client_send_binary(const uint8_t *data, int len) {
    if (data == NULL || len < 0) {
        return -1;
    }

    return ws_client_lock_and_send(true, data, len, false);
}

int ws_client_send_text(const char *text) {
    return ws_send_text_internal(text, false);
}

int ws_client_is_connected(void) {
    return s_socket_connected ? 1 : 0;
}

int ws_client_is_started(void) {
    return s_ws_started ? 1 : 0;
}

void ws_client_process_deferred_cleanup(void) {
    if (!s_ws_deferred_cleanup_pending) {
        return;
    }

    if (s_socket_connected) {
        s_ws_deferred_cleanup_pending = false;
        return;
    }

    /* A transport failure can synchronously dispatch ERROR and DISCONNECTED
     * while esp_websocket_client_send_bin() still owns this mutex. Never wait
     * here: the coordinator will retry on its next tick after the send stack
     * has unwound. */
    if (s_ws_send_lock != NULL) {
        if (xSemaphoreTake(s_ws_send_lock, 0) != pdTRUE) {
            return;
        }
        xSemaphoreGive(s_ws_send_lock);
    }

    s_ws_deferred_cleanup_pending = false;
    (void)ws_camera_runtime_stop();
    ws_abort_tts_playback_internal(false, s_allow_wake_word_resume);
    ws_reset_session_state();
    control_ingress_clear_active_ai_tasks();
    behavior_state_set_with_text("standby", "Disconnected", 0);
    ESP_LOGI(TAG, "Applied deferred WebSocket disconnect cleanup");
}

int ws_client_is_session_ready(void) {
    return (s_socket_connected && s_hello_acknowledged) ? 1 : 0;
}

bool ws_client_has_hello_rejected(void) {
    return s_hello_rejected;
}

bool ws_client_is_session_stale(uint32_t stale_ms) {
    int64_t age_us;

    if (!s_socket_connected || !s_hello_acknowledged) {
        return false;
    }
    if (s_last_session_activity_us <= 0) {
        return true;
    }

    age_us = esp_timer_get_time() - s_last_session_activity_us;
    return age_us >= ((int64_t)stale_ms * 1000LL);
}

void ws_client_invalidate_session(const char *reason) {
    ESP_LOGW(TAG, "Invalidating WebSocket session: %s", reason != NULL ? reason : "no reason");
    ws_reset_session_state();
}

void ws_client_set_behavior_feedback_enabled(bool enabled) {
    s_behavior_feedback_enabled = enabled;
}

void ws_client_register_app_package_handler(const ws_app_package_handler_t *handler) {
    if (handler == NULL) {
        memset(&s_app_package_handler, 0, sizeof(s_app_package_handler));
        ws_handlers_set_app_package_handler(NULL);
        return;
    }
    s_app_package_handler = *handler;
    ws_handlers_set_app_package_handler(handler);
}

void ws_client_mark_server_response(void) {
    ws_mark_session_activity();
    s_waiting_for_response = false;
}

void ws_client_mark_hello_acked(void) {
    if (!s_socket_connected || s_hello_acknowledged) {
        return;
    }

    s_hello_acknowledged = true;
    s_hello_rejected = false;
    ws_mark_session_activity();
    if (!s_behavior_feedback_enabled) {
        ESP_LOGI(TAG, "Skipping hello-ack behavior feedback; app owns connection UI");
    } else if (ws_client_has_hello_ui_headroom()) {
        behavior_state_set_with_resources("happy", NULL, 0, NULL, "");
    } else {
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGW(TAG, "Skipping hello-ack happy state due to low internal heap: free=%u largest=%u",
                 (unsigned)free_internal, (unsigned)largest_internal);
    }
    if (ws_send_device_firmware() < 0) {
        ESP_LOGW(TAG, "failed to send evt.device.firmware");
    }
}

int ws_send_sys_pong(void) {
    cJSON *data = cJSON_CreateObject();

    return ws_send_json_envelope("sys.pong", 0, data, true);
}

int ws_send_sys_ack(const char *message_type, const char *command_id) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || message_type == NULL) {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(data, "type", message_type);
    if (command_id != NULL && command_id[0] != '\0') {
        cJSON_AddStringToObject(data, "command_id", command_id);
    }

    return ws_send_json_envelope("sys.ack", 0, data, true);
}

int ws_send_sys_nack(const char *message_type, const char *command_id, const char *reason) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || message_type == NULL || reason == NULL) {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(data, "type", message_type);
    if (command_id != NULL && command_id[0] != '\0') {
        cJSON_AddStringToObject(data, "command_id", command_id);
    }
    cJSON_AddStringToObject(data, "reason", reason);

    return ws_send_json_envelope("sys.nack", 1, data, true);
}

int ws_send_device_firmware(void) {
    cJSON *data = cJSON_CreateObject();
    char mac[18];

    if (data == NULL) {
        return -1;
    }

    ws_get_mac_string(mac, sizeof(mac));
    cJSON_AddStringToObject(data, "fw_version", ota_service_get_fw_version());
    cJSON_AddStringToObject(data, "board_model", "WatcheRobot-S3");
    if (mac[0] != '\0') {
        cJSON_AddStringToObject(data, "mac", mac);
    }

    return ws_send_json_envelope("evt.device.firmware", 0, data, false);
}

int ws_send_device_error(int code, const char *message) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || message == NULL || message[0] == '\0') {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(data, "message", message);
    return ws_send_json_envelope("evt.device.error", code > 0 ? code : WS_DEVICE_ERROR_CODE_GENERIC, data, false);
}

int ws_send_ota_progress(int progress, const char *state, const char *message) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL) {
        return -1;
    }

    cJSON_AddNumberToObject(data, "progress", progress);
    if (state != NULL && state[0] != '\0') {
        cJSON_AddStringToObject(data, "state", state);
    }
    if (message != NULL && message[0] != '\0') {
        cJSON_AddStringToObject(data, "message", message);
    }

    return ws_send_json_envelope("evt.ota.progress", 0, data, false);
}

int ws_send_ota_handshake(const char *transfer_id, const char *status) {
    cJSON *data = cJSON_CreateObject();
    char mac[18];

    if (data == NULL) {
        return -1;
    }

    ws_get_mac_string(mac, sizeof(mac));
    if (transfer_id != NULL && transfer_id[0] != '\0') {
        cJSON_AddStringToObject(data, "transfer_id", transfer_id);
    }
    cJSON_AddStringToObject(data, "fw_version", ota_service_get_fw_version());
    cJSON_AddStringToObject(data, "board_model", "WatcheRobot-S3");
    if (status != NULL && status[0] != '\0') {
        cJSON_AddStringToObject(data, "status", status);
    }
    if (mac[0] != '\0') {
        cJSON_AddStringToObject(data, "mac", mac);
    }

    return ws_send_json_envelope("xfer.ota.handshake", 0, data, false);
}

int ws_send_app_package_status(const char *command_id, const char *app_id, const char *name, const char *version,
                               const char *state, const char *message) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || app_id == NULL || app_id[0] == '\0' || state == NULL || state[0] == '\0') {
        cJSON_Delete(data);
        return -1;
    }

    if (command_id != NULL && command_id[0] != '\0') {
        cJSON_AddStringToObject(data, "command_id", command_id);
    }
    cJSON_AddStringToObject(data, "app_id", app_id);
    if (name != NULL && name[0] != '\0') {
        cJSON_AddStringToObject(data, "name", name);
    }
    if (version != NULL && version[0] != '\0') {
        cJSON_AddStringToObject(data, "version", version);
    }
    cJSON_AddStringToObject(data, "state", state);
    if (message != NULL && message[0] != '\0') {
        cJSON_AddStringToObject(data, "message", message);
    }

    return ws_send_json_envelope("evt.app.package.status", 0, data, false);
}

int ws_send_app_package_list_json(const char *command_id, const char *apps_json) {
    cJSON *data = cJSON_CreateObject();
    cJSON *apps = NULL;

    if (data == NULL) {
        return -1;
    }
    if (command_id != NULL && command_id[0] != '\0') {
        cJSON_AddStringToObject(data, "command_id", command_id);
    }
    if (apps_json != NULL && apps_json[0] != '\0') {
        apps = cJSON_Parse(apps_json);
    }
    if (apps == NULL || !cJSON_IsArray(apps)) {
        cJSON_Delete(apps);
        apps = cJSON_CreateArray();
    }
    cJSON_AddItemToObject(data, "apps", apps);
    return ws_send_json_envelope("evt.app.package.list", 0, data, false);
}

int ws_send_servo_position(float x_deg, float y_deg) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL) {
        return -1;
    }

    cJSON_AddNumberToObject(data, "x_deg", x_deg);
    cJSON_AddNumberToObject(data, "y_deg", y_deg);
    return ws_send_json_envelope("evt.servo.position", 0, data, false);
}

int ws_send_servo_feedback(const char *axis, uint16_t raw, float angle) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || axis == NULL || axis[0] == '\0') {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(data, "axis", axis);
    cJSON_AddNumberToObject(data, "raw", raw);
    cJSON_AddNumberToObject(data, "angle", angle);
    return ws_send_json_envelope("evt.servo.feedback", 0, data, false);
}

int ws_send_camera_state(const char *action, const char *state, int fps, const char *message) {
    cJSON *data = cJSON_CreateObject();

    if (data == NULL || action == NULL || state == NULL) {
        cJSON_Delete(data);
        return -1;
    }

    cJSON_AddStringToObject(data, "action", action);
    cJSON_AddStringToObject(data, "state", state);
    if (fps > 0) {
        cJSON_AddNumberToObject(data, "fps", fps);
    }
    if (message != NULL && message[0] != '\0') {
        cJSON_AddStringToObject(data, "message", message);
    }

    return ws_send_json_envelope("evt.camera.state", 0, data, false);
}

int ws_send_video_frame(const uint8_t *jpeg, size_t len, bool first_frame) {
    uint8_t flags = WS_FRAME_FLAG_KEYFRAME;

    if (jpeg == NULL || len == 0U) {
        return -1;
    }
    if (first_frame) {
        flags |= WS_FRAME_FLAG_FIRST;
    }

    return ws_send_binary_packet(WS_FRAME_TYPE_VIDEO, flags, jpeg, len);
}

int ws_send_video_end(void) {
    return ws_send_binary_packet(WS_FRAME_TYPE_VIDEO, WS_FRAME_FLAG_LAST, NULL, 0);
}

int ws_send_image_frame(const uint8_t *jpeg, size_t len) {
    if (jpeg == NULL || len == 0U) {
        return -1;
    }

    return ws_send_binary_packet(WS_FRAME_TYPE_IMAGE, WS_FRAME_FLAG_FIRST | WS_FRAME_FLAG_LAST | WS_FRAME_FLAG_KEYFRAME,
                                 jpeg, len);
}

void ws_client_get_media_send_stats(ws_client_media_send_stats_t *stats) {
    if (stats != NULL) {
        *stats = s_last_media_send_stats;
    }
}

int ws_send_audio(const uint8_t *data, int len) {
    if (data == NULL || len <= 0) {
        return -1;
    }

    if (ws_audio_queue_init() != ESP_OK) {
        return -1;
    }

    if (s_audio_state_lock == NULL || s_audio_free_slots == NULL || s_audio_pending_slots == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }

    if (!ws_audio_session_ready() || s_audio_end_pending || s_audio_cancel_pending) {
        xSemaphoreGive(s_audio_state_lock);
        ws_log_send_blocked(true, len, false);
        return -1;
    }

    {
        uint8_t slot_idx = 0;
        ws_audio_frame_slot_t *slot = NULL;
        bool reclaimed_oldest = false;
        uint32_t pending_before = 0U;
        uint64_t evicted_age_us = 0U;
        int transport_len = len;

        if (uxQueueMessagesWaiting(s_audio_free_slots) == 0U && s_audio_uplink_codec == WS_AUDIO_UPLINK_CODEC_OPUS) {
            uint8_t discarded_slot = 0U;
            uint32_t discarded_pending = 0U;

            while (xQueueReceive(s_audio_pending_slots, &discarded_slot, 0) == pdTRUE) {
                (void)xQueueSendToBack(s_audio_free_slots, &discarded_slot, 0);
                discarded_pending++;
            }
            s_audio_dropped_frames += discarded_pending + 1U;
            s_audio_end_pending = false;
            s_audio_cancel_pending = true;
            s_audio_upload_active = false;
            ws_audio_update_stats_locked();
            xSemaphoreGive(s_audio_state_lock);
            ESP_LOGE(TAG, "Opus queue exhausted; cancelling turn and discarding %lu pending packets",
                     (unsigned long)discarded_pending);
            return -1;
        }

        if (uxQueueMessagesWaiting(s_audio_free_slots) == 0U) {
            if (uxQueueMessagesWaiting(s_audio_pending_slots) > 0U &&
                xQueueReceive(s_audio_pending_slots, &slot_idx, 0) == pdTRUE) {
                reclaimed_oldest = true;
                pending_before = (uint32_t)uxQueueMessagesWaiting(s_audio_pending_slots) + 1U;
                if (s_audio_frame_pool[slot_idx].enqueued_us > 0U) {
                    int64_t now_us = esp_timer_get_time();
                    evicted_age_us = (uint64_t)((now_us > (int64_t)s_audio_frame_pool[slot_idx].enqueued_us)
                                                    ? (now_us - (int64_t)s_audio_frame_pool[slot_idx].enqueued_us)
                                                    : 0);
                }
                s_audio_dropped_frames++;
            } else {
                s_audio_dropped_frames++;
                ws_audio_update_stats_locked();
                xSemaphoreGive(s_audio_state_lock);
                if (s_audio_dropped_frames % 10U == 1U) {
                    ESP_LOGW(TAG, "audio enqueue dropped: queue full and no reclaimable slot (pending=%u dropped=%lu)",
                             (unsigned int)uxQueueMessagesWaiting(s_audio_pending_slots),
                             (unsigned long)s_audio_dropped_frames);
                }
                return -1;
            }
        } else if (xQueueReceive(s_audio_free_slots, &slot_idx, 0) != pdTRUE) {
            s_audio_dropped_frames++;
            ws_audio_update_stats_locked();
            xSemaphoreGive(s_audio_state_lock);
            return -1;
        }

        slot = &s_audio_frame_pool[slot_idx];
        slot->codec = s_audio_uplink_codec;
        if (slot->codec == WS_AUDIO_UPLINK_CODEC_OPUS) {
            if (len != WS_AUDIO_FRAME_BYTES) {
                ESP_LOGE(TAG, "Opus requires exactly one 60ms PCM frame: got=%d expected=%d", len,
                         WS_AUDIO_FRAME_BYTES);
                (void)xQueueSendToBack(s_audio_free_slots, &slot_idx, 0);
                xSemaphoreGive(s_audio_state_lock);
                return -1;
            }
        }
        if (transport_len > WS_AUDIO_FRAME_BYTES) {
            transport_len = WS_AUDIO_FRAME_BYTES;
        }
        memcpy(slot->data, data, (size_t)transport_len);
        slot->len = (uint16_t)transport_len;
        slot->enqueued_us = (uint64_t)esp_timer_get_time();

        if (!s_audio_session_open) {
            s_audio_session_open = true;
            s_audio_first_frame_pending = true;
            s_audio_upload_active = true;
        }

        if (xQueueSendToBack(s_audio_pending_slots, &slot_idx, 0) != pdTRUE) {
            (void)xQueueSendToBack(s_audio_free_slots, &slot_idx, 0);
            s_audio_dropped_frames++;
            ws_audio_update_stats_locked();
            xSemaphoreGive(s_audio_state_lock);
            return -1;
        }

        if (reclaimed_oldest && s_audio_dropped_frames % 10U == 1U) {
            ESP_LOGW(TAG, "audio enqueue pressure: evicted oldest frame (pending_before=%u age_us=%llu dropped=%lu)",
                     (unsigned int)pending_before, (unsigned long long)evicted_age_us,
                     (unsigned long)s_audio_dropped_frames);
        }

        s_audio_queued_frames++;
        {
            uint32_t pending = (uint32_t)uxQueueMessagesWaiting(s_audio_pending_slots);
            uint32_t resident = pending + s_audio_inflight_count;
            if (resident > s_audio_high_watermark) {
                s_audio_high_watermark = resident;
            }
        }
        ws_audio_update_stats_locked();
    }

    xSemaphoreGive(s_audio_state_lock);
    return 0;
}

int ws_send_audio_end(void) {
    if (ws_audio_queue_init() != ESP_OK) {
        return -1;
    }

    if (s_audio_state_lock == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }

    if (!ws_audio_session_ready()) {
        xSemaphoreGive(s_audio_state_lock);
        ws_log_send_blocked(true, 0, false);
        return -1;
    }

    s_audio_end_pending = true;
    s_audio_upload_active = false;
    ws_audio_update_stats_locked();
    xSemaphoreGive(s_audio_state_lock);

    if (s_audio_worker_task != NULL) {
        taskYIELD();
    }

    return 0;
}

bool ws_client_apply_audio_uplink_negotiation(const char *codec, int sample_rate, int channels, int frame_duration_ms,
                                              const char *packetization, int version) {
    bool opus_available = false;
#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
    opus_available = codec != NULL && strcmp(codec, "opus") == 0 && hal_opus_is_available();
#endif
    ws_audio_codec_negotiation_result_t result = ws_audio_codec_negotiate(
        codec, sample_rate, channels, frame_duration_ms, packetization, version, opus_available);

    s_audio_uplink_codec = WS_AUDIO_UPLINK_CODEC_PCM_S16LE;
#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
    if (result.explicit_selection && result.codec == WS_AUDIO_CODEC_SELECTION_OPUS && hal_opus_reset() == 0) {
        s_audio_uplink_codec = WS_AUDIO_UPLINK_CODEC_OPUS;
        ESP_LOGI(TAG, "audio uplink negotiated: codec=opus rate=16000 channels=1 frame_ms=60");
        return true;
    }
#endif
    if (result.explicit_selection && result.codec == WS_AUDIO_CODEC_SELECTION_PCM_S16LE) {
#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
        hal_opus_deinit();
#endif
        ESP_LOGI(TAG, "audio uplink negotiated: codec=pcm_s16le rate=16000 channels=1 frame_ms=60");
        return true;
    }

#ifdef CONFIG_WATCHER_WS_AUDIO_UPLINK_OPUS
    hal_opus_deinit();
#endif
    ESP_LOGW(TAG, "audio uplink negotiation absent or invalid; using PCM fallback");
    return false;
}

ws_audio_uplink_codec_t ws_client_get_audio_uplink_codec(void) {
    return s_audio_uplink_codec;
}

int ws_cancel_audio_upload(void) {
    uint8_t slot_idx = 0U;
    uint32_t discarded = 0U;

    if (ws_audio_queue_init() != ESP_OK || s_audio_slot_lock == NULL || s_audio_state_lock == NULL) {
        return -1;
    }
    if (xSemaphoreTake(s_audio_slot_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }
    if (xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        xSemaphoreGive(s_audio_slot_lock);
        return -1;
    }
    if (!ws_audio_session_ready()) {
        xSemaphoreGive(s_audio_state_lock);
        xSemaphoreGive(s_audio_slot_lock);
        return -1;
    }

    while (xQueueReceive(s_audio_pending_slots, &slot_idx, 0) == pdTRUE) {
        (void)xQueueSendToBack(s_audio_free_slots, &slot_idx, 0);
        discarded++;
    }
    s_audio_session_generation++;
    if (s_audio_session_generation == 0U) {
        s_audio_session_generation = 1U;
    }
    s_audio_dropped_frames += discarded;
    s_audio_end_pending = false;
    s_audio_cancel_pending = true;
    s_audio_upload_active = false;
    ws_audio_update_stats_locked();
    xSemaphoreGive(s_audio_state_lock);
    xSemaphoreGive(s_audio_slot_lock);

    ESP_LOGW(TAG, "audio upload cancellation queued: discarded_pending=%lu", (unsigned long)discarded);
    if (s_audio_worker_task != NULL) {
        taskYIELD();
    }
    return 0;
}

void ws_client_get_audio_queue_stats(ws_client_audio_queue_stats_t *stats) {
    if (stats == NULL) {
        return;
    }

    if (s_audio_state_lock != NULL && xSemaphoreTake(s_audio_state_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        ws_audio_update_stats_locked();
        *stats = s_last_audio_queue_stats;
        xSemaphoreGive(s_audio_state_lock);
        return;
    }

    *stats = s_last_audio_queue_stats;
}

void ws_handle_tts_binary(const uint8_t *data, int len) {
    int offset = 0;
    uint32_t remaining_wait_ms = WS_TTS_ENQUEUE_TIMEOUT_MS;

    if (data == NULL || len <= 0) {
        return;
    }

    if (len > WS_TTS_MAX_PAYLOAD_BYTES) {
        ESP_LOGW(TAG, "dropping oversized tts payload: len=%d max=%u", len, (unsigned int)WS_TTS_MAX_PAYLOAD_BYTES);
        ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "tts_payload_too_large");
        return;
    }

    if (ws_tts_runtime_init() != ESP_OK) {
        ESP_LOGW(TAG, "dropping tts payload: playback runtime unavailable len=%d", len);
        ws_send_device_error(WS_DEVICE_ERROR_CODE_GENERIC, "tts_runtime_unavailable");
        return;
    }

    if (len > WS_TTS_FRAME_BYTES) {
        ESP_LOGI(TAG, "tts payload split: len=%d chunks=%u slot=%u pending=%u high=%lu dropped=%lu start_buffer=%u",
                 len, (unsigned int)((len + WS_TTS_FRAME_BYTES - 1) / WS_TTS_FRAME_BYTES),
                 (unsigned int)WS_TTS_FRAME_BYTES,
                 (unsigned int)(s_tts_pending_slots != NULL ? uxQueueMessagesWaiting(s_tts_pending_slots) : 0U),
                 (unsigned long)s_tts_high_watermark, (unsigned long)s_tts_dropped_frames,
                 (unsigned int)WS_TTS_EFFECTIVE_START_BUFFER_FRAMES);
    }

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (!s_tts_session_stats_active) {
        s_tts_session_stats_active = true;
        s_tts_inbound_bytes = 0;
        s_tts_enqueue_wait_total_ms = 0;
        s_tts_enqueued_frames = 0;
        s_tts_played_frames = 0;
        s_tts_dropped_frames = 0;
        s_tts_drop_timeout_frames = 0;
        s_tts_high_watermark = 0;
        s_tts_enqueue_wait_events = 0;
        s_tts_enqueue_wait_max_ms = 0;
        s_tts_starvation_active = false;
        s_tts_starvation_warned = false;
        s_tts_starvation_started_us = 0;
        s_tts_starvation_total_ms = 0;
        s_tts_starvation_events = 0;
        s_tts_starvation_max_ms = 0;
        s_tts_buffer_status_last_us = 0;
        s_tts_buffer_status_attempt_last_us = 0;
        (void)ws_tts_rx_sha256_start_locked();
    }
    s_tts_inbound_bytes += (uint64_t)len;
    ws_tts_rx_sha256_update_locked(data, (size_t)len);
    xSemaphoreGive(s_tts_queue_lock);

    while (offset < len) {
        uint8_t slot_idx = 0;
        int chunk_len = len - offset;
        uint32_t waited_ms = 0;

        if (chunk_len > WS_TTS_FRAME_BYTES) {
            chunk_len = WS_TTS_FRAME_BYTES;
        }

        if (!ws_tts_take_free_slot(&slot_idx, &waited_ms, remaining_wait_ms)) {
            uint32_t pending = 0;
            uint32_t high = 0;
            uint32_t dropped = 0;
            uint32_t drop_timeout = 0;
            uint64_t inbound_bytes = 0;
            uint32_t played_frames = 0;

            if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                ws_tts_record_enqueue_wait_locked(waited_ms);
                s_tts_dropped_frames++;
                s_tts_drop_timeout_frames++;
                if (s_tts_pending_slots != NULL) {
                    pending = (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots);
                }
                high = s_tts_high_watermark;
                dropped = s_tts_dropped_frames;
                drop_timeout = s_tts_drop_timeout_frames;
                inbound_bytes = s_tts_inbound_bytes;
                played_frames = s_tts_played_frames;
                s_tts_deferred_status = WS_TTS_DEFERRED_STATUS_ENQUEUE_TIMEOUT;
                xSemaphoreGive(s_tts_queue_lock);
            }

            ESP_LOGE(
                TAG,
                "tts enqueue timeout: waited_ms=%lu budget_ms=%lu pending=%lu high=%lu dropped=%lu drop_timeout=%lu "
                "inbound_bytes=%llu played_frames=%lu",
                (unsigned long)waited_ms, (unsigned long)remaining_wait_ms, (unsigned long)pending, (unsigned long)high,
                (unsigned long)dropped, (unsigned long)drop_timeout, (unsigned long long)inbound_bytes,
                (unsigned long)played_frames);
            return;
        }

        ws_tts_record_enqueue_wait_locked(waited_ms);
        if (waited_ms >= remaining_wait_ms) {
            remaining_wait_ms = 0;
        } else {
            remaining_wait_ms -= waited_ms;
        }
        memcpy(s_tts_frame_pool[slot_idx].data, data + offset, (size_t)chunk_len);
        s_tts_frame_pool[slot_idx].len = (uint16_t)chunk_len;
        s_tts_frame_pool[slot_idx].enqueued_us = (uint64_t)esp_timer_get_time();

        if (xQueueSendToBack(s_tts_pending_slots, &slot_idx, 0) != pdTRUE) {
            (void)xQueueSendToBack(s_tts_free_slots, &slot_idx, 0);
            s_tts_dropped_frames++;
            s_tts_deferred_status = WS_TTS_DEFERRED_STATUS_ENQUEUE_FAILED;
            xSemaphoreGive(s_tts_queue_lock);
            return;
        }

        ws_tts_buffer_policy_enqueue(&s_tts_buffer_policy, (uint32_t)chunk_len);
        s_tts_enqueued_frames++;
        {
            uint32_t pending = (uint32_t)uxQueueMessagesWaiting(s_tts_pending_slots);
            if (pending > s_tts_high_watermark) {
                s_tts_high_watermark = pending;
            }
        }
        ws_tts_finish_starvation_locked();

        xSemaphoreGive(s_tts_queue_lock);

        if (waited_ms >= WS_TTS_WORKER_WAIT_MS) {
            ESP_LOGD(TAG, "tts enqueue backpressure: waited_ms=%lu timeout_ms=%lu", (unsigned long)waited_ms,
                     (unsigned long)WS_TTS_ENQUEUE_TIMEOUT_MS);
        }

        /* Do not call esp_websocket_client_send_* from its receive callback.
         * Playback-worker feedback below reports the same watermarks without
         * re-entering the websocket client's internal send lock. */
        offset += chunk_len;
    }
}

void ws_tts_complete(void) {
    uint32_t generation = 0U;
    uint32_t pending_frames = 0U;
    uint16_t stream_id = 0U;
    bool inflight_active = false;

    s_waiting_for_response = false;

    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, portMAX_DELAY) == pdTRUE) {
        generation = s_tts_queue_generation;
        stream_id = s_tts_current_stream_id;
        pending_frames = ws_tts_pending_frames_locked();
        inflight_active = s_tts_inflight_active;
        if (stream_id != 0U) {
            s_tts_end_pending = true;
            s_tts_server_eos_deadline_us = 0;
            s_tts_server_eos_generation = 0;
            s_tts_server_eos_stream_id = 0;
        }
        xSemaphoreGive(s_tts_queue_lock);
    }

    if (stream_id == 0U) {
        ESP_LOGW(TAG, "Ignoring TTS network EOS without active stream");
        return;
    }
    ESP_LOGI(TAG, "TTS playout event=network_eos stream_id=%u generation=%lu pending=%lu inflight=%d",
             (unsigned int)stream_id, (unsigned long)generation, (unsigned long)pending_frames,
             inflight_active ? 1 : 0);
}

void ws_client_note_tts_downlink_complete(void) {
    uint16_t stream_id = 0U;
    uint32_t generation = 0U;

    if (s_tts_queue_lock == NULL || xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "Unable to arm TTS server-complete fallback: queue busy");
        return;
    }

    if (!s_tts_end_pending && s_tts_current_stream_id != 0U) {
        stream_id = s_tts_current_stream_id;
        generation = s_tts_queue_generation;
        s_tts_server_eos_stream_id = stream_id;
        s_tts_server_eos_generation = generation;
        s_tts_server_eos_deadline_us = esp_timer_get_time() + (int64_t)WS_TTS_SERVER_EOS_FALLBACK_MS * 1000LL;
    }
    xSemaphoreGive(s_tts_queue_lock);

    if (stream_id != 0U) {
        ESP_LOGI(TAG, "TTS server completion observed; waiting %u ms for binary EOS stream_id=%u generation=%lu",
                 (unsigned int)WS_TTS_SERVER_EOS_FALLBACK_MS, (unsigned int)stream_id, (unsigned long)generation);
    }
}

void ws_tts_timeout_check(void) {
    uint16_t stream_id = 0U;
    uint32_t generation = 0U;
    uint32_t pending_frames = 0U;
    bool inflight_active = false;
    bool fallback_due = false;
    const int64_t now_us = esp_timer_get_time();

    if (s_tts_queue_lock != NULL && xSemaphoreTake(s_tts_queue_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_tts_server_eos_deadline_us > 0 && now_us >= s_tts_server_eos_deadline_us) {
            if (!s_tts_end_pending && s_tts_current_stream_id == s_tts_server_eos_stream_id &&
                s_tts_queue_generation == s_tts_server_eos_generation) {
                stream_id = s_tts_current_stream_id;
                generation = s_tts_queue_generation;
                pending_frames = ws_tts_pending_frames_locked();
                inflight_active = s_tts_inflight_active;
                s_tts_end_pending = true;
                fallback_due = true;
            }
            s_tts_server_eos_deadline_us = 0;
            s_tts_server_eos_generation = 0;
            s_tts_server_eos_stream_id = 0;
        }
        xSemaphoreGive(s_tts_queue_lock);
    }

    if (fallback_due) {
        ESP_LOGW(
            TAG, "TTS playout fallback=server_complete_timeout stream_id=%u generation=%lu pending=%lu inflight=%d",
            (unsigned int)stream_id, (unsigned long)generation, (unsigned long)pending_frames, inflight_active ? 1 : 0);
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    if (s_waiting_for_response) {
        int64_t elapsed_ms = (now_us - s_response_wait_start_time) / 1000;
        if (elapsed_ms > WS_RESPONSE_TIMEOUT_MS && s_timeout_display_count < 1) {
            ESP_LOGW(TAG, "response timeout (%lld ms), deferring wake word resume until sleep", elapsed_ms);
            s_waiting_for_response = false;
            ws_defer_wake_word_until_sleep();
            behavior_state_set_with_text("error", "Timeout", 0);
            s_timeout_display_count++;
        }
    }
#endif
}
