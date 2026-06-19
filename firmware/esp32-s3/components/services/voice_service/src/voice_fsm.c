#include "anim_player.h"
#include "behavior_state_service.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_audio.h"
#include "hal_wake_word.h"
#include "sensecap-watcher.h"
#include "voice_service.h"
#include "ws_client.h"
#include <math.h>
#include <string.h>

#define TAG "VOICE"
#define LISTENING_UI_MIN_INTERNAL_FREE_BYTES (28U * 1024U)
#define LISTENING_UI_MIN_INTERNAL_LARGEST_BYTES (8U * 1024U)
#define LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_FREE_BYTES (20U * 1024U)
#define LISTENING_UI_TEXT_ONLY_MIN_INTERNAL_LARGEST_BYTES (6U * 1024U)
#define RECORDING_FREEZE_MIN_INTERNAL_FREE_BYTES (24U * 1024U)
#define RECORDING_FREEZE_MIN_INTERNAL_LARGEST_BYTES (14U * 1024U)

/* ------------------------------------------------------------------ */
/* Private: Wake word context                                         */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_ENABLE_WAKE_WORD
static wake_word_ctx_t *g_wake_word_ctx = NULL;

/* Forward declarations */
static void on_wake_word_detected(const char *wake_word, void *user_data);
static int wake_word_setup(void);
static void wake_word_cleanup(void);
#endif /* CONFIG_ENABLE_WAKE_WORD */

/* ------------------------------------------------------------------ */
/* Private: State and statistics                                       */
/* ------------------------------------------------------------------ */

static voice_state_t g_state = VOICE_STATE_IDLE;
static voice_stats_t g_stats = {0};
static QueueHandle_t g_event_queue = NULL;
static TaskHandle_t g_voice_task_handle = NULL;
static volatile bool g_task_running = false;
static bool g_button_callback_registered = false;

/* Track how recording was triggered (for button behavior) */
static bool g_recording_triggered_by_wake_word = false;
static volatile bool g_remote_recording_desired = false;
static volatile bool g_remote_apply_pending = false;

/* Audio buffer for PCM data (16kHz, 16-bit, 60ms frame = 1920 bytes) */
#define PCM_FRAME_SIZE 1920

static uint8_t g_pcm_buf[PCM_FRAME_SIZE];

#define VOICE_EVENT_QUEUE_LEN 4
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

static void show_cloud_not_ready_state(void) {
    bool connected = ws_client_is_connected() != 0;
    behavior_state_set_with_text(connected ? "processing" : "error", connected ? "Cloud Handshake..." : "Cloud Offline",
                                 0);
}

#if CONFIG_WATCHER_LOG_HEAP_DIAGNOSTICS
static void log_internal_heap_state(const char *stage) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Internal heap @ %s: free=%u KB, largest=%u KB", stage, (unsigned)(free_internal / 1024U),
             (unsigned)(largest_internal / 1024U));
}
#endif

static bool has_listening_ui_headroom(size_t *free_internal_out, size_t *largest_internal_out) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (free_internal_out != NULL) {
        *free_internal_out = free_internal;
    }
    if (largest_internal_out != NULL) {
        *largest_internal_out = largest_internal;
    }

    return free_internal >= LISTENING_UI_MIN_INTERNAL_FREE_BYTES &&
           largest_internal >= LISTENING_UI_MIN_INTERNAL_LARGEST_BYTES;
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

static bool can_freeze_animation_for_recording(size_t *free_internal_out, size_t *largest_internal_out) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (free_internal_out != NULL) {
        *free_internal_out = free_internal;
    }
    if (largest_internal_out != NULL) {
        *largest_internal_out = largest_internal;
    }

    return free_internal >= RECORDING_FREEZE_MIN_INTERNAL_FREE_BYTES &&
           largest_internal >= RECORDING_FREEZE_MIN_INTERNAL_LARGEST_BYTES;
}

static void freeze_current_animation(void) {
    lvgl_port_lock(0);
    emoji_anim_stop();
    lvgl_port_unlock();
}

