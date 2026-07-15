/**
 * @file anim_player.c
 * @brief SD-backed animation player with ring-buffered stream playback.
 */

#include "anim_fault_injection.h"
#include "anim_frame_pool_core.h"
#include "anim_player_event_queue.h"
#include "anim_player_internal.h"
#include "anim_player_lifecycle.h"
#include "anim_player_private.h"
#include "anim_prepare_watchdog.h"
#include "anim_rgb565_fade.h"
#include "anim_storage.h"
#include "anim_worker_scheduler.h"
#include "animation_playback_policy.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_display.h"
#include "sensecap-watcher.h"

#include <limits.h>
#include <string.h>

#define TAG "ANIM_PLAYER"
#define ANIM_SOURCE_FRAME_SIZE 206
#define ANIM_SOURCE_FRAME_PIVOT (ANIM_SOURCE_FRAME_SIZE / 2)
#define ANIM_DISPLAY_ZOOM_2X (LV_IMG_ZOOM_NONE * 2)
#define ANIM_DIRECT_LCD_SCALE 2U
#define ANIM_DIRECT_LCD_WIDTH (ANIM_SOURCE_FRAME_SIZE * ANIM_DIRECT_LCD_SCALE)
#define ANIM_DIRECT_LCD_HEIGHT (ANIM_SOURCE_FRAME_SIZE * ANIM_DIRECT_LCD_SCALE)
#define ANIM_DIRECT_LCD_STRIP_SRC_ROWS 2U
#define ANIM_DIRECT_LCD_STRIP_DST_ROWS (ANIM_DIRECT_LCD_STRIP_SRC_ROWS * ANIM_DIRECT_LCD_SCALE)
#define ANIM_DIRECT_LCD_STRIP_BUFFERS 1U
#define ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS 50U
#define ANIM_TIMER_TICK_MS 5U
#define ANIM_WORKER_IDLE_MS 1U
#define ANIM_WORKER_TASK_PRIORITY 6
#define ANIM_WORKER_EXIT_WAIT_MS 1000U
#define ANIM_EVENT_DISPATCH_EXIT_WAIT_MS 1000U
#define ANIM_PERF_LOG_FRAME_INTERVAL 10U
#define ANIM_INVALID_SLOT_FRAME (-1)
#define ANIM_FRAME_LOAD_FAILURE_LIMIT 3U
#define ANIM_DIRECT_LCD_PROTECTED_BAND_MAX 2U
#define ANIM_FRAME_POOL_CAPACITY (2U * WATCHER_ANIM_RING_FRAMES)

#ifdef CONFIG_WATCHER_ANIM_ACTIVE_LOW_WATER_FRAMES
#define ANIM_ACTIVE_LOW_WATER_FRAMES CONFIG_WATCHER_ANIM_ACTIVE_LOW_WATER_FRAMES
#else
#define ANIM_ACTIVE_LOW_WATER_FRAMES 1U
#endif

#ifdef CONFIG_WATCHER_ANIM_STAGING_PREFILL_FRAMES
#define ANIM_STAGING_PREFILL_FRAMES CONFIG_WATCHER_ANIM_STAGING_PREFILL_FRAMES
#else
#define ANIM_STAGING_PREFILL_FRAMES 2U
#endif

#ifdef CONFIG_WATCHER_ANIM_PREPARE_STALL_TIMEOUT_MS
#define ANIM_PREPARE_STALL_TIMEOUT_MS CONFIG_WATCHER_ANIM_PREPARE_STALL_TIMEOUT_MS
#else
#define ANIM_PREPARE_STALL_TIMEOUT_MS 2000U
#endif

#define ANIM_FADE_ALPHA_MAX 255U

typedef enum {
    ANIM_PLAYER_IDLE = 0,
    ANIM_PLAYER_SWITCH_PREPARING,
    ANIM_PLAYER_PLAYING,
} anim_player_state_t;

typedef enum {
    ANIM_LOAD_TARGET_ACTIVE = 0,
    ANIM_LOAD_TARGET_PENDING,
    ANIM_LOAD_TARGET_PREFETCH,
} anim_load_target_t;

typedef enum {
    ANIM_PLAYBACK_OPEN_OK = 0,
    ANIM_PLAYBACK_OPEN_STREAM_FAILED = -1,
    ANIM_PLAYBACK_OPEN_NO_MEMORY = -2,
    ANIM_PLAYBACK_OPEN_INVALID_RESOURCE = -3,
    ANIM_PLAYBACK_OPEN_PACK_CORRUPT = -4,
    ANIM_PLAYBACK_OPEN_SD_READ_FAILED = -5,
} anim_playback_open_result_t;

typedef struct {
    bool in_use;
    emoji_anim_type_t type;
    uint32_t generation_id;
    animation_ticket_t ticket;
    animation_playback_mode_t playback_mode;
    uint16_t repeat_count;
    uint16_t fade_in_ms;
    anim_stream_t stream;
    anim_frame_buffer_t slots[WATCHER_ANIM_RING_FRAMES];
    uint16_t slot_pool_index[WATCHER_ANIM_RING_FRAMES];
    int slot_frame_index[WATCHER_ANIM_RING_FRAMES];
    int slot_count;
    int current_slot;
    int current_frame_index;
    int buffered_frames;
    int next_frame_to_load;
    int next_slot_to_load;
    uint32_t load_failure_count;
    uint32_t displayed_frames;
    uint32_t stats_frames;
    uint32_t underrun_count;
    uint32_t stats_underruns;
    int64_t started_us;
    int64_t stats_started_us;
    int64_t stats_late_total_us;
    int64_t stats_late_max_us;
    int64_t stats_read_total_us;
    int64_t stats_read_max_us;
    int64_t stats_draw_total_us;
    int64_t stats_draw_max_us;
    uint32_t stats_read_frames;
    uint32_t stats_draw_frames;
    uint32_t stats_draw_failures;
} anim_playback_t;

typedef struct {
    bool valid;
    anim_load_target_t target;
    emoji_anim_type_t type;
    uint32_t generation_id;
    animation_ticket_t ticket;
    int frame_index;
    int slot_index;
} anim_load_job_t;

typedef struct {
    int y1;
    int y2;
} anim_protected_band_t;

static lv_obj_t *g_front_img = NULL;
static lv_obj_t *g_back_img = NULL;
static lv_timer_t *g_frame_timer = NULL;
static lv_timer_t *g_service_timer = NULL;
static TaskHandle_t g_worker_task = NULL;
static SemaphoreHandle_t g_player_mutex = NULL;
static SemaphoreHandle_t g_worker_exit_sem = NULL;

static anim_player_state_t g_state = ANIM_PLAYER_IDLE;
static anim_playback_t g_active_playback = {0};
static anim_playback_t g_pending_playback = {0};
static anim_playback_t g_prefetch_playback = {0};
static anim_frame_pool_core_t g_frame_pool_core = {0};
static anim_frame_buffer_t g_frame_pool_buffers[ANIM_FRAME_POOL_CAPACITY];
static bool g_frame_pool_initialized = false;
static emoji_anim_type_t g_current_type = EMOJI_ANIM_NONE;
static emoji_anim_type_t g_requested_type = EMOJI_ANIM_NONE;
static uint32_t g_latest_generation = 0;
static int64_t g_next_frame_deadline_us = 0;
static esp_lcd_panel_handle_t g_direct_panel = NULL;
static uint16_t *g_direct_strip = NULL;
static size_t g_direct_strip_pixels = 0;
static bool g_direct_lcd_ready = false;
static bool g_worker_shutdown_requested = false;
static int64_t g_fade_start_us = 0;
static uint16_t g_fade_duration_ms = 0U;
static anim_player_state_t g_state_before_prepare = ANIM_PLAYER_IDLE;
static anim_prepare_watchdog_t g_prepare_watchdog = {0};
static bool g_safe_error_pending = false;
static bool g_safe_error_visible = false;
static bool g_frame_timer_update_pending = false;
static bool g_frame_timer_should_run = false;
static anim_player_lifecycle_t g_lifecycle = {0};
static anim_player_event_queue_t g_ticket_event_queue = {0};
static anim_player_ticket_event_sink_t g_ticket_event_sink = NULL;
static void *g_ticket_event_sink_context = NULL;
static bool g_ticket_event_dispatching = false;
static portMUX_TYPE g_protected_region_lock = portMUX_INITIALIZER_UNLOCKED;
static bool g_protected_region_active = false;
static int g_protected_region_x1 = 0;
static int g_protected_region_y1 = 0;
static int g_protected_region_x2 = 0;
static int g_protected_region_y2 = 0;

static void anim_worker_task(void *param);
static void anim_frame_timer_cb(lv_timer_t *timer);
static void anim_service_timer_cb(lv_timer_t *timer);
static void restore_after_prepare_failure_locked(void);
static bool poll_prepare_watchdog_locked(void);

static const char *player_event_name(anim_player_event_type_t type) {
    switch (type) {
    case ANIM_PLAYER_EVENT_PREPARING:
        return "PREPARING";
    case ANIM_PLAYER_EVENT_COMMITTED:
        return "COMMITTED";
    case ANIM_PLAYER_EVENT_CYCLE_COMPLETED:
        return "CYCLE_COMPLETED";
    case ANIM_PLAYER_EVENT_COMPLETED:
        return "COMPLETED";
    case ANIM_PLAYER_EVENT_FAILED:
        return "FAILED";
    case ANIM_PLAYER_EVENT_NONE:
    default:
        return "NONE";
    }
}

static const char *player_failure_name(anim_player_failure_t failure) {
    switch (failure) {
    case ANIM_PLAYER_FAILURE_STREAM_OPEN:
        return "STREAM_OPEN";
    case ANIM_PLAYER_FAILURE_FRAME_READ:
        return "FRAME_READ";
    case ANIM_PLAYER_FAILURE_PREPARE_STALLED:
        return "PREPARE_STALLED";
    case ANIM_PLAYER_FAILURE_NO_MEMORY:
        return "NO_MEMORY";
    case ANIM_PLAYER_FAILURE_INVALID_RESOURCE:
        return "INVALID_RESOURCE";
    case ANIM_PLAYER_FAILURE_PACK_CORRUPT:
        return "PACK_CORRUPT";
    case ANIM_PLAYER_FAILURE_RENDER:
        return "RENDER";
    case ANIM_PLAYER_FAILURE_SERVICE_STOPPED:
        return "SERVICE_STOPPED";
    case ANIM_PLAYER_FAILURE_NONE:
    default:
        return "NONE";
    }
}

static void log_lifecycle_events(const anim_player_lifecycle_events_t *events) {
    if (events == NULL) {
        return;
    }
    for (size_t index = 0; index < events->count; ++index) {
        const anim_player_lifecycle_event_t *event = &events->items[index];
        ESP_LOGI(TAG, "Player lifecycle event=%s ticket=%lu generation=%lu animation=%s failure=%s",
                 player_event_name(event->type), (unsigned long)event->ticket, (unsigned long)event->generation,
                 emoji_type_name((emoji_anim_type_t)event->animation_type), player_failure_name(event->failure));
    }
}

