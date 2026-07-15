#include "animation_service.h"
#include "behavior_state_service.h"
#include "display_ui.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_audio.h"
#include "hal_wake_word.h"
#include "sensecap-watcher.h"
#include "sfx_service.h"
#include "voice_service.h"
#include "voice_remote_control_core.h"
#include "voice_upload_guard.h"
#include "voice_wake_lifecycle.h"
#include "ws_client.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>

#define TAG "VOICE"
#define LISTENING_UI_MIN_INTERNAL_FREE_BYTES (48U * 1024U)
#define LISTENING_UI_MIN_INTERNAL_LARGEST_BYTES (24U * 1024U)
#define LISTENING_UI_MIN_DMA_LARGEST_BYTES (24U * 1024U)
#define LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_FREE_BYTES (20U * 1024U)
#define LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_LARGEST_BYTES (6U * 1024U)
#ifdef CONFIG_VOICE_WAKE_WORD_RUNTIME_ENABLED
#define VOICE_WAKE_WORD_RUNTIME_ENABLED CONFIG_VOICE_WAKE_WORD_RUNTIME_ENABLED
#else
#define VOICE_WAKE_WORD_RUNTIME_ENABLED 0
#endif
#ifdef CONFIG_VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_KB
#define VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_BYTES ((size_t)CONFIG_VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_KB * 1024U)
#else
#define VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_BYTES (36U * 1024U)
#endif
#ifdef CONFIG_VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_KB
#define VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_BYTES ((size_t)CONFIG_VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_KB * 1024U)
#else
#define VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)
#endif
#ifdef CONFIG_VOICE_WAKE_RESUME_MIN_DMA_LARGEST_KB
#define VOICE_WAKE_RESUME_MIN_DMA_LARGEST_BYTES ((size_t)CONFIG_VOICE_WAKE_RESUME_MIN_DMA_LARGEST_KB * 1024U)
#else
#define VOICE_WAKE_RESUME_MIN_DMA_LARGEST_BYTES (12U * 1024U)
#endif

/* ------------------------------------------------------------------ */
/* Private: Wake word context                                         */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_ENABLE_WAKE_WORD
static voice_wake_lifecycle_t g_wake_lifecycle;
static bool g_wake_lifecycle_configured = false;
static StaticSemaphore_t g_wake_lifecycle_lock_buffer;
static SemaphoreHandle_t g_wake_lifecycle_lock = NULL;

/* Forward declarations */
static void on_wake_word_detected(const char *wake_word, void *user_data);
static void wake_word_configure(void);
static esp_err_t wake_word_resume_standby(const char *reason);
static void wake_word_cleanup(const char *reason);
#endif /* CONFIG_ENABLE_WAKE_WORD */

/* ------------------------------------------------------------------ */
/* Private: State and statistics                                       */
/* ------------------------------------------------------------------ */

static voice_state_t g_state = VOICE_STATE_IDLE;
static voice_stats_t g_stats = {0};
static QueueHandle_t g_event_queue = NULL;
static TaskHandle_t g_voice_task_handle = NULL;
static volatile bool g_task_running = false;
static volatile bool g_closing = false;
static bool g_button_callback_registered = false;
static volatile TickType_t g_button_suppressed_until_tick = 0;

/* Track how recording was triggered (for button behavior) */
static bool g_recording_triggered_by_wake_word = false;
static portMUX_TYPE g_remote_mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
static voice_remote_mailbox_state_t g_remote_mailbox = {
    .target = {
        .recording_desired = false,
        .recording_permitted = true,
        .generation = 1U,
    },
    .accepting_remote = false,
    .suspend_requested_generation = 0U,
};
static voice_remote_control_state_t g_remote_control = {
    .recording_desired = false,
    .recording_permitted = true,
    .applied_generation = 0U,
};
static uint32_t g_remote_suspend_applied_generation = 0U;
static StaticSemaphore_t g_remote_suspend_serial_buffer;
static SemaphoreHandle_t g_remote_suspend_serial = NULL;
static StaticSemaphore_t g_remote_suspend_ack_buffer;
static SemaphoreHandle_t g_remote_suspend_ack = NULL;
static bool g_behavior_feedback_enabled = true;
static voice_upload_guard_t g_upload_guard = {0};

/* Audio buffer for PCM data (16kHz, 16-bit, 60ms frame = 1920 bytes) */
#define PCM_FRAME_SIZE 1920
#define VOICE_SUSPEND_APPLY_WAIT_MS 300

static uint8_t g_pcm_buf[PCM_FRAME_SIZE];

#define VOICE_EVENT_QUEUE_LEN 4

static bool default_transport_is_ready(void *user_ctx) {
    (void)user_ctx;
    return ws_client_is_session_ready() != 0;
}

static void default_transport_abort_playback(void *user_ctx) {
    (void)user_ctx;
    ws_client_abort_tts_playback();
}

static int default_transport_send_audio(const uint8_t *data, int len, void *user_ctx) {
    (void)user_ctx;
    return ws_send_audio(data, len);
}

static int default_transport_send_audio_end(void *user_ctx) {
    (void)user_ctx;
    return ws_send_audio_end();
}

static int default_transport_cancel_audio(void *user_ctx) {
    (void)user_ctx;
    return ws_cancel_audio_upload();
}

static const voice_transport_t s_default_transport = {
    .is_ready = default_transport_is_ready,
    .abort_playback = default_transport_abort_playback,
    .send_audio = default_transport_send_audio,
    .send_audio_end = default_transport_send_audio_end,
    .cancel_audio = default_transport_cancel_audio,
    .user_ctx = NULL,
};

static voice_transport_t g_transport = {
    .is_ready = default_transport_is_ready,
    .abort_playback = default_transport_abort_playback,
    .send_audio = default_transport_send_audio,
    .send_audio_end = default_transport_send_audio_end,
    .cancel_audio = default_transport_cancel_audio,
    .user_ctx = NULL,
};

static bool voice_transport_is_ready(void) {
    return g_transport.is_ready != NULL && g_transport.is_ready(g_transport.user_ctx);
}

static void voice_transport_abort_playback(void) {
    if (g_transport.abort_playback != NULL) {
        g_transport.abort_playback(g_transport.user_ctx);
    }
}

static int voice_transport_send_audio(const uint8_t *data, int len) {
    if (g_transport.send_audio == NULL) {
        return -1;
    }
    return g_transport.send_audio(data, len, g_transport.user_ctx);
}

static int voice_transport_send_audio_end(void) {
    if (g_transport.send_audio_end == NULL) {
        return -1;
    }
    return g_transport.send_audio_end(g_transport.user_ctx);
}

static int voice_transport_cancel_audio(void) {
    if (g_transport.cancel_audio == NULL) {
        return -1;
    }
    return g_transport.cancel_audio(g_transport.user_ctx);
}

typedef struct {
    voice_remote_snapshot_t target;
    uint32_t suspend_requested_generation;
} voice_remote_mailbox_snapshot_t;