static void show_listening_ui(void) {
    size_t free_internal = 0;
    size_t largest_internal = 0;
    bool can_freeze = can_freeze_animation_for_recording(&free_internal, &largest_internal);

    if (can_freeze) {
        freeze_current_animation();
    } else {
        ESP_LOGW(TAG,
                 "Low internal heap, keeping current frame to avoid animation stop flush: free=%u KB largest=%u KB",
                 (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U));
    }

    if (has_listening_ui_headroom(&free_internal, &largest_internal)) {
        behavior_state_set_with_resources("listening", "", 0, NULL, "");
        return;
    }

    if (has_text_only_listening_ui_headroom(&free_internal, &largest_internal)) {
        ESP_LOGW(TAG, "Low internal heap, using text-only listening UI: free=%u KB largest=%u KB",
                 (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U));
        behavior_state_set_with_resources("listening", "", 24, "", "");
        return;
    }

    ESP_LOGW(TAG, "Very low internal heap, skipping listening UI refresh: free=%u KB largest=%u KB",
             (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U));
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

/* ------------------------------------------------------------------ */
/* Private: Start recording                                           */
/* ------------------------------------------------------------------ */

static int start_recording(void) {
    if (!ws_client_is_session_ready()) {
        ESP_LOGW(TAG, "start_recording blocked: ws session not ready (connected=%d)", ws_client_is_connected());
        show_cloud_not_ready_state();
        g_stats.error_count++;
        return -1;
    }

    ws_client_abort_tts_playback();

    LOG_INTERNAL_HEAP_STATE("before_recording");
    size_t free_internal = 0;
    size_t largest_internal = 0;
    if (can_freeze_animation_for_recording(&free_internal, &largest_internal)) {
        freeze_current_animation();
    } else {
        ESP_LOGW(TAG, "Low internal heap, skipping animation freeze before recording: free=%u KB largest=%u KB",
                 (unsigned)(free_internal / 1024U), (unsigned)(largest_internal / 1024U));
    }

    hal_audio_set_playback_mode(false);
    hal_audio_set_sample_rate(16000);
    ESP_LOGI(TAG, "start_recording: calling hal_audio_start()");
    if (hal_audio_start() != 0) {
        ESP_LOGE(TAG, "start_recording: hal_audio_start failed");
        g_stats.error_count++;
        return -1;
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* Stop wake word detection during recording to prevent AFE empty warnings */
    if (g_wake_word_ctx != NULL) {
        hal_wake_word_stop(g_wake_word_ctx);
        /* Wait for detection task to finish current fetch */
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Initialize VAD for wake word mode */
    if (g_recording_triggered_by_wake_word) {
        vad_reset();
        ESP_LOGI(TAG, "VAD enabled: silence_timeout=%dms, rms_threshold=%d, min_speech=%dms",
                 CONFIG_VAD_SILENCE_TIMEOUT_MS, CONFIG_VAD_RMS_THRESHOLD, CONFIG_VAD_MIN_SPEECH_MS);
    }
#endif

    g_state = VOICE_STATE_RECORDING;
    LOG_INTERNAL_HEAP_STATE("after_recording");
    ESP_LOGI(TAG, "start_recording: state -> RECORDING");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Private: Stop recording                                            */
/* ------------------------------------------------------------------ */

static int stop_recording(void) {
    /* In wake word mode, keep audio running for continuous detection */
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (g_wake_word_ctx != NULL) {
        hal_wake_word_start(g_wake_word_ctx);
    } else {
        hal_audio_stop();
    }
    /* Wake word mode: audio stays running for next detection */
#else
    hal_audio_stop();
#endif

    /* Skip end marker if the cloud session is already gone. */
    if (ws_client_is_session_ready()) {
        if (ws_send_audio_end() != 0) {
            g_stats.error_count++;
            /* Still transition to idle */
        }
    } else {
        ESP_LOGW(TAG, "Skipping audio end marker: ws session not ready (connected=%d)", ws_client_is_connected());
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* Disable VAD when stopping */
    vad_disable();
#endif

    g_state = VOICE_STATE_IDLE;
    g_stats.record_count++;
    g_recording_triggered_by_wake_word = false; /* Reset trigger flag */
    return 0;
}

void voice_recorder_suspend_cloud_audio(void) {
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
}

static esp_err_t submit_remote_recording_target(bool should_record) {
    voice_event_t event = VOICE_EVENT_REMOTE_APPLY;

    if (!g_task_running || g_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    g_remote_recording_desired = should_record;
    if (g_remote_apply_pending) {
        return ESP_OK;
    }

    g_remote_apply_pending = true;
    if (xQueueSend(g_event_queue, &event, 0) != pdTRUE) {
        g_remote_apply_pending = false;
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t voice_recorder_request_open(void) {
    return submit_remote_recording_target(true);
}

esp_err_t voice_recorder_request_close(void) {
    return submit_remote_recording_target(false);
}

static void handle_short_press_toggle(void) {
    if (g_state == VOICE_STATE_IDLE) {
        if (!ws_client_is_session_ready()) {
            ESP_LOGW(TAG, "Short press ignored: ws session not ready (connected=%d)", ws_client_is_connected());
            show_cloud_not_ready_state();
            return;
        }

        ESP_LOGI(TAG, "Short press - starting recording");
        g_recording_triggered_by_wake_word = false;
        voice_recorder_process_event(VOICE_EVENT_BUTTON_SHORT_CLICK);
        if (g_state == VOICE_STATE_RECORDING) {
            show_listening_ui();
        } else {
            show_cloud_not_ready_state();
        }
        return;
    }

    if (g_state == VOICE_STATE_RECORDING) {
        ESP_LOGI(TAG, "Short press - stopping recording");
        voice_recorder_process_event(VOICE_EVENT_BUTTON_SHORT_CLICK);
        behavior_state_set_with_text("processing", "", 0);
    }
}

static void handle_remote_apply(void) {
    bool should_record = g_remote_recording_desired;

    g_remote_apply_pending = false;
    if (should_record && g_state == VOICE_STATE_RECORDING) {
        ESP_LOGI(TAG, "Remote microphone target recording ignored: already recording");
        return;
    }
    if (!should_record && g_state == VOICE_STATE_IDLE) {
        ESP_LOGI(TAG, "Remote microphone target idle ignored: already idle");
        return;
    }

    if (should_record) {
        ESP_LOGI(TAG, "Remote microphone target recording - starting recording");
        g_recording_triggered_by_wake_word = false;
        voice_recorder_process_event(VOICE_EVENT_REMOTE_APPLY);
        if (g_state == VOICE_STATE_RECORDING) {
            show_listening_ui();
        } else {
            show_cloud_not_ready_state();
        }
    } else {
        ESP_LOGI(TAG, "Remote microphone target idle - stopping recording");
        voice_recorder_process_event(VOICE_EVENT_REMOTE_APPLY);
        behavior_state_set_with_text("processing", "", 0);
    }
}

/* ------------------------------------------------------------------ */
/* Public: Process event                                              */
/* ------------------------------------------------------------------ */

void voice_recorder_process_event(voice_event_t event) {
    switch (g_state) {
    case VOICE_STATE_IDLE:
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK || event == VOICE_EVENT_WAKE_WORD ||
            (event == VOICE_EVENT_REMOTE_APPLY && g_remote_recording_desired)) {
#ifdef CONFIG_ENABLE_WAKE_WORD
            if (event == VOICE_EVENT_WAKE_WORD) {
                ESP_LOGI(TAG, "Wake word triggered recording");
            }
#endif
            start_recording();
        }
        break;

    case VOICE_STATE_RECORDING:
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK || event == VOICE_EVENT_TIMEOUT ||
            (event == VOICE_EVENT_REMOTE_APPLY && !g_remote_recording_desired)) {
            stop_recording();
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public: Process tick (read, send)                                   */
/* ------------------------------------------------------------------ */

int voice_recorder_tick(void) {
    /* Always read audio when wake word detection is enabled */
    int pcm_len = 0;

#ifdef CONFIG_ENABLE_WAKE_WORD
    static uint32_t wake_idle_frame_count = 0;
    static bool wake_audio_first_read_logged = false;

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

    int16_t *samples = (int16_t *)g_pcm_buf;
    size_t num_samples = pcm_len / 2; /* 16-bit samples */

    /* Feed wake word detector when idle (local detection, no network) */
    if (g_state == VOICE_STATE_IDLE && g_wake_word_ctx != NULL) {
        hal_wake_word_feed(g_wake_word_ctx, samples, num_samples);
        /* Yield after feed so higher-priority detection task can call fetch()
         * before we loop back. Prevents AFE FEED ring buffer overflow. */
        taskYIELD();

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
                     (unsigned long)wake_idle_frame_count, idle_rms, idle_peak, idle_zero_count, (unsigned)num_samples,
                     (unsigned)hal_wake_word_get_feed_size(g_wake_word_ctx));
        }
    }

    /* Only send to WebSocket when recording */
    if (g_state != VOICE_STATE_RECORDING) {
        return 0;
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

    int16_t *samples = (int16_t *)g_pcm_buf;
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
                 "audio frame=%d rms=%d peak=%d zeros=%d/%d q{p=%u hi=%u in=%lu out=%lu drop=%lu delay_us=%lu} "
                 "send_us=%lu/%lu packet=%u",
                 g_stats.encode_count + 1, rms, peak, zero_count, sample_count,
                 (unsigned int)queue_stats.pending_frames, (unsigned int)queue_stats.high_watermark,
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
        behavior_state_set_with_text("processing", "", 0);
        return 0; /* Recording stopped, don't send this frame */
    }
#endif

    /* Enqueue PCM for asynchronous WebSocket upload */
    if (ws_send_audio(g_pcm_buf, pcm_len) != 0) {
        if (!ws_client_is_session_ready()) {
            ESP_LOGW(TAG, "Cloud session lost during recording (connected=%d)", ws_client_is_connected());
            show_cloud_not_ready_state();
        }
        g_stats.error_count++;
        /* Only log every 10 errors to avoid flooding */
        if (g_stats.error_count % 10 == 1) {
            ESP_LOGE(TAG, "WS send audio failed (count: %d)", g_stats.error_count);
        }
        return -1;
    }

    g_stats.encode_count++;
    return 1; /* One frame sent */
}

/* ------------------------------------------------------------------ */
/* Private: Button callback                                           */
/* ------------------------------------------------------------------ */

static void button_single_click_callback(void) {
    voice_event_t event = VOICE_EVENT_BUTTON_SHORT_CLICK;

    if (!g_task_running || g_event_queue == NULL) {
        ESP_LOGI(TAG, "Ignoring button click: recorder task not running");
        return;
    }

    if (xQueueSend(g_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Dropping button click: voice event queue full");
        return;
    }

    ESP_LOGI(TAG, "Queued button single-click event");
}

static void voice_process_pending_events(void) {
    voice_event_t event = VOICE_EVENT_NONE;

    while (g_event_queue != NULL && xQueueReceive(g_event_queue, &event, 0) == pdTRUE) {
        if (event == VOICE_EVENT_BUTTON_SHORT_CLICK) {
            handle_short_press_toggle();
        } else if (event == VOICE_EVENT_REMOTE_APPLY) {
            handle_remote_apply();
        } else {
            voice_recorder_process_event(event);
        }
    }
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
    ESP_LOGI(TAG, "Voice recorder task started");

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
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word);
    g_recording_triggered_by_wake_word = true; /* Mark as wake word triggered */
    show_listening_ui();
    voice_recorder_process_event(VOICE_EVENT_WAKE_WORD);
}

static int wake_word_setup(void) {
    wake_word_config_t config = {
        .model_path = NULL, /* Use default */
        .callback = on_wake_word_detected,
        .user_data = NULL,
    };

#ifdef CONFIG_WAKE_WORD_CUSTOM
    config.wake_word_phrase = CONFIG_CUSTOM_WAKE_WORD_PHRASE;
    config.detection_threshold = (float)CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
#endif

    g_wake_word_ctx = hal_wake_word_init(&config);
    if (g_wake_word_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize wake word detector");
        return -1;
    }

    hal_wake_word_start(g_wake_word_ctx);
    ESP_LOGI(TAG, "Wake word detection enabled");
    return 0;
}

static void wake_word_cleanup(void) {
    if (g_wake_word_ctx != NULL) {
        hal_wake_word_stop(g_wake_word_ctx);
        /* Wait for detection task to finish current fetch */
        vTaskDelay(pdMS_TO_TICKS(50));
        hal_wake_word_deinit(g_wake_word_ctx);
        g_wake_word_ctx = NULL;
    }
}
#endif /* CONFIG_ENABLE_WAKE_WORD */

/* ------------------------------------------------------------------ */
/* Public: Start voice recorder system (with button and task)         */
/* ------------------------------------------------------------------ */

int voice_recorder_start(void) {
    if (g_task_running && g_voice_task_handle != NULL) {
        ESP_LOGI(TAG, "Voice recorder already running");
        return 0;
    }

    if (g_voice_task_handle != NULL && !voice_wait_for_task_exit(VOICE_TASK_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "Voice recorder task is still stopping");
        return -1;
    }

    if (g_event_queue == NULL) {
        g_event_queue = xQueueCreate(VOICE_EVENT_QUEUE_LEN, sizeof(voice_event_t));
        if (g_event_queue == NULL) {
            ESP_LOGE(TAG, "Voice event queue create failed");
            return -1;
        }
    } else {
        xQueueReset(g_event_queue);
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    if (hal_wake_word_is_supported()) {
        hal_audio_set_playback_mode(false);
        hal_audio_set_sample_rate(16000);
        if (hal_audio_start() != 0) {
            ESP_LOGE(TAG, "Failed to start audio capture for wake word detection");
            return -1;
        }
        if (wake_word_setup() != 0) {
            hal_audio_stop();
            return -1;
        }
    } else {
        ESP_LOGW(TAG, "Wake word detection requested but hardware support is unavailable");
    }
#endif

    if (!g_button_callback_registered) {
        esp_err_t btn_ret = bsp_set_btn_single_click_cb(button_single_click_callback);
        if (btn_ret == ESP_OK) {
            g_button_callback_registered = true;
            ESP_LOGI(TAG, "Voice button single-click handler registered");
        } else {
            ESP_LOGW(TAG, "Voice button unavailable, starting recorder without button input: %s",
                     esp_err_to_name(btn_ret));
        }
    }

    /* Start voice recorder task */
    g_task_running = true;
    BaseType_t ret;
#ifdef CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    ret = xTaskCreateWithCaps(voice_recorder_task, "voice_task", CONFIG_VOICE_TASK_STACK_SIZE, NULL, 5,
                              &g_voice_task_handle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create voice task in PSRAM, retrying internal RAM");
        ret =
            xTaskCreate(voice_recorder_task, "voice_task", CONFIG_VOICE_TASK_STACK_SIZE, NULL, 5, &g_voice_task_handle);
    }
#else
    ret = xTaskCreate(voice_recorder_task, "voice_task", CONFIG_VOICE_TASK_STACK_SIZE, NULL, 5, &g_voice_task_handle);
#endif

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
#ifdef CONFIG_ENABLE_WAKE_WORD
        wake_word_cleanup();
        hal_audio_stop();
#endif
        g_task_running = false;
        return -1;
    }

    ESP_LOGI(TAG, "Voice recorder started");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public: Stop voice recorder system                                  */
/* ------------------------------------------------------------------ */

void voice_recorder_stop(void) {
    bool had_runtime = g_task_running || g_voice_task_handle != NULL;

    g_task_running = false;

    if (g_voice_task_handle != NULL && !voice_wait_for_task_exit(VOICE_TASK_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "Voice recorder task did not exit within %u ms", (unsigned)VOICE_TASK_EXIT_WAIT_MS);
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* Cleanup wake word detector */
    wake_word_cleanup();
#endif

    if (g_event_queue != NULL) {
        xQueueReset(g_event_queue);
    }

    if (had_runtime) {
        ESP_LOGI(TAG, "Voice recorder stopped");
    } else {
        ESP_LOGI(TAG, "Voice recorder already stopped");
    }
}

/* ------------------------------------------------------------------ */
/* Public: Pause wake word detection before TTS                        */
/* ------------------------------------------------------------------ */

void voice_recorder_pause_wake_word(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (g_wake_word_ctx != NULL) {
        ESP_LOGI(TAG, "Pausing wake word detection for TTS");
        hal_wake_word_stop(g_wake_word_ctx);
        /* Wait for detection task to finish current fetch */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Public: Resume wake word detection after TTS                       */
/* ------------------------------------------------------------------ */

void voice_recorder_resume_wake_word(void) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (g_wake_word_ctx != NULL) {
        ESP_LOGI(TAG, "Resuming wake word detection");
        hal_wake_word_start(g_wake_word_ctx);
    }
#endif
}