static animation_event_type_t public_event_type(anim_player_event_type_t type) {
    switch (type) {
    case ANIM_PLAYER_EVENT_PREPARING:
        return ANIMATION_EVENT_PREPARING;
    case ANIM_PLAYER_EVENT_COMMITTED:
        return ANIMATION_EVENT_COMMITTED;
    case ANIM_PLAYER_EVENT_CYCLE_COMPLETED:
        return ANIMATION_EVENT_CYCLE_COMPLETED;
    case ANIM_PLAYER_EVENT_COMPLETED:
        return ANIMATION_EVENT_COMPLETED;
    case ANIM_PLAYER_EVENT_FAILED:
    case ANIM_PLAYER_EVENT_NONE:
    default:
        return ANIMATION_EVENT_FAILED;
    }
}

static animation_failure_t public_failure(anim_player_failure_t failure) {
    switch (failure) {
    case ANIM_PLAYER_FAILURE_STREAM_OPEN:
        return ANIMATION_FAILURE_SD_OPEN_FAILED;
    case ANIM_PLAYER_FAILURE_FRAME_READ:
        return ANIMATION_FAILURE_SD_READ_FAILED;
    case ANIM_PLAYER_FAILURE_PREPARE_STALLED:
        return ANIMATION_FAILURE_PREPARE_STALLED;
    case ANIM_PLAYER_FAILURE_NO_MEMORY:
        return ANIMATION_FAILURE_NO_MEMORY;
    case ANIM_PLAYER_FAILURE_INVALID_RESOURCE:
        return ANIMATION_FAILURE_INVALID_RESOURCE;
    case ANIM_PLAYER_FAILURE_PACK_CORRUPT:
        return ANIMATION_FAILURE_PACK_CORRUPT;
    case ANIM_PLAYER_FAILURE_RENDER:
        return ANIMATION_FAILURE_RENDER_FAILED;
    case ANIM_PLAYER_FAILURE_SERVICE_STOPPED:
        return ANIMATION_FAILURE_SERVICE_STOPPED;
    case ANIM_PLAYER_FAILURE_NONE:
    default:
        return ANIMATION_FAILURE_NONE;
    }
}

static anim_player_failure_t player_failure_from_storage_result(int storage_result, bool opening) {
    switch (storage_result) {
    case ANIM_STORAGE_NO_MEMORY:
        return ANIM_PLAYER_FAILURE_NO_MEMORY;
    case ANIM_STORAGE_INVALID_RESOURCE:
        return ANIM_PLAYER_FAILURE_INVALID_RESOURCE;
    case ANIM_STORAGE_PACK_CORRUPT:
        return ANIM_PLAYER_FAILURE_PACK_CORRUPT;
    case ANIM_STORAGE_SD_OPEN_FAILED:
        return ANIM_PLAYER_FAILURE_STREAM_OPEN;
    case ANIM_STORAGE_SD_READ_FAILED:
        return ANIM_PLAYER_FAILURE_FRAME_READ;
    case ANIM_STORAGE_OK:
    default:
        return opening ? ANIM_PLAYER_FAILURE_STREAM_OPEN : ANIM_PLAYER_FAILURE_FRAME_READ;
    }
}

/* Caller holds g_player_mutex. */
static bool enqueue_lifecycle_events_locked(const anim_player_lifecycle_events_t *events) {
    anim_player_ticket_event_t ticket_events[ANIM_PLAYER_LIFECYCLE_MAX_EVENTS];
    size_t ticket_event_count = 0U;

    if (events == NULL) {
        return true;
    }
    log_lifecycle_events(events);
    for (size_t index = 0U; index < events->count; ++index) {
        const anim_player_lifecycle_event_t *event = &events->items[index];
        if (event->ticket == ANIMATION_TICKET_INVALID) {
            continue;
        }
        ticket_events[ticket_event_count++] = (anim_player_ticket_event_t){
            .ticket = event->ticket,
            .type = public_event_type(event->type),
            .failure = public_failure(event->failure),
            .animation_type = (emoji_anim_type_t)event->animation_type,
            .generation = event->generation,
            .completed_cycles = event->completed_cycles,
        };
    }
    if (ticket_event_count == 0U) {
        return true;
    }
    if (!anim_player_event_queue_push_batch(&g_ticket_event_queue, ticket_events, ticket_event_count)) {
        ESP_LOGE(TAG, "Player event queue overflow: ticket=%lu events=%u queued=%u overflow=%lu",
                 (unsigned long)ticket_events[0].ticket, (unsigned)ticket_event_count,
                 (unsigned)anim_player_event_queue_count(&g_ticket_event_queue),
                 (unsigned long)g_ticket_event_queue.overflow_count);
        return false;
    }
    return true;
}

static int min_int(int lhs, int rhs) {
    return lhs < rhs ? lhs : rhs;
}

static int max_int(int lhs, int rhs) {
    return lhs > rhs ? lhs : rhs;
}

static int effective_delay_ms(anim_playback_t *playback, int frame_index) {
    int delay_ms = anim_stream_get_frame_delay_ms(&playback->stream, frame_index);
    return delay_ms > 0 ? delay_ms : EMOJI_ANIM_INTERVAL_MS;
}

static int playback_loop_duration_ms(anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use || playback->stream.frame_count <= 0) {
        return 0;
    }

    int64_t total_ms = 0;
    for (int index = 0; index < playback->stream.frame_count; ++index) {
        total_ms += effective_delay_ms(playback, index);
    }
    return total_ms > 0 && total_ms <= INT_MAX ? (int)total_ms : 0;
}

static bool anim_type_should_loop(emoji_anim_type_t type, const anim_stream_t *stream) {
    if (type == EMOJI_ANIM_RECHARGE) {
        return false;
    }

    return stream != NULL && stream->info.loop;
}

static bool playback_should_cycle(const anim_playback_t *playback) {
    return playback != NULL && playback->in_use &&
           animation_playback_should_cycle(playback->playback_mode, playback->repeat_count,
                                           anim_type_should_loop(playback->type, &playback->stream));
}

static void playback_reset_perf_stats(anim_playback_t *playback, int64_t now_us) {
    if (playback == NULL) {
        return;
    }

    playback->displayed_frames = 1;
    playback->stats_frames = 0;
    playback->underrun_count = 0;
    playback->stats_underruns = 0;
    playback->started_us = now_us;
    playback->stats_started_us = now_us;
    playback->stats_late_total_us = 0;
    playback->stats_late_max_us = 0;
    playback->stats_read_total_us = 0;
    playback->stats_read_max_us = 0;
    playback->stats_draw_total_us = 0;
    playback->stats_draw_max_us = 0;
    playback->stats_read_frames = 0;
    playback->stats_draw_frames = 0;
    playback->stats_draw_failures = 0;
}

static void playback_note_underrun(anim_playback_t *playback) {
    if (playback == NULL) {
        return;
    }

    playback->underrun_count++;
    playback->stats_underruns++;
}

static void playback_note_displayed_frame(anim_playback_t *playback, int64_t now_us, int64_t late_us) {
    if (playback == NULL) {
        return;
    }

    if (late_us < 0) {
        late_us = 0;
    }

    playback->displayed_frames++;
    playback->stats_frames++;
    playback->stats_late_total_us += late_us;
    if (late_us > playback->stats_late_max_us) {
        playback->stats_late_max_us = late_us;
    }

#ifdef CONFIG_WATCHER_ANIM_DEBUG_PERF
    if (playback->stats_frames >= ANIM_PERF_LOG_FRAME_INTERVAL) {
        int64_t elapsed_us = now_us - playback->stats_started_us;
        uint32_t fps_x100 =
            elapsed_us > 0 ? (uint32_t)(((int64_t)playback->stats_frames * 100000000LL) / elapsed_us) : 0U;
        uint32_t avg_late_ms = playback->stats_frames > 0
                                   ? (uint32_t)(playback->stats_late_total_us / playback->stats_frames / 1000LL)
                                   : 0U;
        uint32_t max_late_ms = (uint32_t)(playback->stats_late_max_us / 1000LL);

        uint32_t avg_read_ms = playback->stats_read_frames > 0
                                   ? (uint32_t)(playback->stats_read_total_us / playback->stats_read_frames / 1000LL)
                                   : 0U;
        uint32_t max_read_ms = (uint32_t)(playback->stats_read_max_us / 1000LL);
        uint32_t avg_draw_ms = playback->stats_draw_frames > 0
                                   ? (uint32_t)(playback->stats_draw_total_us / playback->stats_draw_frames / 1000LL)
                                   : 0U;
        uint32_t max_draw_ms = (uint32_t)(playback->stats_draw_max_us / 1000LL);

        ESP_LOGI(TAG,
                 "Playback perf %s: fps=%lu.%02lu frames=%lu buffered=%d underruns=%lu late_avg=%lums "
                 "late_max=%lums read_avg=%lums read_max=%lums read_frames=%lu draw_avg=%lums draw_max=%lums "
                 "draw_frames=%lu draw_failures=%lu direct=%d",
                 emoji_type_name(playback->type), (unsigned long)(fps_x100 / 100U), (unsigned long)(fps_x100 % 100U),
                 (unsigned long)playback->stats_frames, playback->buffered_frames,
                 (unsigned long)playback->stats_underruns, (unsigned long)avg_late_ms, (unsigned long)max_late_ms,
                 (unsigned long)avg_read_ms, (unsigned long)max_read_ms, (unsigned long)playback->stats_read_frames,
                 (unsigned long)avg_draw_ms, (unsigned long)max_draw_ms, (unsigned long)playback->stats_draw_frames,
                 (unsigned long)playback->stats_draw_failures, g_direct_lcd_ready ? 1 : 0);

        playback->stats_frames = 0;
        playback->stats_underruns = 0;
        playback->stats_started_us = now_us;
        playback->stats_late_total_us = 0;
        playback->stats_late_max_us = 0;
        playback->stats_read_total_us = 0;
        playback->stats_read_max_us = 0;
        playback->stats_draw_total_us = 0;
        playback->stats_draw_max_us = 0;
        playback->stats_read_frames = 0;
        playback->stats_draw_frames = 0;
        playback->stats_draw_failures = 0;
    }
#else
    (void)now_us;
#endif
}

static void playback_note_read_frame(anim_playback_t *playback, int64_t read_us) {
    if (playback == NULL || read_us < 0) {
        return;
    }

    playback->stats_read_frames++;
    playback->stats_read_total_us += read_us;
    if (read_us > playback->stats_read_max_us) {
        playback->stats_read_max_us = read_us;
    }
}

