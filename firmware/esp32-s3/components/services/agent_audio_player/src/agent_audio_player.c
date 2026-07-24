#include "agent_audio_player.h"

#include "agent_audio_processing.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_audio.h"

#include <string.h>

#define TAG "AGENT_AUDIO"
#define AGENT_AUDIO_FRAME_MAX 6144
#define AGENT_AUDIO_WORKER_STACK 4096
#define AGENT_AUDIO_WORKER_PRIO 7
#define AGENT_AUDIO_WAIT_MS 20
#define AGENT_AUDIO_PLAY_SAMPLE_RATE 24000
#define AGENT_AUDIO_BYTES_PER_SAMPLE 2
#define AGENT_AUDIO_SILENCE_CHUNK_BYTES 512

#ifdef CONFIG_WATCHER_AGENT_AUDIO_GAIN_Q8
#define AGENT_AUDIO_GAIN_Q8 CONFIG_WATCHER_AGENT_AUDIO_GAIN_Q8
#else
#define AGENT_AUDIO_GAIN_Q8 362
#endif

#ifdef CONFIG_WATCHER_AGENT_AUDIO_TAIL_PAD_MS
#define AGENT_AUDIO_TAIL_PAD_MS CONFIG_WATCHER_AGENT_AUDIO_TAIL_PAD_MS
#else
#define AGENT_AUDIO_TAIL_PAD_MS 100
#endif

#ifdef CONFIG_WATCHER_AGENT_AUDIO_DRAIN_MS
#define AGENT_AUDIO_DRAIN_MS CONFIG_WATCHER_AGENT_AUDIO_DRAIN_MS
#else
#define AGENT_AUDIO_DRAIN_MS 120
#endif

#ifdef CONFIG_WATCHER_AGENT_AUDIO_QUEUE_DEPTH
#define AGENT_AUDIO_QUEUE_DEPTH CONFIG_WATCHER_AGENT_AUDIO_QUEUE_DEPTH
#else
#define AGENT_AUDIO_QUEUE_DEPTH 32
#endif

typedef struct {
    uint8_t data[AGENT_AUDIO_FRAME_MAX];
    uint16_t len;
} agent_audio_slot_t;

static agent_audio_slot_t *s_slots = NULL;
static QueueHandle_t s_free_slots = NULL;
static QueueHandle_t s_pending_slots = NULL;
static TaskHandle_t s_worker = NULL;
static volatile bool s_running = false;
static volatile bool s_stream_done = false;
static volatile bool s_playing = false;
static uint32_t s_enqueue_timeout_count = 0;
static uint32_t s_enqueue_invalid_count = 0;
static agent_audio_player_done_cb_t s_done_cb = NULL;
static void *s_done_ctx = NULL;
static const uint8_t s_silence[AGENT_AUDIO_SILENCE_CHUNK_BYTES] = {0};

static void reset_queues(void) {
    uint8_t index;

    if (s_free_slots == NULL || s_pending_slots == NULL) {
        return;
    }
    xQueueReset(s_free_slots);
    xQueueReset(s_pending_slots);
    for (index = 0; index < AGENT_AUDIO_QUEUE_DEPTH; ++index) {
        (void)xQueueSend(s_free_slots, &index, 0);
    }
}

static void notify_done_once(void) {
    if (s_done_cb != NULL) {
        s_done_cb(s_done_ctx);
    }
}

static void write_tail_guard(void) {
    size_t remaining =
        (size_t)AGENT_AUDIO_PLAY_SAMPLE_RATE * AGENT_AUDIO_BYTES_PER_SAMPLE * AGENT_AUDIO_TAIL_PAD_MS / 1000U;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(s_silence) ? sizeof(s_silence) : remaining;
        if (hal_audio_write(s_silence, (int)chunk) < 0) {
            ESP_LOGW(TAG, "agent audio tail guard write failed len=%u", (unsigned)chunk);
            break;
        }
        remaining -= chunk;
    }

    if (AGENT_AUDIO_DRAIN_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(AGENT_AUDIO_DRAIN_MS));
    }
}