static void voice_remote_sync_ensure(void) {
    if (g_remote_suspend_serial == NULL) {
        g_remote_suspend_serial = xSemaphoreCreateMutexStatic(&g_remote_suspend_serial_buffer);
    }
    if (g_remote_suspend_ack == NULL) {
        g_remote_suspend_ack = xSemaphoreCreateBinaryStatic(&g_remote_suspend_ack_buffer);
    }
}

static voice_remote_mailbox_snapshot_t voice_remote_mailbox_snapshot(void) {
    voice_remote_mailbox_snapshot_t snapshot;

    portENTER_CRITICAL(&g_remote_mailbox_lock);
    snapshot.target = g_remote_mailbox.target;
    snapshot.suspend_requested_generation = g_remote_mailbox.suspend_requested_generation;
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
    return snapshot;
}

static bool voice_remote_publish_desired(bool should_record) {
    bool accepted;

    portENTER_CRITICAL(&g_remote_mailbox_lock);
    accepted = voice_remote_mailbox_publish_desired(&g_remote_mailbox, should_record);
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
    return accepted;
}

static bool voice_remote_publish_permitted(bool permitted) {
    bool changed;

    portENTER_CRITICAL(&g_remote_mailbox_lock);
    changed = voice_remote_mailbox_publish_permitted(&g_remote_mailbox, permitted);
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
    return changed;
}

static void voice_remote_prepare_session(void) {
    portENTER_CRITICAL(&g_remote_mailbox_lock);
    voice_remote_mailbox_prepare_session(&g_remote_mailbox);
    g_remote_suspend_applied_generation = 0U;
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
}

static void voice_remote_open_session(void) {
    portENTER_CRITICAL(&g_remote_mailbox_lock);
    voice_remote_mailbox_open_session(&g_remote_mailbox);
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
}

static void voice_remote_close_session(void) {
    portENTER_CRITICAL(&g_remote_mailbox_lock);
    voice_remote_mailbox_close_session(&g_remote_mailbox);
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
}

static void voice_remote_acknowledge_pending_suspend(void) {
    bool needs_ack = false;

    portENTER_CRITICAL(&g_remote_mailbox_lock);
    if (g_remote_mailbox.suspend_requested_generation != 0U &&
        g_remote_mailbox.suspend_requested_generation != g_remote_suspend_applied_generation) {
        g_remote_suspend_applied_generation = g_remote_mailbox.suspend_requested_generation;
        needs_ack = true;
    }
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
    if (needs_ack && g_remote_suspend_ack != NULL) {
        xSemaphoreGive(g_remote_suspend_ack);
    }
}

static void voice_reset_runtime_state(void) {
    g_state = VOICE_STATE_IDLE;
    g_recording_triggered_by_wake_word = false;
    voice_remote_control_init(&g_remote_control);
    voice_upload_guard_reset(&g_upload_guard, 0U);
#ifdef CONFIG_ENABLE_WAKE_WORD
    g_wake_lifecycle_configured = false;
#endif
}

static void voice_delete_event_queue(void) {
    if (g_event_queue != NULL) {
        vQueueDelete(g_event_queue);
        g_event_queue = NULL;
    }
}

#ifdef CONFIG_VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES
#define VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES CONFIG_VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES
#else
#define VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES 60
#endif
#ifdef CONFIG_WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES
#define WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES CONFIG_WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES
#else
#define WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES 120
#endif

#if CONFIG_WATCHER_LOG_HEAP_DIAGNOSTICS
#define LOG_INTERNAL_HEAP_STATE(stage) log_internal_heap_state(stage)
static void log_internal_heap_state(const char *stage);
#else
#define LOG_INTERNAL_HEAP_STATE(stage) ((void)0)
#endif

#ifdef CONFIG_ENABLE_WAKE_WORD
static bool wake_lifecycle_is_supported(void *user_data) {
    (void)user_data;
    return hal_wake_word_is_supported();
}

static int wake_lifecycle_start_audio(void *user_data) {
    (void)user_data;
    hal_audio_set_playback_mode(false);
    hal_audio_set_sample_rate(16000);
    hal_audio_set_wake_word_stream_desired(true);
    int ret = hal_audio_start();
    if (ret != 0) {
        hal_audio_set_wake_word_stream_desired(false);
    }
    return ret;
}

static int wake_lifecycle_enter_audio_idle(void *user_data) {
    (void)user_data;
    hal_audio_set_wake_word_stream_desired(false);
    return hal_audio_enter_app_idle();
}

static wake_word_ctx_t *wake_lifecycle_init_detector(const wake_word_config_t *config, void *user_data) {
    (void)user_data;
    return hal_wake_word_init(config);
}

static void wake_lifecycle_start_detector(wake_word_ctx_t *ctx, void *user_data) {
    (void)user_data;
    hal_wake_word_start(ctx);
}

static void wake_lifecycle_feed_detector(wake_word_ctx_t *ctx, const int16_t *samples, size_t num_samples,
                                         void *user_data) {
    (void)user_data;
    hal_wake_word_feed(ctx, samples, num_samples);
}

static size_t wake_lifecycle_get_feed_size(wake_word_ctx_t *ctx, void *user_data) {
    (void)user_data;
    return hal_wake_word_get_feed_size(ctx);
}

static void wake_lifecycle_stop_detector(wake_word_ctx_t *ctx, void *user_data) {
    (void)user_data;
    hal_audio_set_wake_word_stream_desired(false);
    hal_wake_word_stop(ctx);
}

static void wake_lifecycle_deinit_detector(wake_word_ctx_t *ctx, void *user_data) {
    (void)user_data;
    hal_wake_word_deinit(ctx);
}