static void playback_note_draw_frame(anim_playback_t *playback, int64_t draw_us) {
    if (playback == NULL || draw_us < 0) {
        return;
    }

    playback->stats_draw_frames++;
    playback->stats_draw_total_us += draw_us;
    if (draw_us > playback->stats_draw_max_us) {
        playback->stats_draw_max_us = draw_us;
    }
}

static void playback_note_draw_failure(anim_playback_t *playback) {
    if (playback == NULL) {
        return;
    }

    playback->stats_draw_failures++;
}

static bool ensure_direct_lcd_ready(void) {
    if (g_direct_lcd_ready) {
        return true;
    }

    g_direct_panel = hal_display_get_panel_handle();
    if (g_direct_panel == NULL) {
        g_direct_panel = bsp_lcd_get_panel_handle();
    }
    if (g_direct_panel == NULL) {
        return false;
    }

    g_direct_strip_pixels = ANIM_DIRECT_LCD_WIDTH * ANIM_DIRECT_LCD_STRIP_DST_ROWS;
    size_t bytes = g_direct_strip_pixels * sizeof(uint16_t);
    if (g_direct_strip == NULL) {
        if (anim_fault_injection_should_fail(ANIM_FAULT_PSRAM_DIRECT_STRIP_ALLOC)) {
            g_direct_strip_pixels = 0;
            return false;
        }
        g_direct_strip = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (g_direct_strip == NULL) {
            ESP_LOGW(TAG, "Direct LCD strip allocation failed: bytes=%u", (unsigned)bytes);
            g_direct_strip_pixels = 0;
            return false;
        }
    }

    g_direct_lcd_ready = true;
    ESP_LOGI(TAG, "Direct LCD animation path enabled: %ux%u strip_rows=%u buffers=%u bytes_each=%u",
             (unsigned)ANIM_DIRECT_LCD_WIDTH, (unsigned)ANIM_DIRECT_LCD_HEIGHT,
             (unsigned)ANIM_DIRECT_LCD_STRIP_DST_ROWS, (unsigned)ANIM_DIRECT_LCD_STRIP_BUFFERS, (unsigned)bytes);
    return true;
}

static void configure_anim_layer_for_direct_lcd(lv_obj_t *img_obj) {
    if (img_obj == NULL) {
        return;
    }

    lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(img_obj, LV_OPA_TRANSP, 0);
}

static void release_direct_lcd_buffers(void) {
    if (g_direct_strip != NULL) {
        heap_caps_free(g_direct_strip);
        g_direct_strip = NULL;
    }
    g_direct_strip_pixels = 0;
    g_direct_lcd_ready = false;
    g_direct_panel = NULL;
}

static void sort_protected_bands(anim_protected_band_t *bands, size_t count) {
    if (bands == NULL || count < 2U) {
        return;
    }

    for (size_t i = 0; i + 1U < count; ++i) {
        for (size_t j = i + 1U; j < count; ++j) {
            if (bands[j].y1 < bands[i].y1) {
                anim_protected_band_t tmp = bands[i];
                bands[i] = bands[j];
                bands[j] = tmp;
            }
        }
    }
}

static size_t collect_direct_lcd_protected_bands(anim_protected_band_t bands[ANIM_DIRECT_LCD_PROTECTED_BAND_MAX],
                                                 int strip_y1, int strip_y2) {
    size_t count = 0;
    int overlay_x1 = 0;
    int overlay_y1 = 0;
    int overlay_x2 = 0;
    int overlay_y2 = 0;
    bool has_custom_region;
    int custom_x1;
    int custom_y1;
    int custom_x2;
    int custom_y2;

    if (bands == NULL || strip_y1 >= strip_y2) {
        return 0;
    }

    if (hal_display_get_text_overlay_bounds(&overlay_x1, &overlay_y1, &overlay_x2, &overlay_y2) &&
        overlay_y2 > strip_y1 && overlay_y1 < strip_y2 && count < ANIM_DIRECT_LCD_PROTECTED_BAND_MAX) {
        (void)overlay_x1;
        (void)overlay_x2;
        bands[count].y1 = max_int(overlay_y1, strip_y1);
        bands[count].y2 = min_int(overlay_y2, strip_y2);
        count++;
    }

    portENTER_CRITICAL(&g_protected_region_lock);
    has_custom_region = g_protected_region_active;
    custom_x1 = g_protected_region_x1;
    custom_y1 = g_protected_region_y1;
    custom_x2 = g_protected_region_x2;
    custom_y2 = g_protected_region_y2;
    portEXIT_CRITICAL(&g_protected_region_lock);

    if (has_custom_region && custom_x2 > custom_x1 && custom_y2 > custom_y1 && custom_y2 > strip_y1 &&
        custom_y1 < strip_y2 && count < ANIM_DIRECT_LCD_PROTECTED_BAND_MAX) {
        (void)custom_x1;
        (void)custom_x2;
        bands[count].y1 = max_int(custom_y1, strip_y1);
        bands[count].y2 = min_int(custom_y2, strip_y2);
        count++;
    }

    sort_protected_bands(bands, count);
    return count;
}

static bool draw_direct_lcd_segment(int y1, int y2, uint16_t *strip, int strip_y1) {
    if (y1 >= y2) {
        return true;
    }

    uint16_t *segment = strip + ((size_t)(y1 - strip_y1) * ANIM_DIRECT_LCD_WIDTH);
    esp_err_t rc = lvgl_port_panel_draw_bitmap(g_direct_panel, 0, y1, ANIM_DIRECT_LCD_WIDTH, y2, segment,
                                               ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Direct LCD draw segment failed: %s", esp_err_to_name(rc));
        return false;
    }
    return true;
}

static bool draw_direct_lcd_strip_skipping_protected(int strip_y1, int strip_y2, uint16_t *strip) {
    anim_protected_band_t bands[ANIM_DIRECT_LCD_PROTECTED_BAND_MAX];
    size_t count = collect_direct_lcd_protected_bands(bands, strip_y1, strip_y2);
    int cursor = strip_y1;

    if (count == 0U) {
        return draw_direct_lcd_segment(strip_y1, strip_y2, strip, strip_y1);
    }

    for (size_t i = 0; i < count; ++i) {
        if (bands[i].y1 > cursor && !draw_direct_lcd_segment(cursor, bands[i].y1, strip, strip_y1)) {
            return false;
        }
        if (bands[i].y2 > cursor) {
            cursor = bands[i].y2;
        }
    }

    if (cursor < strip_y2 && !draw_direct_lcd_segment(cursor, strip_y2, strip, strip_y1)) {
        return false;
    }
    return true;
}

static uint8_t current_fade_alpha_locked(void) {
    if (g_fade_duration_ms == 0U) {
        return ANIM_FADE_ALPHA_MAX;
    }

    int64_t elapsed_us = esp_timer_get_time() - g_fade_start_us;
    int64_t duration_us = (int64_t)g_fade_duration_ms * 1000LL;
    if (elapsed_us <= 0) {
        return 0U;
    }
    if (elapsed_us >= duration_us) {
        g_fade_duration_ms = 0U;
        return ANIM_FADE_ALPHA_MAX;
    }
    return (uint8_t)((elapsed_us * ANIM_FADE_ALPHA_MAX) / duration_us);
}

static bool draw_frame_direct_lcd(const lv_img_dsc_t *frame) {
    if (!g_direct_lcd_ready || g_direct_strip == NULL || frame == NULL || frame->data == NULL ||
        frame->header.w != ANIM_SOURCE_FRAME_SIZE || frame->header.h != ANIM_SOURCE_FRAME_SIZE) {
        return false;
    }

    const uint16_t *src = (const uint16_t *)frame->data;
    uint8_t fade_alpha = current_fade_alpha_locked();
    bool apply_fade = fade_alpha < ANIM_FADE_ALPHA_MAX;
    anim_rgb565_fade_pattern_t fade_pattern;
    if (apply_fade) {
        anim_rgb565_fade_pattern_prepare(&fade_pattern, fade_alpha);
    }
    for (uint32_t src_y = 0; src_y < ANIM_SOURCE_FRAME_SIZE; src_y += ANIM_DIRECT_LCD_STRIP_SRC_ROWS) {
        uint32_t src_rows = ANIM_DIRECT_LCD_STRIP_SRC_ROWS;
        if (src_y + src_rows > ANIM_SOURCE_FRAME_SIZE) {
            src_rows = ANIM_SOURCE_FRAME_SIZE - src_y;
        }
        uint32_t dst_rows = src_rows * ANIM_DIRECT_LCD_SCALE;
        uint16_t *dst = g_direct_strip;

        for (uint32_t row = 0; row < src_rows; ++row) {
            const uint16_t *src_row = src + ((src_y + row) * ANIM_SOURCE_FRAME_SIZE);
            uint16_t *dst_row0 = dst + ((row * ANIM_DIRECT_LCD_SCALE) * ANIM_DIRECT_LCD_WIDTH);
            uint16_t *dst_row1 = dst_row0 + ANIM_DIRECT_LCD_WIDTH;
            uint32_t dst_y0 = (src_y + row) * ANIM_DIRECT_LCD_SCALE;
            uint32_t dst_y1 = dst_y0 + 1U;
            for (uint32_t x = 0; x < ANIM_SOURCE_FRAME_SIZE; ++x) {
                uint16_t color = src_row[x];
                uint32_t dst_x = x * ANIM_DIRECT_LCD_SCALE;
                if (apply_fade) {
                    dst_row0[dst_x] = anim_rgb565_fade_pattern_apply(&fade_pattern, color, dst_x, dst_y0);
                    dst_row0[dst_x + 1U] = anim_rgb565_fade_pattern_apply(&fade_pattern, color, dst_x + 1U, dst_y0);
                    dst_row1[dst_x] = anim_rgb565_fade_pattern_apply(&fade_pattern, color, dst_x, dst_y1);
                    dst_row1[dst_x + 1U] = anim_rgb565_fade_pattern_apply(&fade_pattern, color, dst_x + 1U, dst_y1);
                } else {
                    dst_row0[dst_x] = color;
                    dst_row0[dst_x + 1U] = color;
                }
            }
            if (!apply_fade) {
                memcpy(dst_row1, dst_row0, ANIM_DIRECT_LCD_WIDTH * sizeof(uint16_t));
            }
        }

        int strip_y1 = (int)(src_y * ANIM_DIRECT_LCD_SCALE);
        int strip_y2 = strip_y1 + (int)dst_rows;
        if (!draw_direct_lcd_strip_skipping_protected(strip_y1, strip_y2, dst)) {
            return false;
        }
    }

    return true;
}

static void configure_anim_layer(lv_obj_t *img_obj) {
    if (img_obj == NULL) {
        return;
    }

    lv_obj_set_size(img_obj, ANIM_SOURCE_FRAME_SIZE, ANIM_SOURCE_FRAME_SIZE);
    lv_img_set_pivot(img_obj, ANIM_SOURCE_FRAME_PIVOT, ANIM_SOURCE_FRAME_PIVOT);
    lv_img_set_zoom(img_obj, ANIM_DISPLAY_ZOOM_2X);
    lv_img_set_antialias(img_obj, false);
}