static void worker_task(void *arg) {
    (void)arg;

    while (s_running) {
        uint8_t index = 0;
        if (xQueueReceive(s_pending_slots, &index, pdMS_TO_TICKS(AGENT_AUDIO_WAIT_MS)) == pdTRUE) {
            agent_audio_slot_t *slot = &s_slots[index];

            if (!s_playing) {
                hal_audio_set_playback_mode(true);
                hal_audio_set_sample_rate(AGENT_AUDIO_PLAY_SAMPLE_RATE);
                if (hal_audio_start() != 0) {
                    ESP_LOGW(TAG, "failed to start agent playback path");
                }
                s_playing = true;
            }

            if (slot->len > 0 && hal_audio_write(slot->data, slot->len) < 0) {
                ESP_LOGW(TAG, "agent audio write failed len=%u", (unsigned)slot->len);
            }
            (void)xQueueSend(s_free_slots, &index, 0);
            continue;
        }

        if (s_stream_done && uxQueueMessagesWaiting(s_pending_slots) == 0) {
            if (s_playing) {
                write_tail_guard();
                hal_audio_enter_app_idle();
                s_playing = false;
            }
            s_stream_done = false;
            notify_done_once();
        }
    }

    if (s_playing) {
        hal_audio_enter_app_idle();
        s_playing = false;
    }
    s_worker = NULL;
    vTaskDelete(NULL);
}

esp_err_t agent_audio_player_start(agent_audio_player_done_cb_t done_cb, void *user_ctx) {
    if (s_running) {
        s_done_cb = done_cb;
        s_done_ctx = user_ctx;
        return ESP_OK;
    }
    if (s_slots != NULL || s_free_slots != NULL || s_pending_slots != NULL) {
        agent_audio_player_stop();
        if (s_slots != NULL || s_free_slots != NULL || s_pending_slots != NULL) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    s_slots = heap_caps_calloc(AGENT_AUDIO_QUEUE_DEPTH, sizeof(agent_audio_slot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_slots == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_free_slots = xQueueCreate(AGENT_AUDIO_QUEUE_DEPTH, sizeof(uint8_t));
    s_pending_slots = xQueueCreate(AGENT_AUDIO_QUEUE_DEPTH, sizeof(uint8_t));
    if (s_free_slots == NULL || s_pending_slots == NULL) {
        agent_audio_player_stop();
        return ESP_ERR_NO_MEM;
    }

    s_done_cb = done_cb;
    s_done_ctx = user_ctx;
    s_stream_done = false;
    s_playing = false;
    reset_queues();
    s_running = true;
    if (xTaskCreate(worker_task, "agent_audio", AGENT_AUDIO_WORKER_STACK, NULL, AGENT_AUDIO_WORKER_PRIO, &s_worker) !=
        pdPASS) {
        s_running = false;
        agent_audio_player_stop();
        return ESP_FAIL;
    }
    return ESP_OK;
}

void agent_audio_player_stop(void) {
    s_running = false;
    if (s_worker != NULL) {
        for (int i = 0; i < 100 && s_worker != NULL; ++i) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (s_worker != NULL) {
            ESP_LOGW(TAG, "agent audio worker still exiting; keeping queues allocated");
            return;
        }
    }
    if (s_free_slots != NULL) {
        vQueueDelete(s_free_slots);
        s_free_slots = NULL;
    }
    if (s_pending_slots != NULL) {
        vQueueDelete(s_pending_slots);
        s_pending_slots = NULL;
    }
    if (s_slots != NULL) {
        heap_caps_free(s_slots);
        s_slots = NULL;
    }
    s_done_cb = NULL;
    s_done_ctx = NULL;
    s_stream_done = false;
    s_playing = false;
    s_enqueue_timeout_count = 0;
    s_enqueue_invalid_count = 0;
}

esp_err_t agent_audio_player_enqueue(const uint8_t *pcm, size_t len) {
    uint8_t index = 0;

    if (!s_running || pcm == NULL || len == 0 || len > AGENT_AUDIO_FRAME_MAX) {
        s_enqueue_invalid_count++;
        ESP_LOGW(TAG, "agent audio invalid enqueue len=%u invalid=%u", (unsigned)len,
                 (unsigned)s_enqueue_invalid_count);
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueReceive(s_free_slots, &index, pdMS_TO_TICKS(100)) != pdTRUE) {
        s_enqueue_timeout_count++;
        ESP_LOGW(TAG, "agent audio queue full len=%u timeout=%u", (unsigned)len,
                 (unsigned)s_enqueue_timeout_count);
        return ESP_ERR_TIMEOUT;
    }
    memcpy(s_slots[index].data, pcm, len);
    s_slots[index].len = (uint16_t)len;
    agent_audio_apply_gain_q8(s_slots[index].data, s_slots[index].len, AGENT_AUDIO_GAIN_Q8);
    if (xQueueSend(s_pending_slots, &index, 0) != pdTRUE) {
        (void)xQueueSend(s_free_slots, &index, 0);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void agent_audio_player_mark_stream_done(void) {
    s_stream_done = true;
}

void agent_audio_player_abort(void) {
    if (!s_running) {
        return;
    }
    reset_queues();
    s_stream_done = false;
    if (s_playing) {
        hal_audio_enter_app_idle();
        s_playing = false;
    }
}

bool agent_audio_player_is_playing(void) {
    return s_playing;
}