static void wake_lifecycle_delay_ms(uint32_t delay_ms, void *user_data) {
    (void)user_data;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static bool wake_lifecycle_ensure_lock(void) {
    if (g_wake_lifecycle_lock == NULL) {
        g_wake_lifecycle_lock = xSemaphoreCreateRecursiveMutexStatic(&g_wake_lifecycle_lock_buffer);
        if (g_wake_lifecycle_lock == NULL) {
            ESP_LOGE(TAG, "Failed to create wake lifecycle lock");
            return false;
        }
    }
    return true;
}

static bool wake_lifecycle_lock(void *user_data) {
    (void)user_data;
    if (g_wake_lifecycle_lock == NULL) {
        ESP_LOGE(TAG, "Wake lifecycle lock is not initialized");
        return false;
    }

    return xSemaphoreTakeRecursive(g_wake_lifecycle_lock, portMAX_DELAY) == pdTRUE;
}

static void wake_lifecycle_unlock(void *user_data) {
    (void)user_data;
    if (g_wake_lifecycle_lock != NULL) {
        xSemaphoreGiveRecursive(g_wake_lifecycle_lock);
    }
}

static void wake_lifecycle_snapshot(voice_wake_heap_snapshot_t *out_snapshot, void *user_data) {
    (void)user_data;
    if (out_snapshot == NULL) {
        return;
    }
    out_snapshot->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    out_snapshot->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    out_snapshot->dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    out_snapshot->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool wake_lifecycle_can_resume(const voice_wake_heap_snapshot_t *snapshot, void *user_data) {
    (void)user_data;
    if (snapshot == NULL) {
        return true;
    }

    if (snapshot->internal_free < VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_BYTES ||
        snapshot->internal_largest < VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_BYTES ||
        snapshot->dma_largest < VOICE_WAKE_RESUME_MIN_DMA_LARGEST_BYTES) {
        ESP_LOGW(TAG,
                 "Wake word resume skipped due to low heap: int_free=%uKB int_largest=%uKB dma_largest=%uKB "
                 "need>=%u/%u/%uKB",
                 (unsigned)(snapshot->internal_free / 1024U), (unsigned)(snapshot->internal_largest / 1024U),
                 (unsigned)(snapshot->dma_largest / 1024U),
                 (unsigned)(VOICE_WAKE_RESUME_MIN_INTERNAL_FREE_BYTES / 1024U),
                 (unsigned)(VOICE_WAKE_RESUME_MIN_INTERNAL_LARGEST_BYTES / 1024U),
                 (unsigned)(VOICE_WAKE_RESUME_MIN_DMA_LARGEST_BYTES / 1024U));
        return false;
    }

    return true;
}

static void wake_lifecycle_log(const char *stage, const char *reason, const voice_wake_heap_snapshot_t *before,
                               const voice_wake_heap_snapshot_t *after, void *user_data) {
    (void)user_data;
    const char *safe_stage = stage != NULL ? stage : "unknown";
    const char *safe_reason = reason != NULL ? reason : "unspecified";
    const voice_wake_heap_snapshot_t empty = {0};
    const voice_wake_heap_snapshot_t *b = before != NULL ? before : &empty;
    const voice_wake_heap_snapshot_t *a = after != NULL ? after : &empty;

    ESP_LOGI(TAG,
             "evt=voice_wake_resource stage=%s reason=\"%s\" "
             "before{int_free=%uKB int_largest=%uKB dma_largest=%uKB psram_free=%uKB} "
             "after{int_free=%uKB int_largest=%uKB dma_largest=%uKB psram_free=%uKB}",
             safe_stage, safe_reason, (unsigned)(b->internal_free / 1024U), (unsigned)(b->internal_largest / 1024U),
             (unsigned)(b->dma_largest / 1024U), (unsigned)(b->psram_free / 1024U),
             (unsigned)(a->internal_free / 1024U), (unsigned)(a->internal_largest / 1024U),
             (unsigned)(a->dma_largest / 1024U), (unsigned)(a->psram_free / 1024U));
}

static const voice_wake_lifecycle_ops_t s_wake_lifecycle_ops = {
    .is_supported = wake_lifecycle_is_supported,
    .start_audio = wake_lifecycle_start_audio,
    .enter_audio_idle = wake_lifecycle_enter_audio_idle,
    .wake_init = wake_lifecycle_init_detector,
    .wake_start = wake_lifecycle_start_detector,
    .wake_feed = wake_lifecycle_feed_detector,
    .wake_get_feed_size = wake_lifecycle_get_feed_size,
    .wake_stop = wake_lifecycle_stop_detector,
    .wake_deinit = wake_lifecycle_deinit_detector,
    .delay_ms = wake_lifecycle_delay_ms,
    .snapshot = wake_lifecycle_snapshot,
    .can_resume = wake_lifecycle_can_resume,
    .lock = wake_lifecycle_lock,
    .unlock = wake_lifecycle_unlock,
    .log = wake_lifecycle_log,
};
#endif

static void log_cloud_not_ready(void) {
    ESP_LOGW(TAG, "Cloud is not ready; local voice state remains unchanged (connected=%d)", ws_client_is_connected());
}

#if CONFIG_WATCHER_LOG_HEAP_DIAGNOSTICS
static void log_internal_heap_state(const char *stage) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Internal heap @ %s: free=%u KB, largest=%u KB", stage, (unsigned)(free_internal / 1024U),
             (unsigned)(largest_internal / 1024U));
}
#endif

static bool has_listening_ui_headroom(size_t *free_internal_out, size_t *largest_internal_out,
                                      size_t *largest_dma_out) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT);

    if (free_internal_out != NULL) {
        *free_internal_out = free_internal;
    }
    if (largest_internal_out != NULL) {
        *largest_internal_out = largest_internal;
    }
    if (largest_dma_out != NULL) {
        *largest_dma_out = largest_dma;
    }

    return free_internal >= LISTENING_UI_MIN_INTERNAL_FREE_BYTES &&
           largest_internal >= LISTENING_UI_MIN_INTERNAL_LARGEST_BYTES &&
           largest_dma >= LISTENING_UI_MIN_DMA_LARGEST_BYTES;
}

static bool has_text_only_listening_ui_headroom(size_t *free_internal_out, size_t *largest_internal_out) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (free_internal_out != NULL) {
        *free_internal_out = free_internal;
    }
    if (largest_internal_out != NULL) {
        *largest_internal_out = largest_internal;
    }

    return free_internal >= LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_FREE_BYTES &&
           largest_internal >= LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_LARGEST_BYTES;
}

static bool current_display_is_awake_idle_variant(emoji_anim_type_t *current_out) {
    animation_snapshot_t snapshot;
    emoji_anim_type_t current = EMOJI_ANIM_NONE;
    if (animation_get_snapshot(&snapshot) == ANIMATION_SERVICE_OK) {
        current = snapshot.visible_type;
    }
    if (current_out != NULL) {
        *current_out = current;
    }
    return current == EMOJI_ANIM_STANDBY_1 || current == EMOJI_ANIM_STANDBY_2 || current == EMOJI_ANIM_STANDBY_3 ||
           current == EMOJI_ANIM_STANDBY_4;
}

static bool current_display_is_sleep_standby(emoji_anim_type_t current) {
    return current == EMOJI_ANIM_STANDBY_LOOP || current == EMOJI_ANIM_STANDBY_START;
}