static void hide_back_layer(void) {
    if (g_back_img != NULL) {
        lv_obj_add_flag(g_back_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(g_back_img, LV_OPA_TRANSP, 0);
    }
    if (g_front_img != NULL) {
        if (g_direct_lcd_ready) {
            configure_anim_layer_for_direct_lcd(g_front_img);
        } else {
            lv_obj_set_style_opa(g_front_img, LV_OPA_COVER, 0);
        }
    }
}

_Static_assert(ANIM_FRAME_POOL_CAPACITY <= ANIM_FRAME_POOL_CORE_MAX_SLOTS,
               "Frame pool capacity exceeds allocator core limit");

static void frame_pool_deinit(void) {
    for (uint16_t index = 0U; index < ANIM_FRAME_POOL_CAPACITY; ++index) {
        anim_frame_buffer_free(&g_frame_pool_buffers[index]);
    }
    memset(&g_frame_pool_core, 0, sizeof(g_frame_pool_core));
    g_frame_pool_initialized = false;
}

static bool frame_pool_init(void) {
    if (g_frame_pool_initialized) {
        return true;
    }
    if (!anim_frame_pool_core_init(&g_frame_pool_core, ANIM_FRAME_POOL_CAPACITY)) {
        return false;
    }
    memset(g_frame_pool_buffers, 0, sizeof(g_frame_pool_buffers));
    for (uint16_t index = 0U; index < ANIM_FRAME_POOL_CAPACITY; ++index) {
        if (anim_frame_buffer_init(&g_frame_pool_buffers[index], ANIM_SOURCE_FRAME_SIZE, ANIM_SOURCE_FRAME_SIZE) != 0) {
            ESP_LOGE(TAG, "Frame pool allocation failed: slot=%u/%u", (unsigned)index,
                     (unsigned)ANIM_FRAME_POOL_CAPACITY);
            frame_pool_deinit();
            return false;
        }
    }
    g_frame_pool_initialized = true;
    ESP_LOGI(TAG, "Frame pool ready: slots=%u frame_bytes=%u total_bytes=%u", (unsigned)ANIM_FRAME_POOL_CAPACITY,
             (unsigned)g_frame_pool_buffers[0].data_size,
             (unsigned)(ANIM_FRAME_POOL_CAPACITY * g_frame_pool_buffers[0].data_size));
    return true;
}

static void mark_safe_error_if_no_valid_frame_locked(void) {
    if (!g_active_playback.in_use || g_active_playback.current_slot < 0 ||
        g_active_playback.current_slot >= g_active_playback.slot_count ||
        g_active_playback.slots[g_active_playback.current_slot].img_dsc.data == NULL) {
        g_safe_error_pending = true;
    }
}

static void clear_safe_error_locked(void) {
    if (!g_safe_error_visible && !g_safe_error_pending) {
        return;
    }
    if (!g_direct_lcd_ready && g_front_img != NULL) {
        lv_obj_t *parent = lv_obj_get_parent(g_front_img);
        if (parent != NULL) {
            lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
        }
        lv_obj_clear_flag(g_front_img, LV_OBJ_FLAG_HIDDEN);
    }
    g_safe_error_pending = false;
    g_safe_error_visible = false;
}

static void render_safe_error_page_locked(void) {
    if (!g_safe_error_pending) {
        return;
    }

    bool rendered = false;
    if (g_direct_lcd_ready && g_direct_strip != NULL) {
        for (uint32_t y = 0U; y < ANIM_DIRECT_LCD_HEIGHT; y += ANIM_DIRECT_LCD_STRIP_DST_ROWS) {
            uint32_t rows = ANIM_DIRECT_LCD_STRIP_DST_ROWS;
            if (y + rows > ANIM_DIRECT_LCD_HEIGHT) {
                rows = ANIM_DIRECT_LCD_HEIGHT - y;
            }
            for (uint32_t row = 0U; row < rows; ++row) {
                for (uint32_t x = 0U; x < ANIM_DIRECT_LCD_WIDTH; ++x) {
                    uint32_t screen_y = y + row;
                    bool border = x < 12U || x >= ANIM_DIRECT_LCD_WIDTH - 12U || screen_y < 12U ||
                                  screen_y >= ANIM_DIRECT_LCD_HEIGHT - 12U;
                    bool diagonal = x + 10U >= screen_y && x <= screen_y + 10U;
                    g_direct_strip[row * ANIM_DIRECT_LCD_WIDTH + x] = border || diagonal ? 0xF800U : 0x1000U;
                }
            }
            if (lvgl_port_panel_draw_bitmap(g_direct_panel, 0, (int)y, ANIM_DIRECT_LCD_WIDTH, (int)(y + rows),
                                            g_direct_strip, ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS) != ESP_OK) {
                return;
            }
        }
        rendered = true;
    } else if (g_front_img != NULL) {
        lv_obj_t *parent = lv_obj_get_parent(g_front_img);
        if (parent != NULL) {
            lv_obj_add_flag(g_front_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(parent, lv_color_hex(0x300000), 0);
            lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
            rendered = true;
        }
    }

    if (rendered) {
        g_safe_error_pending = false;
        g_safe_error_visible = true;
        ESP_LOGE(TAG, "Displayed built-in animation error page");
    }
}

static bool frame_pool_acquire(anim_playback_t *playback, uint16_t count) {
    uint16_t indices[WATCHER_ANIM_RING_FRAMES];

    if (playback == NULL || count == 0U || count > WATCHER_ANIM_RING_FRAMES || !g_frame_pool_initialized ||
        !anim_frame_pool_core_acquire(&g_frame_pool_core, count, indices)) {
        return false;
    }
    for (uint16_t index = 0U; index < count; ++index) {
        playback->slot_pool_index[index] = indices[index];
        playback->slots[index] = g_frame_pool_buffers[indices[index]];
    }
    return true;
}

static void frame_pool_release(anim_playback_t *playback) {
    uint16_t indices[WATCHER_ANIM_RING_FRAMES];
    uint16_t count = 0U;

    if (playback == NULL || !g_frame_pool_initialized) {
        return;
    }
    for (int index = 0; index < playback->slot_count; ++index) {
        if (playback->slot_pool_index[index] != ANIM_FRAME_POOL_INVALID_INDEX) {
            indices[count++] = playback->slot_pool_index[index];
            playback->slot_pool_index[index] = ANIM_FRAME_POOL_INVALID_INDEX;
            memset(&playback->slots[index], 0, sizeof(playback->slots[index]));
        }
    }
    anim_frame_pool_core_release(&g_frame_pool_core, count, indices);
}

static void playback_reset_slots(anim_playback_t *playback) {
    for (int index = 0; index < WATCHER_ANIM_RING_FRAMES; ++index) {
        playback->slot_frame_index[index] = ANIM_INVALID_SLOT_FRAME;
        playback->slot_pool_index[index] = ANIM_FRAME_POOL_INVALID_INDEX;
    }
}

static void playback_cleanup(anim_playback_t *playback) {
    if (playback == NULL) {
        return;
    }

    anim_stream_close(&playback->stream);
    frame_pool_release(playback);
    memset(playback, 0, sizeof(*playback));
    playback_reset_slots(playback);
}

static int playback_open(anim_playback_t *playback, emoji_anim_type_t type, uint32_t generation_id,
                         animation_ticket_t ticket, animation_playback_mode_t playback_mode, uint16_t repeat_count,
                         uint16_t fade_in_ms) {
    memset(playback, 0, sizeof(*playback));
    playback_reset_slots(playback);

    int storage_result = anim_stream_open(type, &playback->stream);
    if (storage_result != ANIM_STORAGE_OK) {
        switch (storage_result) {
        case ANIM_STORAGE_NO_MEMORY:
            return ANIM_PLAYBACK_OPEN_NO_MEMORY;
        case ANIM_STORAGE_INVALID_RESOURCE:
            return ANIM_PLAYBACK_OPEN_INVALID_RESOURCE;
        case ANIM_STORAGE_PACK_CORRUPT:
            return ANIM_PLAYBACK_OPEN_PACK_CORRUPT;
        case ANIM_STORAGE_SD_READ_FAILED:
            return ANIM_PLAYBACK_OPEN_SD_READ_FAILED;
        case ANIM_STORAGE_SD_OPEN_FAILED:
        default:
            return ANIM_PLAYBACK_OPEN_STREAM_FAILED;
        }
    }

    playback->slot_count =
        min_int(WATCHER_ANIM_RING_FRAMES, playback->stream.frame_count > 0 ? playback->stream.frame_count : 1);
    playback->in_use = true;
    playback->type = type;
    playback->generation_id = generation_id;
    playback->ticket = ticket;
    playback->playback_mode = playback_mode;
    playback->repeat_count = repeat_count;
    playback->fade_in_ms = fade_in_ms;
    playback->current_slot = 0;
    playback->current_frame_index = 0;
    playback->buffered_frames = 0;
    playback->next_frame_to_load = 0;
    playback->next_slot_to_load = 0;

    if (playback->stream.info.width != ANIM_SOURCE_FRAME_SIZE ||
        playback->stream.info.height != ANIM_SOURCE_FRAME_SIZE) {
        playback_cleanup(playback);
        return ANIM_PLAYBACK_OPEN_INVALID_RESOURCE;
    }
    if (!frame_pool_acquire(playback, (uint16_t)playback->slot_count)) {
        playback_cleanup(playback);
        return ANIM_PLAYBACK_OPEN_NO_MEMORY;
    }

    return ANIM_PLAYBACK_OPEN_OK;
}

static bool playback_has_loadable_frame(const anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use || playback->buffered_frames >= playback->slot_count ||
        playback->stream.frame_count <= 0) {
        return false;
    }

    return playback->next_frame_to_load < playback->stream.frame_count || playback_should_cycle(playback);
}

static bool playback_prepare_load_job(anim_playback_t *playback, anim_load_target_t target, anim_load_job_t *job) {
    if (job == NULL) {
        return false;
    }
    memset(job, 0, sizeof(*job));

    if (!playback_has_loadable_frame(playback)) {
        return false;
    }

    int frame_count = playback->stream.frame_count;
    if (frame_count <= 0) {
        return false;
    }

    int frame_index = playback->next_frame_to_load;
    if (frame_index >= frame_count) {
        if (!playback_should_cycle(playback)) {
            return false;
        }
        frame_index %= frame_count;
    }

    job->valid = true;
    job->target = target;
    job->type = playback->type;
    job->generation_id = playback->generation_id;
    job->ticket = playback->ticket;
    job->frame_index = frame_index;
    job->slot_index = playback->next_slot_to_load;
    return true;
}

static bool playback_commit_loaded_frame(anim_playback_t *playback, const anim_load_job_t *job,
                                         const anim_frame_buffer_t *loaded_frame, int64_t read_us) {
    if (playback == NULL || job == NULL || loaded_frame == NULL || !job->valid || !playback->in_use) {
        return false;
    }
    if (playback->type != job->type || playback->generation_id != job->generation_id ||
        playback->ticket != job->ticket || playback->buffered_frames >= playback->slot_count ||
        playback->next_slot_to_load != job->slot_index) {
        return false;
    }

    anim_load_job_t expected_job = {0};
    if (!playback_prepare_load_job(playback, job->target, &expected_job) ||
        expected_job.frame_index != job->frame_index || expected_job.slot_index != job->slot_index) {
        return false;
    }

    anim_frame_buffer_t *slot = &playback->slots[job->slot_index];
    if (loaded_frame->img_dsc.data == NULL || loaded_frame->img_dsc.data_size > slot->data_size) {
        return false;
    }

    memcpy(slot->img_data, loaded_frame->img_dsc.data, loaded_frame->img_dsc.data_size);
    slot->img_dsc.data = slot->img_data;
    slot->img_dsc.data_size = loaded_frame->img_dsc.data_size;
    playback->slot_frame_index[job->slot_index] = job->frame_index;
    playback->buffered_frames++;
    playback->next_frame_to_load = job->frame_index + 1;
    playback->next_slot_to_load = (job->slot_index + 1) % playback->slot_count;
    playback->load_failure_count = 0;
    if (job->target == ANIM_LOAD_TARGET_PENDING) {
        anim_player_lifecycle_note_prepare_progress(&g_lifecycle, job->generation_id, job->ticket);
        (void)anim_prepare_watchdog_note_progress(&g_prepare_watchdog, job->generation_id, job->ticket,
                                                  (uint64_t)(esp_timer_get_time() / 1000LL));
    }
    playback_note_read_frame(playback, read_us);
    return true;
}

static bool playback_note_load_failure(anim_playback_t *playback, const anim_load_job_t *job,
                                       anim_player_failure_t failure) {
    if (playback == NULL || job == NULL || !job->valid || !playback->in_use) {
        return false;
    }
    if (playback->type != job->type || playback->generation_id != job->generation_id ||
        playback->ticket != job->ticket || playback->next_slot_to_load != job->slot_index) {
        return false;
    }

    anim_load_job_t expected_job = {0};
    if (!playback_prepare_load_job(playback, job->target, &expected_job) ||
        expected_job.frame_index != job->frame_index || expected_job.slot_index != job->slot_index) {
        return false;
    }

    if (job->target == ANIM_LOAD_TARGET_PENDING) {
        anim_player_lifecycle_events_t events = anim_player_lifecycle_note_prepare_failure(
            &g_lifecycle, job->generation_id, job->ticket, failure, ANIM_FRAME_LOAD_FAILURE_LIMIT);
        playback->load_failure_count++;
        if (events.count == 0U) {
            return true;
        }
        ESP_LOGW(TAG, "Cancelling pending animation switch to %s after %lu failed load attempt(s) at frame %d: %s",
                 emoji_type_name(job->type), (unsigned long)playback->load_failure_count, job->frame_index,
                 player_failure_name(failure));
        playback_cleanup(playback);
        restore_after_prepare_failure_locked();
        (void)enqueue_lifecycle_events_locked(&events);
        return true;
    }

    playback->load_failure_count++;
    if (playback->load_failure_count < ANIM_FRAME_LOAD_FAILURE_LIMIT) {
        return true;
    }

    ESP_LOGW(TAG, "Skipping unreadable frame %d for %s after %lu failed read(s)", job->frame_index,
             emoji_type_name(job->type), (unsigned long)playback->load_failure_count);
    playback->next_frame_to_load = job->frame_index + 1;
    playback->load_failure_count = 0;
    return true;
}

static bool playback_advance(anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use) {
        return false;
    }

    if (playback->buffered_frames <= 1) {
        return false;
    }

    int old_slot = playback->current_slot;
    int next_slot = (old_slot + 1) % playback->slot_count;
    if (playback->slot_frame_index[next_slot] < 0) {
        return false;
    }

    playback->slot_frame_index[old_slot] = ANIM_INVALID_SLOT_FRAME;
    playback->current_slot = next_slot;
    playback->current_frame_index = playback->slot_frame_index[next_slot];
    playback->buffered_frames--;
    return true;
}

static bool playback_reached_final_frame(const anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use || playback_should_cycle(playback)) {
        return false;
    }

    return playback->stream.frame_count > 0 && playback->current_frame_index >= playback->stream.frame_count - 1 &&
           playback->buffered_frames <= 1;
}

static anim_playback_t *playback_for_load_target(anim_load_target_t target) {
    switch (target) {
    case ANIM_LOAD_TARGET_PENDING:
        return &g_pending_playback;
    case ANIM_LOAD_TARGET_PREFETCH:
        return &g_prefetch_playback;
    case ANIM_LOAD_TARGET_ACTIVE:
    default:
        return &g_active_playback;
    }
}

static const lv_img_dsc_t *playback_current_descriptor(anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use || playback->current_slot < 0) {
        return NULL;
    }
    return &playback->slots[playback->current_slot].img_dsc;
}

static void redraw_active_frame_locked(void) {
    const lv_img_dsc_t *frame = playback_current_descriptor(&g_active_playback);
    if (frame == NULL || g_front_img == NULL) {
        return;
    }
    if (g_direct_lcd_ready) {
        (void)draw_frame_direct_lcd(frame);
    } else {
        lv_img_set_src(g_front_img, frame);
        lv_obj_set_style_opa(g_front_img, current_fade_alpha_locked(), 0);
    }
}

static int ensure_worker_started(void) {
    if (g_player_mutex == NULL) {
        g_player_mutex = xSemaphoreCreateMutex();
        if (g_player_mutex == NULL) {
            return -1;
        }
    }

    if (g_worker_exit_sem == NULL) {
        g_worker_exit_sem = xSemaphoreCreateBinary();
        if (g_worker_exit_sem == NULL) {
            return -1;
        }
    }

    if (g_worker_task == NULL) {
        (void)xSemaphoreTake(g_worker_exit_sem, 0);
        g_worker_shutdown_requested = false;
        BaseType_t rc =
            xTaskCreate(anim_worker_task, "anim_stream", 6144, NULL, ANIM_WORKER_TASK_PRIORITY, &g_worker_task);
        if (rc != pdPASS) {
            g_worker_task = NULL;
            return -1;
        }
        if (xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
            anim_player_lifecycle_worker_started(&g_lifecycle);
            xSemaphoreGive(g_player_mutex);
        }
    }

    return 0;
}

static void restore_after_prepare_failure_locked(void) {
    anim_prepare_watchdog_reset(&g_prepare_watchdog);
    g_state = g_active_playback.in_use ? g_state_before_prepare : ANIM_PLAYER_IDLE;
    g_requested_type = g_state == ANIM_PLAYER_PLAYING ? g_current_type : EMOJI_ANIM_NONE;
    g_frame_timer_should_run = g_state == ANIM_PLAYER_PLAYING;
    g_frame_timer_update_pending = true;
    mark_safe_error_if_no_valid_frame_locked();
}

/* Called only from the LVGL service timer callback while holding g_player_mutex. */
static void apply_frame_timer_update_locked(void) {
    if (!g_frame_timer_update_pending || g_frame_timer == NULL) {
        return;
    }
    if (g_frame_timer_should_run) {
        lv_timer_resume(g_frame_timer);
    } else {
        lv_timer_pause(g_frame_timer);
    }
    g_frame_timer_update_pending = false;
}

static int commit_pending_playback(void) {
    if (!g_pending_playback.in_use || g_pending_playback.buffered_frames <= 0) {
        return -1;
    }

    if (playback_current_descriptor(&g_pending_playback) == NULL) {
        return -1;
    }

    bool pending_should_loop = playback_should_cycle(&g_pending_playback);
    size_t required_event_slots = g_pending_playback.ticket == ANIMATION_TICKET_INVALID
                                      ? 0U
                                      : (!pending_should_loop && g_pending_playback.stream.frame_count == 1 ? 2U : 1U);
    if (required_event_slots > 0U && (anim_player_event_queue_has_overflow_fault(&g_ticket_event_queue) ||
                                      anim_player_event_queue_free(&g_ticket_event_queue) < required_event_slots)) {
        return EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL;
    }

    const lv_img_dsc_t *staging_frame = playback_current_descriptor(&g_pending_playback);

    int64_t previous_fade_start_us = g_fade_start_us;
    uint16_t previous_fade_duration_ms = g_fade_duration_ms;
    g_fade_start_us = esp_timer_get_time();
    g_fade_duration_ms = g_pending_playback.fade_in_ms;

    int64_t initial_draw_us = -1;
    bool initial_draw_failed = false;
    if (g_front_img != NULL) {
        if (anim_fault_injection_should_fail(ANIM_FAULT_RENDER)) {
            initial_draw_failed = true;
        } else if (g_direct_lcd_ready) {
            configure_anim_layer_for_direct_lcd(g_front_img);
            int64_t draw_started_us = esp_timer_get_time();
            if (draw_frame_direct_lcd(staging_frame)) {
                initial_draw_us = esp_timer_get_time() - draw_started_us;
            } else {
                initial_draw_failed = true;
            }
        } else {
            lv_img_set_src(g_front_img, staging_frame);
            lv_obj_set_style_opa(g_front_img, current_fade_alpha_locked(), 0);
        }
    }

    if (initial_draw_failed) {
        g_fade_start_us = previous_fade_start_us;
        g_fade_duration_ms = previous_fade_duration_ms;
        redraw_active_frame_locked();
        anim_player_lifecycle_events_t failed_events = anim_player_lifecycle_note_prepare_failure(
            &g_lifecycle, g_pending_playback.generation_id, g_pending_playback.ticket, ANIM_PLAYER_FAILURE_RENDER, 1U);
        playback_note_draw_failure(&g_pending_playback);
        playback_cleanup(&g_pending_playback);
        restore_after_prepare_failure_locked();
        (void)enqueue_lifecycle_events_locked(&failed_events);
        return -1;
    }

    bool should_loop = pending_should_loop;
    anim_player_lifecycle_events_t lifecycle_events = anim_player_lifecycle_commit(
        &g_lifecycle, g_pending_playback.generation_id, g_pending_playback.ticket, (int32_t)g_pending_playback.type,
        (uint32_t)g_pending_playback.stream.frame_count, should_loop);
    if (lifecycle_events.count == 0U) {
        g_fade_start_us = previous_fade_start_us;
        g_fade_duration_ms = previous_fade_duration_ms;
        ESP_LOGW(TAG, "Ignoring stale animation commit: %s generation=%lu", emoji_type_name(g_pending_playback.type),
                 (unsigned long)g_pending_playback.generation_id);
        redraw_active_frame_locked();
        playback_cleanup(&g_pending_playback);
        restore_after_prepare_failure_locked();
        return -1;
    }

    playback_cleanup(&g_active_playback);
    g_active_playback = g_pending_playback;
    memset(&g_pending_playback, 0, sizeof(g_pending_playback));
    playback_reset_slots(&g_pending_playback);
    anim_prepare_watchdog_reset(&g_prepare_watchdog);
    clear_safe_error_locked();
    hide_back_layer();

    g_state = should_loop || g_active_playback.stream.frame_count > 1 ? ANIM_PLAYER_PLAYING : ANIM_PLAYER_IDLE;
    g_current_type = g_active_playback.type;
    g_requested_type = g_state == ANIM_PLAYER_PLAYING ? g_active_playback.type : EMOJI_ANIM_NONE;
    int64_t now_us = esp_timer_get_time();
    playback_reset_perf_stats(&g_active_playback, now_us);
    if (initial_draw_us >= 0) {
        playback_note_draw_frame(&g_active_playback, initial_draw_us);
    }
    g_next_frame_deadline_us =
        now_us + (int64_t)effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index) * 1000LL;
    g_frame_timer_should_run = g_state == ANIM_PLAYER_PLAYING;
    g_frame_timer_update_pending = true;

    ESP_LOGI(TAG, "Animation switch committed: %s frames=%d loop_ms=%d ring=%d", emoji_type_name(g_current_type),
             g_active_playback.stream.frame_count, playback_loop_duration_ms(&g_active_playback),
             g_active_playback.slot_count);
    (void)enqueue_lifecycle_events_locked(&lifecycle_events);
    return 0;
}