static void show_listening_ui(void) {
    if (!g_behavior_feedback_enabled) {
        return;
    }

    size_t free_internal = 0;
    size_t largest_internal = 0;
    size_t largest_dma = 0;
    emoji_anim_type_t current_emoji = EMOJI_ANIM_NONE;
    const char *current_behavior_state = behavior_state_get_current();
    const bool wake_transition_in_progress =
        current_behavior_state != NULL && strcmp(current_behavior_state, "listening_wake") == 0;
    const bool skip_wake_intro = current_display_is_awake_idle_variant(&current_emoji);
    const bool use_wake_intro = wake_transition_in_progress || current_display_is_sleep_standby(current_emoji);
    const char *listening_state = use_wake_intro ? "listening_wake" : "listening";

    bool full_anim_headroom = has_listening_ui_headroom(&free_internal, &largest_internal, &largest_dma);

    ESP_LOGI(TAG,
             "Listening UI decision current_emoji=%d skip_wake_intro=%d wake_intro=%d "
             "full_anim_headroom=%d heap{free=%uKB largest=%uKB dma_largest=%uKB}",
             (int)current_emoji, skip_wake_intro ? 1 : 0, use_wake_intro ? 1 : 0, full_anim_headroom ? 1 : 0,
             (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U), (unsigned)(largest_dma / 1024U));

    if (full_anim_headroom && use_wake_intro && !wake_transition_in_progress) {
        (void)animation_prefetch_hint(EMOJI_ANIM_STANDBY_END);
    } else if (full_anim_headroom && !use_wake_intro) {
        (void)animation_prefetch_hint(EMOJI_ANIM_LISTENING);
    }

    if (!use_wake_intro) {
        ESP_LOGI(TAG, "Listening UI will skip wake intro from non-sleep state");
    }

    if (full_anim_headroom) {
        behavior_state_set_with_resources(listening_state, "", 0, NULL, "");
        return;
    }

    if (has_text_only_listening_ui_headroom(&free_internal, &largest_internal)) {
        ESP_LOGW(TAG, "Low internal heap, using text-only listening UI: free=%u KB largest=%u KB dma_largest=%u KB",
                 (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U),
                 (unsigned)(largest_dma / 1024U));
        behavior_state_set_with_resources("listening", "", 24, "", "");
        return;
    }

    ESP_LOGW(TAG, "Very low internal heap, skipping listening UI refresh: free=%u KB largest=%u KB dma_largest=%u KB",
             (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U), (unsigned)(largest_dma / 1024U));
}

/* ------------------------------------------------------------------ */
/* Private: VAD (Voice Activity Detection)                             */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_ENABLE_WAKE_WORD
/* VAD state */
static int g_vad_silence_frames = 0; /* Consecutive silent frames */
static int g_vad_speech_frames = 0;  /* Total speech frames in this recording */

/* VAD configuration from Kconfig */
#define VAD_FRAME_MS 60 /* Each frame is 60ms */
#define VAD_SILENCE_FRAMES (CONFIG_VAD_SILENCE_TIMEOUT_MS / VAD_FRAME_MS)
#define VAD_RMS_THRESHOLD CONFIG_VAD_RMS_THRESHOLD
#define VAD_MIN_SPEECH_FRAMES (CONFIG_VAD_MIN_SPEECH_MS / VAD_FRAME_MS)

/* VAD control: only enable when wake word triggered */
static bool g_vad_enabled = false;

static void vad_reset(void) {
    g_vad_silence_frames = 0;
    g_vad_speech_frames = 0;
    g_vad_enabled = true;
    ESP_LOGD(TAG, "VAD reset, silence_threshold=%d frames, rms_threshold=%d, min_speech=%d frames", VAD_SILENCE_FRAMES,
             VAD_RMS_THRESHOLD, VAD_MIN_SPEECH_FRAMES);
}

static void vad_disable(void) {
    g_vad_enabled = false;
}

/**
 * Process VAD on a frame
 * @param rms RMS value of the audio frame
 * @return true if recording should stop (silence timeout)
 */
static bool vad_process_frame(int rms) {
    if (!g_vad_enabled) {
        return false;
    }

    /* Skip VAD if silence timeout is disabled (0) */
    if (VAD_SILENCE_FRAMES <= 0) {
        return false;
    }

    if (rms < VAD_RMS_THRESHOLD) {
        /* Silent frame */
        g_vad_silence_frames++;

        /* Log every 10 silent frames */
        if (g_vad_silence_frames % 10 == 0) {
            ESP_LOGD(TAG, "VAD: silence_frames=%d/%d, rms=%d (threshold=%d)", g_vad_silence_frames, VAD_SILENCE_FRAMES,
                     rms, VAD_RMS_THRESHOLD);
        }

        /* Check if silence timeout reached and minimum speech achieved */
        if (g_vad_silence_frames >= VAD_SILENCE_FRAMES && g_vad_speech_frames >= VAD_MIN_SPEECH_FRAMES) {
            ESP_LOGI(TAG, "VAD: Silence timeout detected! speech_frames=%d, silence_frames=%d", g_vad_speech_frames,
                     g_vad_silence_frames);
            return true; /* Signal to stop recording */
        }
    } else {
        /* Speech frame */
        g_vad_silence_frames = 0; /* Reset silence counter */
        g_vad_speech_frames++;

        /* Log every 20 speech frames */
        if (g_vad_speech_frames % 20 == 0) {
            ESP_LOGD(TAG, "VAD: speech_frames=%d, rms=%d", g_vad_speech_frames, rms);
        }
    }

    return false;
}
#endif /* CONFIG_ENABLE_WAKE_WORD */

/* ------------------------------------------------------------------ */
/* Public: Initialize                                                 */
/* ------------------------------------------------------------------ */