/* Caller holds g_player_mutex. */
static anim_worker_target_t select_worker_target_locked(void) {
    bool deadline_near = false;
    if (g_active_playback.in_use && g_next_frame_deadline_us > 0) {
        int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
        int64_t remaining_us = g_next_frame_deadline_us - esp_timer_get_time();
        deadline_near = remaining_us <= (int64_t)max_int(delay_ms, 1) * 1000LL;
    }

    anim_worker_schedule_input_t input = {
        .active_in_use = g_active_playback.in_use,
        .active_loadable = playback_has_loadable_frame(&g_active_playback) &&
                           (g_state == ANIM_PLAYER_PLAYING ||
                            (g_state == ANIM_PLAYER_SWITCH_PREPARING && g_state_before_prepare == ANIM_PLAYER_PLAYING)),
        .active_buffered = (uint16_t)max_int(g_active_playback.buffered_frames, 0),
        .active_capacity = (uint16_t)max_int(g_active_playback.slot_count, 0),
        .active_deadline_near = deadline_near,
        .staging_in_use = g_pending_playback.in_use,
        .staging_loadable = playback_has_loadable_frame(&g_pending_playback),
        .staging_buffered = (uint16_t)max_int(g_pending_playback.buffered_frames, 0),
        .staging_capacity = (uint16_t)max_int(g_pending_playback.slot_count, 0),
        .prefetch_in_use = g_prefetch_playback.in_use,
        .prefetch_loadable = playback_has_loadable_frame(&g_prefetch_playback),
        .prefetch_buffered = (uint16_t)max_int(g_prefetch_playback.buffered_frames, 0),
        .prefetch_capacity = (uint16_t)max_int(g_prefetch_playback.slot_count, 0),
        .active_low_water = ANIM_ACTIVE_LOW_WATER_FRAMES,
        .staging_prefill = ANIM_STAGING_PREFILL_FRAMES,
    };
    return anim_worker_schedule(&input);
}

static void anim_worker_task(void *param) {
    (void)param;

    anim_stream_t worker_stream = {0};
    anim_frame_buffer_t worker_frame = {0};
    emoji_anim_type_t worker_stream_type = EMOJI_ANIM_NONE;

    for (;;) {
        bool did_work = false;
        anim_load_job_t job = {0};

        if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
            if (g_worker_shutdown_requested) {
                xSemaphoreGive(g_player_mutex);
                anim_stream_close(&worker_stream);
                anim_frame_buffer_free(&worker_frame);
                worker_stream_type = EMOJI_ANIM_NONE;
                if (xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
                    g_worker_task = NULL;
                    anim_player_lifecycle_worker_exited(&g_lifecycle);
                    xSemaphoreGive(g_player_mutex);
                }
                if (g_worker_exit_sem != NULL) {
                    xSemaphoreGive(g_worker_exit_sem);
                }
                vTaskDelete(NULL);
                return;
            }
            bool prepare_watchdog_expired = poll_prepare_watchdog_locked();
            if (prepare_watchdog_expired) {
                xSemaphoreGive(g_player_mutex);
                emoji_anim_player_dispatch_events();
                continue;
            }
            anim_worker_target_t target = select_worker_target_locked();
            if (target == ANIM_WORK_ACTIVE) {
                did_work = playback_prepare_load_job(&g_active_playback, ANIM_LOAD_TARGET_ACTIVE, &job);
            } else if (target == ANIM_WORK_STAGING) {
                did_work = playback_prepare_load_job(&g_pending_playback, ANIM_LOAD_TARGET_PENDING, &job);
            } else if (target == ANIM_WORK_PREFETCH) {
                did_work = playback_prepare_load_job(&g_prefetch_playback, ANIM_LOAD_TARGET_PREFETCH, &job);
            }
            xSemaphoreGive(g_player_mutex);
        }

        if (!did_work || !job.valid) {
            vTaskDelay(pdMS_TO_TICKS(ANIM_WORKER_IDLE_MS));
            continue;
        }

        if (worker_stream_type != job.type) {
            anim_stream_close(&worker_stream);
            worker_stream_type = EMOJI_ANIM_NONE;
            int storage_result = anim_stream_open(job.type, &worker_stream);
            if (storage_result != ANIM_STORAGE_OK) {
                ESP_LOGW(TAG, "Failed to open worker stream for %s", emoji_type_name(job.type));
                if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
                    anim_playback_t *target = playback_for_load_target(job.target);
                    (void)playback_note_load_failure(target, &job,
                                                     player_failure_from_storage_result(storage_result, true));
                    xSemaphoreGive(g_player_mutex);
                }
                emoji_anim_player_dispatch_events();
                vTaskDelay(pdMS_TO_TICKS(ANIM_WORKER_IDLE_MS));
                continue;
            }
            worker_stream_type = job.type;
        }

        int64_t read_started_us = esp_timer_get_time();
        int storage_result = anim_stream_read_frame(&worker_stream, job.frame_index, &worker_frame);
        if (storage_result != ANIM_STORAGE_OK) {
            ESP_LOGW(TAG, "Failed to read frame %d for %s", job.frame_index, emoji_type_name(job.type));
            anim_stream_close(&worker_stream);
            worker_stream_type = EMOJI_ANIM_NONE;
            if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
                anim_playback_t *target = playback_for_load_target(job.target);
                (void)playback_note_load_failure(target, &job,
                                                 player_failure_from_storage_result(storage_result, false));
                xSemaphoreGive(g_player_mutex);
            }
            emoji_anim_player_dispatch_events();
            vTaskDelay(pdMS_TO_TICKS(ANIM_WORKER_IDLE_MS));
            continue;
        }
        int64_t read_us = esp_timer_get_time() - read_started_us;

        if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
            anim_playback_t *target = playback_for_load_target(job.target);
            (void)playback_commit_loaded_frame(target, &job, &worker_frame, read_us);
            xSemaphoreGive(g_player_mutex);
        }
    }
}

static bool anim_wait_for_worker_exit(uint32_t timeout_ms) {
    if (g_worker_task == NULL) {
        return true;
    }
    return g_worker_exit_sem != NULL && xSemaphoreTake(g_worker_exit_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static bool anim_request_worker_shutdown(void) {
    if (g_player_mutex == NULL || g_worker_task == NULL) {
        return true;
    }

    if (xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
        g_worker_shutdown_requested = true;
        anim_player_lifecycle_request_worker_stop(&g_lifecycle);
        xSemaphoreGive(g_player_mutex);
    }

    if (!anim_wait_for_worker_exit(ANIM_WORKER_EXIT_WAIT_MS)) {
        ESP_LOGW(TAG, "Animation worker did not exit within %u ms", (unsigned)ANIM_WORKER_EXIT_WAIT_MS);
        return false;
    }

    return true;
}

static void anim_frame_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (g_player_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_player_mutex, 0) != pdTRUE) {
        return;
    }
    if (g_state != ANIM_PLAYER_PLAYING || g_current_type == EMOJI_ANIM_NONE) {
        xSemaphoreGive(g_player_mutex);
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us < g_next_frame_deadline_us) {
        xSemaphoreGive(g_player_mutex);
        return;
    }

    const lv_img_dsc_t *latest_frame = NULL;
    bool frame_advanced = false;
    int64_t due_us = g_next_frame_deadline_us;
    if (!playback_advance(&g_active_playback)) {
        if (playback_should_cycle(&g_active_playback) && g_active_playback.stream.frame_count == 1) {
            latest_frame = playback_current_descriptor(&g_active_playback);
            int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
            g_next_frame_deadline_us = now_us + (int64_t)delay_ms * 1000LL;
        } else if (playback_reached_final_frame(&g_active_playback)) {
            latest_frame = playback_current_descriptor(&g_active_playback);
            int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
            g_next_frame_deadline_us = now_us + (int64_t)delay_ms * 1000LL;
        } else {
            playback_note_underrun(&g_active_playback);
            int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
            g_next_frame_deadline_us = now_us + (int64_t)delay_ms * 1000LL;
        }
    } else {
        frame_advanced = true;
        latest_frame = playback_current_descriptor(&g_active_playback);
        g_current_type = g_active_playback.type;
        int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
        int64_t delay_us = (int64_t)delay_ms * 1000LL;
        int64_t late_us = now_us - due_us;
        g_next_frame_deadline_us = late_us > delay_us ? now_us + delay_us : due_us + delay_us;
    }

    if (latest_frame != NULL && g_front_img != NULL) {
        bool draw_succeeded = false;
        int64_t draw_started_us = esp_timer_get_time();
        if (anim_fault_injection_should_fail(ANIM_FAULT_RENDER)) {
            playback_note_draw_failure(&g_active_playback);
        } else if (g_direct_lcd_ready) {
            if (draw_frame_direct_lcd(latest_frame)) {
                playback_note_draw_frame(&g_active_playback, esp_timer_get_time() - draw_started_us);
                draw_succeeded = true;
            } else {
                playback_note_draw_failure(&g_active_playback);
            }
        } else {
            lv_img_set_src(g_front_img, latest_frame);
            lv_obj_set_style_opa(g_front_img, current_fade_alpha_locked(), 0);
            draw_succeeded = true;
        }

        if (draw_succeeded) {
            if (frame_advanced) {
                int64_t late_us = now_us - due_us;
                playback_note_displayed_frame(&g_active_playback, now_us, late_us);
            }
            anim_player_lifecycle_events_t events = anim_player_lifecycle_note_frame(
                &g_lifecycle, g_active_playback.generation_id, g_active_playback.ticket,
                (uint32_t)g_active_playback.current_frame_index, (uint32_t)g_active_playback.stream.frame_count,
                playback_should_cycle(&g_active_playback));
            (void)enqueue_lifecycle_events_locked(&events);
            if (events.count > 0U && events.items[events.count - 1U].type == ANIM_PLAYER_EVENT_COMPLETED) {
                if (g_frame_timer != NULL) {
                    lv_timer_pause(g_frame_timer);
                }
                g_state = ANIM_PLAYER_IDLE;
                g_requested_type = EMOJI_ANIM_NONE;
                g_next_frame_deadline_us = 0;
                ESP_LOGI(TAG, "Animation completed and held on final frame: %s", emoji_type_name(g_current_type));
            }
        } else {
            anim_player_lifecycle_events_t failed_events = anim_player_lifecycle_fail_visible(
                &g_lifecycle, g_active_playback.generation_id, g_active_playback.ticket, ANIM_PLAYER_FAILURE_RENDER);
            if (failed_events.count > 0U) {
                g_state = ANIM_PLAYER_IDLE;
                g_requested_type = EMOJI_ANIM_NONE;
                g_next_frame_deadline_us = 0;
                g_safe_error_pending = true;
                if (g_frame_timer != NULL) {
                    lv_timer_pause(g_frame_timer);
                }
                (void)enqueue_lifecycle_events_locked(&failed_events);
            }
        }
    }

    xSemaphoreGive(g_player_mutex);
    emoji_anim_player_dispatch_events();
}

static bool poll_prepare_watchdog_locked(void) {
    if (g_state != ANIM_PLAYER_SWITCH_PREPARING) {
        return false;
    }

    uint32_t stalled_generation = 0U;
    uint32_t stalled_ticket = 0U;
    if (anim_prepare_watchdog_poll(&g_prepare_watchdog, (uint64_t)(esp_timer_get_time() / 1000LL), &stalled_generation,
                                   &stalled_ticket)) {
        anim_player_lifecycle_events_t failed_events = anim_player_lifecycle_note_prepare_failure(
            &g_lifecycle, stalled_generation, stalled_ticket, ANIM_PLAYER_FAILURE_PREPARE_STALLED, 1U);
        if (failed_events.count > 0U) {
            ESP_LOGE(TAG, "Animation prepare stalled: ticket=%lu generation=%lu timeout_ms=%u",
                     (unsigned long)stalled_ticket, (unsigned long)stalled_generation,
                     (unsigned)ANIM_PREPARE_STALL_TIMEOUT_MS);
            playback_cleanup(&g_pending_playback);
            restore_after_prepare_failure_locked();
            (void)enqueue_lifecycle_events_locked(&failed_events);
            return true;
        }
    }

    return false;
}

void emoji_anim_player_poll_prepare_watchdog(void) {
    bool expired = false;

    if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, 0) == pdTRUE) {
        expired = poll_prepare_watchdog_locked();
        xSemaphoreGive(g_player_mutex);
    }
    if (expired) {
        emoji_anim_player_dispatch_events();
    }
}

static void anim_service_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (g_player_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_player_mutex, 0) != pdTRUE) {
        return;
    }
    apply_frame_timer_update_locked();
    render_safe_error_page_locked();
    if (poll_prepare_watchdog_locked()) {
        xSemaphoreGive(g_player_mutex);
        emoji_anim_player_dispatch_events();
        return;
    }

    int required_frames = min_int((int)ANIM_STAGING_PREFILL_FRAMES, g_pending_playback.slot_count);
    if (g_pending_playback.in_use && g_pending_playback.buffered_frames >= required_frames) {
        (void)commit_pending_playback();
    }
    apply_frame_timer_update_locked();

    xSemaphoreGive(g_player_mutex);
    emoji_anim_player_dispatch_events();
}

static void render_cold_start_error(lv_obj_t *img_obj) {
    if (img_obj == NULL || !lvgl_port_lock(0)) {
        return;
    }
    lv_obj_t *parent = lv_obj_get_parent(img_obj);
    if (parent != NULL) {
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x300000), 0);
        lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    }
    lvgl_port_unlock();
}

static void cleanup_failed_player_init(void) {
    if (lvgl_port_lock(0)) {
        if (g_frame_timer != NULL) {
            lv_timer_del(g_frame_timer);
            g_frame_timer = NULL;
        }
        if (g_service_timer != NULL) {
            lv_timer_del(g_service_timer);
            g_service_timer = NULL;
        }
        if (g_back_img != NULL) {
            lv_obj_del(g_back_img);
            g_back_img = NULL;
        }
        lvgl_port_unlock();
    }
    release_direct_lcd_buffers();
    frame_pool_deinit();
    if (g_worker_task == NULL && g_player_mutex != NULL) {
        vSemaphoreDelete(g_player_mutex);
        g_player_mutex = NULL;
    }
    if (g_worker_task == NULL && g_worker_exit_sem != NULL) {
        vSemaphoreDelete(g_worker_exit_sem);
        g_worker_exit_sem = NULL;
    }
    g_front_img = NULL;
}

int emoji_anim_init(lv_obj_t *img_obj) {
    if (img_obj == NULL) {
        ESP_LOGE(TAG, "Invalid image object");
        return -1;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGE(TAG, "Animation catalog init failed");
        render_cold_start_error(img_obj);
        return -1;
    }
    if (!frame_pool_init()) {
        ESP_LOGE(TAG, "Animation frame pool init failed");
        render_cold_start_error(img_obj);
        return -1;
    }

    g_front_img = img_obj;
    g_current_type = EMOJI_ANIM_NONE;
    g_requested_type = EMOJI_ANIM_NONE;
    g_state = ANIM_PLAYER_IDLE;
    g_latest_generation = 0;
    g_next_frame_deadline_us = 0;
    g_fade_start_us = 0;
    g_fade_duration_ms = 0U;
    g_state_before_prepare = ANIM_PLAYER_IDLE;
    anim_prepare_watchdog_reset(&g_prepare_watchdog);
    g_safe_error_pending = false;
    g_safe_error_visible = false;
    g_frame_timer_update_pending = false;
    g_frame_timer_should_run = false;
    anim_player_lifecycle_init(&g_lifecycle);
    anim_player_event_queue_init(&g_ticket_event_queue);
    g_ticket_event_dispatching = false;

    lvgl_port_lock(0);

    lv_obj_t *parent = lv_obj_get_parent(img_obj);
    bool direct_ready = ensure_direct_lcd_ready();
    if (direct_ready) {
        configure_anim_layer_for_direct_lcd(g_front_img);
    } else {
        configure_anim_layer(g_front_img);
    }
    if (g_back_img == NULL && parent != NULL) {
        g_back_img = lv_img_create(parent);
        lv_obj_set_pos(g_back_img, lv_obj_get_x(img_obj), lv_obj_get_y(img_obj));
        if (direct_ready) {
            configure_anim_layer_for_direct_lcd(g_back_img);
        } else {
            configure_anim_layer(g_back_img);
        }
    }
    hide_back_layer();

    if (g_frame_timer == NULL) {
        g_frame_timer = lv_timer_create(anim_frame_timer_cb, ANIM_TIMER_TICK_MS, NULL);
        lv_timer_pause(g_frame_timer);
    }
    if (g_service_timer == NULL) {
        g_service_timer = lv_timer_create(anim_service_timer_cb, ANIM_TIMER_TICK_MS, NULL);
    }

    lvgl_port_unlock();

    if (g_frame_timer == NULL || g_service_timer == NULL) {
        ESP_LOGE(TAG, "Animation LVGL timer creation failed");
        render_cold_start_error(img_obj);
        cleanup_failed_player_init();
        return -1;
    }

    if (ensure_worker_started() != 0) {
        ESP_LOGE(TAG, "Failed to start animation stream worker");
        render_cold_start_error(img_obj);
        cleanup_failed_player_init();
        return -1;
    }
    return 0;
}