void voice_recorder_init(void) {
    g_state = VOICE_STATE_IDLE;
    memset(&g_stats, 0, sizeof(g_stats));
    voice_remote_sync_ensure();
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (VOICE_WAKE_WORD_RUNTIME_ENABLED) {
        (void)wake_lifecycle_ensure_lock();
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Public: Get current state                                          */
/* ------------------------------------------------------------------ */

voice_state_t voice_recorder_get_state(void) {
    return g_state;
}

/* ------------------------------------------------------------------ */
/* Public: Reset statistics                                           */
/* ------------------------------------------------------------------ */

void voice_recorder_reset_stats(void) {
    g_stats.record_count = 0;
    g_stats.encode_count = 0;
    g_stats.error_count = 0;
}

void voice_recorder_set_transport(const voice_transport_t *transport) {
    if (transport == NULL) {
        voice_recorder_reset_transport();
        return;
    }

    g_transport = *transport;
}

void voice_recorder_reset_transport(void) {
    g_transport = s_default_transport;
}

void voice_recorder_set_behavior_feedback_enabled(bool enabled) {
    g_behavior_feedback_enabled = enabled;
}

/* ------------------------------------------------------------------ */
/* Public: Get statistics                                             */
/* ------------------------------------------------------------------ */

void voice_recorder_get_stats(voice_stats_t *out_stats) {
    if (out_stats) {
        out_stats->record_count = g_stats.record_count;
        out_stats->encode_count = g_stats.encode_count;
        out_stats->error_count = g_stats.error_count;
        out_stats->current_state = (int)g_state;
    }
}

static void log_waiting_for_server_response(void) {
    ESP_LOGI(TAG, "Audio turn ended; local voice state remains unchanged while waiting for the server");
}

/* ------------------------------------------------------------------ */
/* Private: Start recording                                           */
/* ------------------------------------------------------------------ */

static int start_recording(void) {
    ws_client_audio_queue_stats_t queue_stats = {0};

    if (!g_remote_control.recording_permitted) {
        ESP_LOGI(TAG, "Recording start deferred until animation gate opens");
        return -1;
    }
    if (!voice_transport_is_ready()) {
        ESP_LOGW(TAG, "start_recording blocked: ws session not ready (connected=%d)", ws_client_is_connected());
        log_cloud_not_ready();
        g_stats.error_count++;
        return -1;
    }

    sfx_service_set_voice_audio_busy(true);
    sfx_service_stop();
    voice_transport_abort_playback();

#ifdef CONFIG_ENABLE_WAKE_WORD
    wake_word_cleanup("recording start");
#endif

    LOG_INTERNAL_HEAP_STATE("before_recording");
    hal_audio_set_playback_mode(false);
    hal_audio_set_sample_rate(16000);
    ESP_LOGI(TAG, "start_recording: calling hal_audio_start()");
    if (hal_audio_start() != 0) {
        ESP_LOGE(TAG, "start_recording: hal_audio_start failed");
        (void)behavior_state_refresh_animation();
        g_stats.error_count++;
        return -1;
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    if (g_recording_triggered_by_wake_word) {
        vad_reset();
        ESP_LOGI(TAG, "VAD enabled: silence_timeout=%dms, rms_threshold=%d, min_speech=%dms",
                 CONFIG_VAD_SILENCE_TIMEOUT_MS, CONFIG_VAD_RMS_THRESHOLD, CONFIG_VAD_MIN_SPEECH_MS);
    }
#endif

    g_state = VOICE_STATE_RECORDING;
    ws_client_get_audio_queue_stats(&queue_stats);
    voice_upload_guard_reset(&g_upload_guard, queue_stats.dropped_frames);
    LOG_INTERNAL_HEAP_STATE("after_recording");
    ESP_LOGI(TAG, "start_recording: state -> RECORDING");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Private: Stop recording                                            */
/* ------------------------------------------------------------------ */

static int stop_recording(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (VOICE_WAKE_WORD_RUNTIME_ENABLED) {
        hal_audio_enter_app_idle();
    } else {
        hal_audio_stop();
    }
#else
    hal_audio_stop();
#endif

    /* Skip end marker if the cloud session is already gone. */
    if (voice_transport_is_ready()) {
        if (voice_transport_send_audio_end() != 0) {
            g_stats.error_count++;
            /* Still transition to idle */
        }
        log_waiting_for_server_response();
    } else {
        ESP_LOGW(TAG, "Skipping audio end marker: ws session not ready (connected=%d)", ws_client_is_connected());
        (void)behavior_state_refresh_animation();
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* Disable VAD when stopping */
    vad_disable();
#endif

    g_state = VOICE_STATE_IDLE;
    voice_upload_guard_reset(&g_upload_guard, 0U);
    g_stats.record_count++;
    g_recording_triggered_by_wake_word = false; /* Reset trigger flag */
    return 0;
}

static const char *voice_upload_abort_reason(voice_upload_guard_result_t result) {
    switch (result) {
    case VOICE_UPLOAD_ABORT_CLOUD_LOST:
        return "cloud_lost";
    case VOICE_UPLOAD_ABORT_SEND_FAILURES:
        return "consecutive_send_failures";
    case VOICE_UPLOAD_ABORT_QUEUE_OVERRUN:
        return "audio_queue_overrun";
    case VOICE_UPLOAD_CONTINUE:
    default:
        return "none";
    }
}

static void abort_recording_after_upload_failure(voice_upload_guard_result_t result) {
    if (g_state != VOICE_STATE_RECORDING || result == VOICE_UPLOAD_CONTINUE) {
        return;
    }

    ESP_LOGE(TAG, "Aborting recording after upload failure: reason=%s", voice_upload_abort_reason(result));
    (void)voice_remote_publish_desired(false);
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (VOICE_WAKE_WORD_RUNTIME_ENABLED) {
        hal_audio_enter_app_idle();
    } else {
        hal_audio_stop();
    }
    vad_disable();
#else
    hal_audio_stop();
#endif
    if (voice_transport_is_ready()) {
        if (voice_transport_cancel_audio() != 0) {
            ESP_LOGW(TAG, "Failed to enqueue audio cancel marker after upload abort");
            g_stats.error_count++;
        }
    } else {
        ESP_LOGW(TAG, "Skipping audio cancel marker: transport unavailable");
    }
    g_state = VOICE_STATE_IDLE;
    g_recording_triggered_by_wake_word = false;
    voice_upload_guard_reset(&g_upload_guard, 0U);
    sfx_service_set_voice_audio_busy(false);
    ESP_LOGW(TAG, "Audio upload failed; local voice state remains unchanged: reason=%s",
             voice_upload_abort_reason(result));
}

void voice_recorder_suspend_cloud_audio(void) {
    voice_remote_suspend_request_t request;

    voice_remote_sync_ensure();
    if (g_remote_suspend_serial == NULL || g_remote_suspend_ack == NULL) {
        ESP_LOGE(TAG, "Cloud audio suspend synchronization unavailable");
        return;
    }
    if (xSemaphoreTake(g_remote_suspend_serial, pdMS_TO_TICKS(VOICE_SUSPEND_APPLY_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Cloud audio suspend serialization timed out");
        return;
    }

    while (xSemaphoreTake(g_remote_suspend_ack, 0) == pdTRUE) {
    }
    portENTER_CRITICAL(&g_remote_mailbox_lock);
    request = voice_remote_mailbox_request_suspend(&g_remote_mailbox);
    portEXIT_CRITICAL(&g_remote_mailbox_lock);

    if (request.needs_ack &&
        xSemaphoreTake(g_remote_suspend_ack, pdMS_TO_TICKS(VOICE_SUSPEND_APPLY_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Cloud audio suspend apply timed out: generation=%" PRIu32, request.generation);
    }
    xSemaphoreGive(g_remote_suspend_serial);
}

static esp_err_t submit_remote_recording_target(bool should_record) {
    if (!voice_remote_publish_desired(should_record)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t voice_recorder_request_open(void) {
    return submit_remote_recording_target(true);
}

esp_err_t voice_recorder_request_close(void) {
    return submit_remote_recording_target(false);
}

void voice_recorder_set_recording_permitted(bool permitted) {
    bool changed = voice_remote_publish_permitted(permitted);
    if (changed) {
        ESP_LOGI(TAG, "Recording animation gate %s", permitted ? "opened" : "closed");
    }
}

static void handle_short_press_toggle(void) {
    if (g_state == VOICE_STATE_IDLE) {
        if (!voice_transport_is_ready()) {
            ESP_LOGW(TAG, "Short press ignored: ws session not ready (connected=%d)", ws_client_is_connected());
            log_cloud_not_ready();
            return;
        }

        ESP_LOGI(TAG, "Short press - starting recording");
        g_recording_triggered_by_wake_word = false;
        voice_recorder_process_event(VOICE_EVENT_BUTTON_SHORT_CLICK);
        if (g_state == VOICE_STATE_RECORDING) {
            show_listening_ui();
        } else {
            log_cloud_not_ready();
        }
        return;
    }

    if (g_state == VOICE_STATE_RECORDING) {
        ESP_LOGI(TAG, "Short press - stopping recording");
        voice_recorder_process_event(VOICE_EVENT_BUTTON_SHORT_CLICK);
        log_waiting_for_server_response();
    }
}

static void voice_task_apply_suspend(uint32_t requested_generation) {
    if (g_state == VOICE_STATE_RECORDING) {
        ESP_LOGW(TAG, "Suspending active recording without stopping button runtime");
        hal_audio_stop();
#ifdef CONFIG_ENABLE_WAKE_WORD
        vad_disable();
#endif
        g_state = VOICE_STATE_IDLE;
        g_recording_triggered_by_wake_word = false;
    }
    if (g_event_queue != NULL) {
        xQueueReset(g_event_queue);
    }

    portENTER_CRITICAL(&g_remote_mailbox_lock);
    g_remote_suspend_applied_generation = requested_generation;
    portEXIT_CRITICAL(&g_remote_mailbox_lock);
    if (g_remote_suspend_ack != NULL) {
        xSemaphoreGive(g_remote_suspend_ack);
    }
}

static void handle_remote_control_snapshot(void) {
    voice_remote_mailbox_snapshot_t snapshot = voice_remote_mailbox_snapshot();
    if (snapshot.suspend_requested_generation != 0U &&
        snapshot.suspend_requested_generation != g_remote_suspend_applied_generation) {
        voice_task_apply_suspend(snapshot.suspend_requested_generation);
    }

    voice_remote_action_t action =
        voice_remote_control_apply(&g_remote_control, snapshot.target, g_state == VOICE_STATE_RECORDING);

    if (action == VOICE_REMOTE_ACTION_START) {
        ESP_LOGI(TAG, "Remote microphone target recording - starting recording");
        g_recording_triggered_by_wake_word = false;
        (void)start_recording();
        if (g_state == VOICE_STATE_RECORDING) {
            show_listening_ui();
        } else {
            log_cloud_not_ready();
        }
    } else if (action == VOICE_REMOTE_ACTION_STOP) {
        ESP_LOGI(TAG, "Remote microphone target idle - stopping recording");
        (void)stop_recording();
        log_waiting_for_server_response();
    }
}

#ifdef CONFIG_ENABLE_WAKE_WORD
static void handle_wake_word_event(void) {
    if (g_state == VOICE_STATE_IDLE) {
        g_recording_triggered_by_wake_word = true;
    }
    voice_recorder_process_event(VOICE_EVENT_WAKE_WORD);
    if (g_state == VOICE_STATE_RECORDING) {
        show_listening_ui();
    } else {
        g_recording_triggered_by_wake_word = false;
        log_cloud_not_ready();
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Public: Process event                                              */
/* ------------------------------------------------------------------ */

void voice_recorder_process_event(voice_event_t event) {
    switch (g_state) {
    case VOICE_STATE_IDLE:
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK || event == VOICE_EVENT_WAKE_WORD) {
#ifdef CONFIG_ENABLE_WAKE_WORD
            if (event == VOICE_EVENT_WAKE_WORD) {
                ESP_LOGI(TAG, "Wake word triggered recording");
            }
#endif
            start_recording();
        }
        break;

    case VOICE_STATE_RECORDING:
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK || event == VOICE_EVENT_TIMEOUT) {
            stop_recording();
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public: Process tick (read, send)                                   */
/* ------------------------------------------------------------------ */

int voice_recorder_tick(void) {
    int pcm_len = 0;
    int16_t *samples = NULL;
    int send_ret;
    ws_client_audio_queue_stats_t queue_stats = {0};
    voice_upload_guard_result_t upload_result;

#ifdef CONFIG_ENABLE_WAKE_WORD
    static uint32_t wake_idle_frame_count = 0;
    static bool wake_audio_first_read_logged = false;

    bool wake_runtime_active = VOICE_WAKE_WORD_RUNTIME_ENABLED && g_wake_lifecycle_configured &&
                               voice_wake_lifecycle_is_active(&g_wake_lifecycle);

    if (!wake_runtime_active) {
        if (g_state != VOICE_STATE_RECORDING) {
            return 0;
        }

        pcm_len = hal_audio_read(g_pcm_buf, PCM_FRAME_SIZE);
        if (pcm_len < 0) {
            ESP_LOGE(TAG, "Audio read error");
            g_stats.error_count++;
            return -1;
        }
        if (pcm_len == 0) {
            ESP_LOGW(TAG, "Audio read: no data");
            return 0;
        }

        samples = (int16_t *)g_pcm_buf;
    } else {
        /* Read audio for both wake word detection and recording */
        if (!wake_audio_first_read_logged) {
            wake_audio_first_read_logged = true;
            ESP_LOGI(TAG, "Wake audio loop entering first microphone read");
        }
        pcm_len = hal_audio_read(g_pcm_buf, PCM_FRAME_SIZE);
        if (pcm_len < 0) {
            ESP_LOGE(TAG, "Audio read error");
            g_stats.error_count++;
            return -1;
        }
        if (pcm_len == 0) {
            return 0; /* No data available */
        }

        samples = (int16_t *)g_pcm_buf;
        size_t num_samples = pcm_len / 2; /* 16-bit samples */

        /* Feed wake word detector when idle (local detection, no network) */
        if (g_state == VOICE_STATE_IDLE) {
            size_t feed_size = 0;
            bool fed = voice_wake_lifecycle_feed(&g_wake_lifecycle, samples, num_samples, &feed_size);
            /* Yield after feed so higher-priority detection task can call fetch()
             * before we loop back. Prevents AFE FEED ring buffer overflow. */
            if (fed) {
                taskYIELD();
            }

            wake_idle_frame_count++;
            if (WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES > 0 &&
                (wake_idle_frame_count % WAKE_IDLE_STATS_LOG_INTERVAL_FRAMES) == 0U) {
                int64_t idle_sum_sq = 0;
                int16_t idle_peak = 0;
                int idle_zero_count = 0;
                for (size_t i = 0; i < num_samples; i++) {
                    int16_t s = samples[i];
                    if (s == 0) {
                        idle_zero_count++;
                    }
                    if (s < 0) {
                        s = -s;
                    }
                    idle_sum_sq += (int64_t)s * s;
                    if (s > idle_peak) {
                        idle_peak = s;
                    }
                }
                int idle_rms = num_samples > 0 ? (int)sqrt((double)(idle_sum_sq / (int64_t)num_samples)) : 0;
                ESP_LOGI(TAG, "wake_idle frame=%lu rms=%d peak=%d zeros=%d/%u feed=%u",
                         (unsigned long)wake_idle_frame_count, idle_rms, idle_peak, idle_zero_count,
                         (unsigned)num_samples, (unsigned)feed_size);
            }
        }

        /* Only send to WebSocket when recording */
        if (g_state != VOICE_STATE_RECORDING) {
            return 0;
        }
    }
#else
    /* Original behavior: only read when recording */
    if (g_state != VOICE_STATE_RECORDING) {
        return 0;
    }

    pcm_len = hal_audio_read(g_pcm_buf, PCM_FRAME_SIZE);
    if (pcm_len < 0) {
        ESP_LOGE(TAG, "Audio read error");
        g_stats.error_count++;
        return -1;
    }
    if (pcm_len == 0) {
        ESP_LOGW(TAG, "Audio read: no data");
        return 0; /* No data available */
    }

    samples = (int16_t *)g_pcm_buf;
#endif

    /* Audio quality check: calculate RMS and peak */
    int sample_count = pcm_len / 2;
    int64_t sum_sq = 0;
    int16_t peak = 0;
    int zero_count = 0;

    for (int i = 0; i < sample_count; i++) {
        int16_t s = samples[i];
        if (s == 0)
            zero_count++;
        if (s < 0)
            s = -s; /* abs */
        sum_sq += (int64_t)s * s;
        if (s > peak)
            peak = s;
    }

    int rms = (int)(sum_sq / sample_count);
    rms = (int)sqrt((double)rms);

    if (VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES > 0 &&
        g_stats.encode_count % VOICE_AUDIO_STATS_LOG_INTERVAL_FRAMES == 0) {
        ws_client_audio_queue_stats_t queue_stats = {0};
        ws_client_media_send_stats_t send_stats = {0};

        ws_client_get_audio_queue_stats(&queue_stats);
        ws_client_get_media_send_stats(&send_stats);
        ESP_LOGI(TAG,
                 "audio frame=%d rms=%d peak=%d zeros=%d/%d q{p=%u f=%u r=%u hi=%u in=%lu out=%lu drop=%lu "
                 "delay_us=%lu} "
                 "send_us=%lu/%lu packet=%u",
                 g_stats.encode_count + 1, rms, peak, zero_count, sample_count,
                 (unsigned int)queue_stats.pending_frames, (unsigned int)queue_stats.inflight_frames,
                 (unsigned int)queue_stats.resident_frames, (unsigned int)queue_stats.high_watermark,
                 (unsigned long)queue_stats.queued_frames, (unsigned long)queue_stats.sent_frames,
                 (unsigned long)queue_stats.dropped_frames, (unsigned long)queue_stats.last_queue_delay_us,
                 (unsigned long)send_stats.send_us, (unsigned long)send_stats.total_us,
                 (unsigned int)send_stats.packet_len);
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* VAD: Check for silence timeout (only in wake word mode) */
    if (g_vad_enabled && vad_process_frame(rms)) {
        ESP_LOGI(TAG, "VAD triggered stop - silence timeout");
        /* Stop recording due to silence timeout */
        voice_recorder_process_event(VOICE_EVENT_TIMEOUT);
        log_waiting_for_server_response();
        return 0; /* Recording stopped, don't send this frame */
    }
#endif

    /* Enqueue PCM for asynchronous WebSocket upload */
    send_ret = voice_transport_send_audio(g_pcm_buf, pcm_len);
    ws_client_get_audio_queue_stats(&queue_stats);
    upload_result = voice_upload_guard_observe(&g_upload_guard, voice_transport_is_ready(), send_ret == 0,
                                               queue_stats.dropped_frames);
    if (send_ret != 0) {
        if (upload_result == VOICE_UPLOAD_ABORT_CLOUD_LOST) {
            ESP_LOGW(TAG, "Cloud session lost during recording (connected=%d)", ws_client_is_connected());
        }
        g_stats.error_count++;
        /* Only log every 10 errors to avoid flooding */
        if (g_stats.error_count % 10 == 1) {
            ESP_LOGE(TAG, "WS send audio failed (count: %d)", g_stats.error_count);
        }
    }
    if (upload_result != VOICE_UPLOAD_CONTINUE) {
        if (send_ret == 0) {
            g_stats.error_count++;
        }
        abort_recording_after_upload_failure(upload_result);
        return -1;
    }
    if (send_ret != 0) {
        return -1;
    }

    g_stats.encode_count++;
    return 1; /* One frame sent */
}

static void voice_process_pending_events(void) {
    voice_event_t event = VOICE_EVENT_NONE;

    handle_remote_control_snapshot();
    while (g_event_queue != NULL && xQueueReceive(g_event_queue, &event, 0) == pdTRUE) {
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK) {
            handle_short_press_toggle();
#ifdef CONFIG_ENABLE_WAKE_WORD
        } else if (event == VOICE_EVENT_WAKE_WORD) {
            handle_wake_word_event();
#endif
        } else {
            voice_recorder_process_event(event);
        }
    }
    handle_remote_control_snapshot();
}

/* ------------------------------------------------------------------ */
/* Private: Voice recorder task                                        */
/* ------------------------------------------------------------------ */

/* Tick interval: 60ms for Opus frame size */
#define TICK_INTERVAL_MS 60
#define VOICE_TASK_EXIT_WAIT_MS 300

static bool voice_wait_for_task_exit(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (g_voice_task_handle != NULL && waited_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }

    return g_voice_task_handle == NULL;
}

static void voice_recorder_task(void *arg) {
    ESP_LOGI(TAG, "Voice recorder task started: stack_size=%u stack_hwm=%u",
             (unsigned)CONFIG_VOICE_TASK_STACK_SIZE, (unsigned)uxTaskGetStackHighWaterMark(NULL));

    while (g_task_running) {
        voice_process_pending_events();

        /* Process audio capture/upload if recording */
        voice_recorder_tick();

        vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL_MS));
    }

    g_voice_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Private: Wake word callback                                        */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_ENABLE_WAKE_WORD
static void on_wake_word_detected(const char *wake_word, void *user_data) {
    voice_event_t event = VOICE_EVENT_WAKE_WORD;

    (void)user_data;
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word);
    if (!g_task_running || g_event_queue == NULL) {
        ESP_LOGW(TAG, "Wake word event dropped: voice task not running");
        return;
    }
    if (xQueueSend(g_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Wake word event dropped: voice queue full");
    }
}

static void wake_word_configure(void) {
    wake_word_config_t config = {
        .model_path = NULL, /* Use default */
        .callback = on_wake_word_detected,
        .user_data = NULL,
    };

    if (g_wake_lifecycle_configured) {
        return;
    }

    /* Create the static mutex before the voice task can race resume/release paths. */
    if (!wake_lifecycle_ensure_lock()) {
        return;
    }

#ifdef CONFIG_WAKE_WORD_CUSTOM
    config.wake_word_phrase = CONFIG_CUSTOM_WAKE_WORD_PHRASE;
    config.detection_threshold = (float)CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
#endif

    voice_wake_lifecycle_init(&g_wake_lifecycle, &s_wake_lifecycle_ops, NULL, &config,
                              VOICE_WAKE_WORD_RUNTIME_ENABLED != 0);
    g_wake_lifecycle_configured = true;
    ESP_LOGI(TAG, "Wake word standby runtime configured; waiting for sleep standby resume");
}

static esp_err_t wake_word_resume_standby(const char *reason) {
    if (!g_wake_lifecycle_configured) {
        wake_word_configure();
    }
    if (!g_wake_lifecycle_configured) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = voice_wake_lifecycle_resume(&g_wake_lifecycle, reason);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGW(TAG, "Wake word standby runtime deferred due to low heap; button recording remains available");
        } else {
            ESP_LOGW(TAG, "Wake word standby runtime resume failed: %s; button recording remains available",
                     esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Wake word standby runtime active");
    return ESP_OK;
}

static void wake_word_cleanup(const char *reason) {
    if (g_wake_lifecycle_configured) {
        voice_wake_lifecycle_release(&g_wake_lifecycle, reason);
    }
}

#endif /* CONFIG_ENABLE_WAKE_WORD */

/* ------------------------------------------------------------------ */
/* Public: Start voice recorder system (with button and task)         */
/* ------------------------------------------------------------------ */

int voice_recorder_start(void) {
    g_closing = false;
    voice_remote_sync_ensure();
    sfx_service_set_voice_audio_busy(true);
    sfx_service_stop();

    if (g_task_running && g_voice_task_handle != NULL) {
        ESP_LOGI(TAG, "Voice recorder already running");
        return 0;
    }

    if (g_voice_task_handle != NULL && !voice_wait_for_task_exit(VOICE_TASK_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "Voice recorder task is still stopping");
        sfx_service_set_voice_audio_busy(false);
        return -1;
    }

    if (g_event_queue == NULL) {
        g_event_queue = xQueueCreate(VOICE_EVENT_QUEUE_LEN, sizeof(voice_event_t));
        if (g_event_queue == NULL) {
            ESP_LOGE(TAG, "Voice event queue create failed");
            sfx_service_set_voice_audio_busy(false);
            return -1;
        }
    } else {
        xQueueReset(g_event_queue);
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    if (VOICE_WAKE_WORD_RUNTIME_ENABLED && hal_wake_word_is_supported()) {
        wake_word_configure();
    } else if (VOICE_WAKE_WORD_RUNTIME_ENABLED) {
        ESP_LOGW(TAG, "Wake word detection requested but hardware support is unavailable");
    } else {
        ESP_LOGI(TAG, "Wake word detection disabled for voice app runtime");
    }
#endif

    if (!g_button_callback_registered) {
        g_button_callback_registered = true;
        ESP_LOGI(TAG, "Voice button uses app-level dispatcher");
    }

    voice_remote_prepare_session();
    voice_remote_control_init(&g_remote_control);

    /* Start voice recorder task */
    g_task_running = true;
    BaseType_t ret;
    ret = xTaskCreate(voice_recorder_task, "voice_task", CONFIG_VOICE_TASK_STACK_SIZE, NULL, 5, &g_voice_task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
#ifdef CONFIG_ENABLE_WAKE_WORD
        wake_word_cleanup("voice task create failed");
        hal_audio_enter_app_idle();
#endif
        g_task_running = false;
        voice_delete_event_queue();
        voice_remote_close_session();
        voice_reset_runtime_state();
        sfx_service_set_voice_audio_busy(false);
        return -1;
    }

    voice_remote_open_session();
    ESP_LOGI(TAG, "Voice recorder started");
    return 0;
}

void voice_recorder_suppress_button_clicks(uint32_t duration_ms) {
    TickType_t ticks = pdMS_TO_TICKS(duration_ms);

    if (ticks == 0) {
        ticks = 1;
    }

    g_button_suppressed_until_tick = xTaskGetTickCount() + ticks;
    ESP_LOGI(TAG, "Suppressing voice button clicks for %u ms", (unsigned)duration_ms);
}

static esp_err_t voice_recorder_stop_internal(bool release_audio_hal) {
    bool had_runtime = g_task_running || g_voice_task_handle != NULL || g_event_queue != NULL;

    g_closing = true;
    voice_transport_abort_playback();
    voice_remote_close_session();
    g_task_running = false;

    if (g_voice_task_handle != NULL && !voice_wait_for_task_exit(VOICE_TASK_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "Voice recorder task did not exit within %u ms", (unsigned)VOICE_TASK_EXIT_WAIT_MS);
        g_closing = false;
        return ESP_ERR_TIMEOUT;
    }
    voice_remote_acknowledge_pending_suspend();

#ifdef CONFIG_ENABLE_WAKE_WORD
    wake_word_cleanup("voice recorder stop");
#endif

    voice_delete_event_queue();
    voice_reset_runtime_state();

    if (release_audio_hal) {
        if (hal_audio_deinit() != 0) {
            ESP_LOGW(TAG, "Voice recorder closed with audio HAL deinit warning");
        }
    } else if (hal_audio_enter_app_idle() != 0) {
        ESP_LOGW(TAG, "Voice recorder stopped with audio path idle warning");
    }
    sfx_service_set_voice_audio_busy(false);

    if (had_runtime) {
        ESP_LOGI(TAG, "Voice recorder %s", release_audio_hal ? "closed" : "stopped");
    } else {
        ESP_LOGI(TAG, "Voice recorder already %s", release_audio_hal ? "closed" : "stopped");
    }
    g_closing = false;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public: Stop voice recorder system                                  */
/* ------------------------------------------------------------------ */

esp_err_t voice_recorder_stop(void) {
    return voice_recorder_stop_internal(false);
}

esp_err_t voice_recorder_close(void) {
    return voice_recorder_stop_internal(true);
}

/* ------------------------------------------------------------------ */
/* Public: Pause wake word detection before TTS                        */
/* ------------------------------------------------------------------ */

void voice_recorder_pause_wake_word(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    ESP_LOGI(TAG, "Releasing wake word runtime for active voice path");
    wake_word_cleanup("active voice path");
#endif
}

/* ------------------------------------------------------------------ */
/* Public: Resume wake word detection after TTS                       */
/* ------------------------------------------------------------------ */

static void voice_recorder_resume_wake_word_with_reason(const char *reason) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (!VOICE_WAKE_WORD_RUNTIME_ENABLED || !hal_wake_word_is_supported()) {
        ESP_LOGD(TAG, "Wake word resume skipped: runtime disabled or unsupported");
        return;
    }
    if (g_closing || !g_task_running || g_state != VOICE_STATE_IDLE) {
        ESP_LOGD(TAG, "Wake word resume skipped: closing=%d running=%d state=%d", g_closing, g_task_running,
                 (int)g_state);
        return;
    }

    ESP_LOGI(TAG, "Resuming wake word standby runtime: %s", reason != NULL ? reason : "unspecified");
    (void)wake_word_resume_standby(reason);
#else
    (void)reason;
#endif
}

void voice_recorder_resume_wake_word(void) {
    voice_recorder_resume_wake_word_with_reason("resume wake word");
}

void voice_recorder_resume_wake_word_for_sleep(void) {
    voice_recorder_resume_wake_word_with_reason("voice sleep standby");
}