static int emoji_anim_start_request_internal(const animation_request_t *request, animation_ticket_t ticket) {
    emoji_anim_type_t type;

    if (request == NULL) {
        return -1;
    }
    type = request->type;
    if (g_front_img == NULL) {
        ESP_LOGE(TAG, "Animation not initialized");
        return -1;
    }
    if (!anim_catalog_has_type(type)) {
        ESP_LOGW(TAG, "Animation type unavailable: %s", emoji_type_name(type));
        return -1;
    }
    if (ensure_worker_started() != 0) {
        return -1;
    }
    if (g_player_mutex == NULL || xSemaphoreTake(g_player_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }

    if (ticket != ANIMATION_TICKET_INVALID && (anim_player_event_queue_has_overflow_fault(&g_ticket_event_queue) ||
                                               anim_player_event_queue_free(&g_ticket_event_queue) < 2U)) {
        xSemaphoreGive(g_player_mutex);
        emoji_anim_player_dispatch_events();
        return EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL;
    }

    if (ticket == ANIMATION_TICKET_INVALID && g_active_playback.in_use && g_active_playback.type == type &&
        g_state == ANIM_PLAYER_PLAYING) {
        xSemaphoreGive(g_player_mutex);
        return 0;
    }
    if (ticket == ANIMATION_TICKET_INVALID && g_pending_playback.in_use && g_pending_playback.type == type) {
        xSemaphoreGive(g_player_mutex);
        return 0;
    }

    playback_cleanup(&g_pending_playback);
    if (g_state != ANIM_PLAYER_SWITCH_PREPARING) {
        g_state_before_prepare = g_state;
    }
    ++g_latest_generation;
    anim_player_lifecycle_events_t preparing_events =
        anim_player_lifecycle_begin_prepare(&g_lifecycle, g_latest_generation, ticket, (int32_t)type);
    g_requested_type = type;
    g_state = ANIM_PLAYER_SWITCH_PREPARING;
    if (!enqueue_lifecycle_events_locked(&preparing_events)) {
        restore_after_prepare_failure_locked();
        xSemaphoreGive(g_player_mutex);
        emoji_anim_player_dispatch_events();
        return EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL;
    }
    anim_prepare_watchdog_start(&g_prepare_watchdog, g_latest_generation, ticket,
                                (uint64_t)(esp_timer_get_time() / 1000LL), ANIM_PREPARE_STALL_TIMEOUT_MS);

    /* Adopt a matching prefetch even before its first frame is loaded. It already owns
     * the staging frame slots; opening a second playback here would exhaust the fixed
     * frame pool during the short prefetch/start race. */
    if (g_prefetch_playback.in_use && g_prefetch_playback.type == type) {
        g_pending_playback = g_prefetch_playback;
        g_pending_playback.generation_id = g_latest_generation;
        g_pending_playback.ticket = ticket;
        g_pending_playback.playback_mode = request->playback_mode;
        g_pending_playback.repeat_count = request->repeat_count;
        g_pending_playback.fade_in_ms = request->fade_in_ms;
        memset(&g_prefetch_playback, 0, sizeof(g_prefetch_playback));
        playback_reset_slots(&g_prefetch_playback);
        ESP_LOGI(TAG, "Animation switch uses prefetched frames: %s buffered=%d", emoji_type_name(type),
                 g_pending_playback.buffered_frames);
    } else {
        if (g_prefetch_playback.in_use && g_prefetch_playback.type != type) {
            playback_cleanup(&g_prefetch_playback);
        }
        int open_result = playback_open(&g_pending_playback, type, g_latest_generation, ticket, request->playback_mode,
                                        request->repeat_count, request->fade_in_ms);
        if (open_result != ANIM_PLAYBACK_OPEN_OK) {
            anim_player_failure_t failure = open_result == ANIM_PLAYBACK_OPEN_NO_MEMORY
                                                ? ANIM_PLAYER_FAILURE_NO_MEMORY
                                                : (open_result == ANIM_PLAYBACK_OPEN_INVALID_RESOURCE
                                                       ? ANIM_PLAYER_FAILURE_INVALID_RESOURCE
                                                       : (open_result == ANIM_PLAYBACK_OPEN_PACK_CORRUPT
                                                              ? ANIM_PLAYER_FAILURE_PACK_CORRUPT
                                                              : (open_result == ANIM_PLAYBACK_OPEN_SD_READ_FAILED
                                                                     ? ANIM_PLAYER_FAILURE_FRAME_READ
                                                                     : ANIM_PLAYER_FAILURE_STREAM_OPEN)));
            anim_player_lifecycle_events_t failed_events =
                anim_player_lifecycle_note_prepare_failure(&g_lifecycle, g_latest_generation, ticket, failure, 1U);
            restore_after_prepare_failure_locked();
            bool failure_queued = enqueue_lifecycle_events_locked(&failed_events);
            xSemaphoreGive(g_player_mutex);
            emoji_anim_player_dispatch_events();
            return failure_queued ? 0 : EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL;
        }
    }

    xSemaphoreGive(g_player_mutex);
    emoji_anim_player_dispatch_events();
    return 0;
}

int emoji_anim_start_request_with_ticket(const animation_request_t *request, animation_ticket_t ticket) {
    if (ticket == ANIMATION_TICKET_INVALID) {
        return -1;
    }
    return emoji_anim_start_request_internal(request, ticket);
}

static bool anim_wait_for_event_dispatch_idle(uint32_t timeout_ms) {
    uint32_t waited_ms = 0U;

    while (waited_ms < timeout_ms) {
        bool dispatch_idle = false;
        SemaphoreHandle_t mutex = g_player_mutex;
        if (mutex == NULL) {
            return true;
        }
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            dispatch_idle = !g_ticket_event_dispatching;
            xSemaphoreGive(mutex);
        }
        if (dispatch_idle) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
        waited_ms++;
    }
    return false;
}

int emoji_anim_player_set_event_sink(anim_player_ticket_event_sink_t sink, void *context) {
    SemaphoreHandle_t mutex = g_player_mutex;

    if (mutex == NULL) {
        g_ticket_event_sink = sink;
        g_ticket_event_sink_context = context;
    } else {
        if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
            return -1;
        }
        g_ticket_event_sink = sink;
        g_ticket_event_sink_context = context;
        xSemaphoreGive(mutex);
    }
    emoji_anim_player_dispatch_events();

    if (sink == NULL && mutex != NULL && !anim_wait_for_event_dispatch_idle(ANIM_EVENT_DISPATCH_EXIT_WAIT_MS)) {
        ESP_LOGE(TAG, "Timed out waiting for player event dispatch to stop");
        return -1;
    }
    return 0;
}

void emoji_anim_player_dispatch_events(void) {
    bool owns_dispatch = false;

    for (;;) {
        anim_player_ticket_event_t event;
        anim_player_ticket_event_sink_t sink;
        void *sink_context;
        SemaphoreHandle_t mutex = g_player_mutex;

        if (mutex == NULL || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        if (!owns_dispatch) {
            if (g_ticket_event_dispatching) {
                xSemaphoreGive(mutex);
                return;
            }
            g_ticket_event_dispatching = true;
            owns_dispatch = true;
        }

        sink = g_ticket_event_sink;
        sink_context = g_ticket_event_sink_context;
        if (sink == NULL || !anim_player_event_queue_pop(&g_ticket_event_queue, &event)) {
            g_ticket_event_dispatching = false;
            xSemaphoreGive(mutex);
            return;
        }
        xSemaphoreGive(mutex);

        sink(&event, sink_context);
    }
}

void emoji_anim_set_direct_lcd_protected_region(int x1, int y1, int x2, int y2) {
    x1 = max_int(0, min_int(x1, (int)ANIM_DIRECT_LCD_WIDTH));
    x2 = max_int(0, min_int(x2, (int)ANIM_DIRECT_LCD_WIDTH));
    y1 = max_int(0, min_int(y1, (int)ANIM_DIRECT_LCD_HEIGHT));
    y2 = max_int(0, min_int(y2, (int)ANIM_DIRECT_LCD_HEIGHT));

    portENTER_CRITICAL(&g_protected_region_lock);
    g_protected_region_x1 = x1;
    g_protected_region_y1 = y1;
    g_protected_region_x2 = x2;
    g_protected_region_y2 = y2;
    g_protected_region_active = x2 > x1 && y2 > y1;
    portEXIT_CRITICAL(&g_protected_region_lock);
}

void emoji_anim_clear_direct_lcd_protected_region(void) {
    portENTER_CRITICAL(&g_protected_region_lock);
    g_protected_region_active = false;
    g_protected_region_x1 = 0;
    g_protected_region_y1 = 0;
    g_protected_region_x2 = 0;
    g_protected_region_y2 = 0;
    portEXIT_CRITICAL(&g_protected_region_lock);
}

void emoji_anim_stop(void) {
    bool lvgl_locked = lvgl_port_lock(0);

    if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
        playback_cleanup(&g_active_playback);
        playback_cleanup(&g_pending_playback);
        playback_cleanup(&g_prefetch_playback);
        g_state = ANIM_PLAYER_IDLE;
        g_current_type = EMOJI_ANIM_NONE;
        g_requested_type = EMOJI_ANIM_NONE;
        g_next_frame_deadline_us = 0;
        g_state_before_prepare = ANIM_PLAYER_IDLE;
        anim_prepare_watchdog_reset(&g_prepare_watchdog);
        g_frame_timer_update_pending = false;
        g_frame_timer_should_run = false;
        xSemaphoreGive(g_player_mutex);
    } else {
        g_state = ANIM_PLAYER_IDLE;
        g_current_type = EMOJI_ANIM_NONE;
        g_requested_type = EMOJI_ANIM_NONE;
        g_next_frame_deadline_us = 0;
        g_state_before_prepare = ANIM_PLAYER_IDLE;
        anim_prepare_watchdog_reset(&g_prepare_watchdog);
        g_frame_timer_update_pending = false;
        g_frame_timer_should_run = false;
    }

    if (g_frame_timer != NULL) {
        lv_timer_pause(g_frame_timer);
    }
    hide_back_layer();

    if (lvgl_locked) {
        lvgl_port_unlock();
    }
}

int emoji_anim_deinit(void) {
    emoji_anim_stop();
    bool worker_stopped = anim_request_worker_shutdown();
    bool dispatch_stopped = anim_wait_for_event_dispatch_idle(ANIM_EVENT_DISPATCH_EXIT_WAIT_MS);
    if (!worker_stopped) {
        ESP_LOGE(TAG, "Player deinit blocked: worker exit acknowledgement missing");
    }
    if (!dispatch_stopped) {
        ESP_LOGW(TAG, "Player event dispatcher still active during deinit; retaining synchronization objects");
    }
    if (!worker_stopped || !dispatch_stopped) {
        return -1;
    }
    emoji_anim_clear_direct_lcd_protected_region();
    release_direct_lcd_buffers();
    lvgl_port_lock(0);
    if (g_frame_timer != NULL) {
        lv_timer_del(g_frame_timer);
        g_frame_timer = NULL;
    }
    if (g_service_timer != NULL) {
        lv_timer_del(g_service_timer);
        g_service_timer = NULL;
    }
    lvgl_port_unlock();

    if (g_worker_task == NULL && g_player_mutex != NULL) {
        vSemaphoreDelete(g_player_mutex);
        g_player_mutex = NULL;
    }
    if (g_worker_task == NULL && g_worker_exit_sem != NULL) {
        vSemaphoreDelete(g_worker_exit_sem);
        g_worker_exit_sem = NULL;
    }
    frame_pool_deinit();

    g_front_img = NULL;
    g_back_img = NULL;
    g_current_type = EMOJI_ANIM_NONE;
    g_requested_type = EMOJI_ANIM_NONE;
    g_state = ANIM_PLAYER_IDLE;
    g_latest_generation = 0;
    g_next_frame_deadline_us = 0;
    g_fade_start_us = 0;
    g_fade_duration_ms = 0U;
    g_state_before_prepare = ANIM_PLAYER_IDLE;
    anim_prepare_watchdog_reset(&g_prepare_watchdog);
    g_safe_error_pending = false;
    g_safe_error_visible = false;
    g_frame_timer_update_pending = false;
    g_frame_timer_should_run = false;
    g_worker_shutdown_requested = false;
    return 0;
}

int emoji_anim_prefetch_type(emoji_anim_type_t type) {
    if (!anim_catalog_has_type(type)) {
        return -1;
    }
    if (ensure_worker_started() != 0) {
        return -1;
    }
    if (g_player_mutex == NULL || xSemaphoreTake(g_player_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return -1;
    }

    if ((g_active_playback.in_use && g_active_playback.type == type) ||
        (g_pending_playback.in_use && g_pending_playback.type == type) ||
        (g_prefetch_playback.in_use && g_prefetch_playback.type == type)) {
        xSemaphoreGive(g_player_mutex);
        return 0;
    }

    playback_cleanup(&g_prefetch_playback);
    ++g_latest_generation;
    if (playback_open(&g_prefetch_playback, type, g_latest_generation, ANIMATION_TICKET_INVALID,
                      ANIM_PLAYBACK_RESOURCE_DEFAULT, 0U, 0U) != 0) {
        xSemaphoreGive(g_player_mutex);
        return -1;
    }

    ESP_LOGI(TAG, "Animation prefetch queued: %s", emoji_type_name(type));
    xSemaphoreGive(g_player_mutex);
    return 0;
}
