/**
 * @file anim_player.c
 * @brief SD-backed animation player with ring-buffered stream playback.
 */

#include "anim_player.h"

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
#define ANIM_DIRECT_LCD_STRIP_SRC_ROWS 4U
#define ANIM_DIRECT_LCD_STRIP_DST_ROWS (ANIM_DIRECT_LCD_STRIP_SRC_ROWS * ANIM_DIRECT_LCD_SCALE)
#define ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS 50U
#define ANIM_TIMER_TICK_MS 5U
#define ANIM_WORKER_IDLE_MS 1U
#define ANIM_WORKER_TASK_PRIORITY 6
#define ANIM_SWITCH_PREFILL_FRAMES 3
#define ANIM_PERF_LOG_FRAME_INTERVAL 10U
#define ANIM_INVALID_SLOT_FRAME (-1)

typedef enum {
    ANIM_PLAYER_IDLE = 0,
    ANIM_PLAYER_SWITCH_PREPARING,
    ANIM_PLAYER_PLAYING,
} anim_player_state_t;

typedef struct {
    bool in_use;
    emoji_anim_type_t type;
    uint32_t generation_id;
    anim_stream_t stream;
    anim_frame_buffer_t slots[WATCHER_ANIM_RING_FRAMES];
    int slot_frame_index[WATCHER_ANIM_RING_FRAMES];
    int slot_count;
    int current_slot;
    int current_frame_index;
    int buffered_frames;
    int next_frame_to_load;
    int next_slot_to_load;
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
    bool target_pending;
    emoji_anim_type_t type;
    uint32_t generation_id;
    int frame_index;
    int slot_index;
} anim_load_job_t;

static lv_obj_t *g_front_img = NULL;
static lv_obj_t *g_back_img = NULL;
static lv_timer_t *g_frame_timer = NULL;
static lv_timer_t *g_service_timer = NULL;
static TaskHandle_t g_worker_task = NULL;
static SemaphoreHandle_t g_player_mutex = NULL;

static anim_player_state_t g_state = ANIM_PLAYER_IDLE;
static anim_playback_t g_active_playback = {0};
static anim_playback_t g_pending_playback = {0};
static anim_frame_buffer_t g_static_frame = {0};
static emoji_anim_type_t g_current_type = EMOJI_ANIM_NONE;
static emoji_anim_type_t g_requested_type = EMOJI_ANIM_NONE;
static uint32_t g_latest_generation = 0;
static uint32_t g_override_interval_ms = 0;
static int64_t g_next_frame_deadline_us = 0;
static esp_lcd_panel_handle_t g_direct_panel = NULL;
static uint16_t *g_direct_strip[2] = {NULL, NULL};
static size_t g_direct_strip_pixels = 0;
static int g_direct_next_strip = 0;
static bool g_direct_lcd_ready = false;

static void anim_worker_task(void *param);
static void anim_frame_timer_cb(lv_timer_t *timer);
static void anim_service_timer_cb(lv_timer_t *timer);

static int min_int(int lhs, int rhs) {
    return lhs < rhs ? lhs : rhs;
}

static int effective_delay_ms(anim_playback_t *playback, int frame_index) {
    if (g_override_interval_ms > 0) {
        return (int)g_override_interval_ms;
    }

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
    for (int index = 0; index < 2; ++index) {
        if (g_direct_strip[index] == NULL) {
            g_direct_strip[index] = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (g_direct_strip[index] == NULL) {
                ESP_LOGW(TAG, "Direct LCD strip allocation failed: index=%d bytes=%u", index, (unsigned)bytes);
                for (int free_index = 0; free_index < 2; ++free_index) {
                    if (g_direct_strip[free_index] != NULL) {
                        heap_caps_free(g_direct_strip[free_index]);
                        g_direct_strip[free_index] = NULL;
                    }
                }
                g_direct_strip_pixels = 0;
                return false;
            }
        }
    }

    g_direct_next_strip = 0;
    g_direct_lcd_ready = true;
    ESP_LOGI(TAG, "Direct LCD animation path enabled: %ux%u strip_rows=%u buffers=%u bytes_each=%u",
             (unsigned)ANIM_DIRECT_LCD_WIDTH, (unsigned)ANIM_DIRECT_LCD_HEIGHT,
             (unsigned)ANIM_DIRECT_LCD_STRIP_DST_ROWS, 2U, (unsigned)bytes);
    return true;
}

static void configure_anim_layer_for_direct_lcd(lv_obj_t *img_obj) {
    if (img_obj == NULL) {
        return;
    }

    lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(img_obj, LV_OPA_TRANSP, 0);
}

static bool draw_frame_direct_lcd(const lv_img_dsc_t *frame) {
    if (!g_direct_lcd_ready || frame == NULL || frame->data == NULL || frame->header.w != ANIM_SOURCE_FRAME_SIZE ||
        frame->header.h != ANIM_SOURCE_FRAME_SIZE) {
        return false;
    }

    int overlay_x1 = 0;
    int overlay_y1 = 0;
    int overlay_x2 = 0;
    int overlay_y2 = 0;
    bool has_overlay = hal_display_get_text_overlay_bounds(&overlay_x1, &overlay_y1, &overlay_x2, &overlay_y2);
    (void)overlay_x1;
    (void)overlay_x2;

    const uint16_t *src = (const uint16_t *)frame->data;
    for (uint32_t src_y = 0; src_y < ANIM_SOURCE_FRAME_SIZE; src_y += ANIM_DIRECT_LCD_STRIP_SRC_ROWS) {
        uint32_t src_rows = ANIM_DIRECT_LCD_STRIP_SRC_ROWS;
        if (src_y + src_rows > ANIM_SOURCE_FRAME_SIZE) {
            src_rows = ANIM_SOURCE_FRAME_SIZE - src_y;
        }
        uint32_t dst_rows = src_rows * ANIM_DIRECT_LCD_SCALE;
        uint16_t *dst = g_direct_strip[g_direct_next_strip];
        g_direct_next_strip = (g_direct_next_strip + 1) % 2;

        for (uint32_t row = 0; row < src_rows; ++row) {
            const uint16_t *src_row = src + ((src_y + row) * ANIM_SOURCE_FRAME_SIZE);
            uint16_t *dst_row0 = dst + ((row * ANIM_DIRECT_LCD_SCALE) * ANIM_DIRECT_LCD_WIDTH);
            uint16_t *dst_row1 = dst_row0 + ANIM_DIRECT_LCD_WIDTH;
            for (uint32_t x = 0; x < ANIM_SOURCE_FRAME_SIZE; ++x) {
                uint16_t color = src_row[x];
                uint32_t dst_x = x * ANIM_DIRECT_LCD_SCALE;
                dst_row0[dst_x] = color;
                dst_row0[dst_x + 1] = color;
            }
            memcpy(dst_row1, dst_row0, ANIM_DIRECT_LCD_WIDTH * sizeof(uint16_t));
        }

        int strip_y1 = (int)(src_y * ANIM_DIRECT_LCD_SCALE);
        int strip_y2 = strip_y1 + (int)dst_rows;
        if (!has_overlay || overlay_y2 <= strip_y1 || overlay_y1 >= strip_y2) {
            esp_err_t rc = lvgl_port_panel_draw_bitmap(g_direct_panel, 0, strip_y1, ANIM_DIRECT_LCD_WIDTH, strip_y2,
                                                       dst, ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "Direct LCD draw failed: %s", esp_err_to_name(rc));
                return false;
            }
            continue;
        }

        int top_y2 = overlay_y1 < strip_y2 ? overlay_y1 : strip_y2;
        if (strip_y1 < top_y2) {
            esp_err_t rc = lvgl_port_panel_draw_bitmap(g_direct_panel, 0, strip_y1, ANIM_DIRECT_LCD_WIDTH, top_y2, dst,
                                                       ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "Direct LCD draw top failed: %s", esp_err_to_name(rc));
                return false;
            }
        }

        int bottom_y1 = overlay_y2 > strip_y1 ? overlay_y2 : strip_y1;
        if (bottom_y1 < strip_y2) {
            uint16_t *bottom = dst + ((size_t)(bottom_y1 - strip_y1) * ANIM_DIRECT_LCD_WIDTH);
            esp_err_t rc = lvgl_port_panel_draw_bitmap(g_direct_panel, 0, bottom_y1, ANIM_DIRECT_LCD_WIDTH, strip_y2,
                                                       bottom, ANIM_DIRECT_LCD_DRAW_TIMEOUT_MS);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "Direct LCD draw bottom failed: %s", esp_err_to_name(rc));
                return false;
            }
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

static void playback_reset_slots(anim_playback_t *playback) {
    for (int index = 0; index < WATCHER_ANIM_RING_FRAMES; ++index) {
        playback->slot_frame_index[index] = ANIM_INVALID_SLOT_FRAME;
    }
}

static void playback_cleanup(anim_playback_t *playback) {
    if (playback == NULL) {
        return;
    }

    anim_stream_close(&playback->stream);
    for (int index = 0; index < WATCHER_ANIM_RING_FRAMES; ++index) {
        anim_frame_buffer_free(&playback->slots[index]);
    }
    memset(playback, 0, sizeof(*playback));
    playback_reset_slots(playback);
}

static int playback_open(anim_playback_t *playback, emoji_anim_type_t type, uint32_t generation_id) {
    memset(playback, 0, sizeof(*playback));
    playback_reset_slots(playback);

    if (anim_stream_open(type, &playback->stream) != 0) {
        return -1;
    }

    playback->slot_count =
        min_int(WATCHER_ANIM_RING_FRAMES, playback->stream.frame_count > 0 ? playback->stream.frame_count : 1);
    playback->in_use = true;
    playback->type = type;
    playback->generation_id = generation_id;
    playback->current_slot = 0;
    playback->current_frame_index = 0;
    playback->buffered_frames = 0;
    playback->next_frame_to_load = 0;
    playback->next_slot_to_load = 0;

    for (int index = 0; index < playback->slot_count; ++index) {
        if (anim_frame_buffer_init(&playback->slots[index], playback->stream.info.width,
                                   playback->stream.info.height) != 0) {
            playback_cleanup(playback);
            return -1;
        }
    }

    return 0;
}

static bool playback_prepare_load_job(anim_playback_t *playback, bool target_pending, anim_load_job_t *job) {
    if (job == NULL) {
        return false;
    }
    memset(job, 0, sizeof(*job));

    if (playback == NULL || !playback->in_use || playback->buffered_frames >= playback->slot_count) {
        return false;
    }

    int frame_count = playback->stream.frame_count;
    if (frame_count <= 0) {
        return false;
    }

    int frame_index = playback->next_frame_to_load;
    if (frame_index >= frame_count) {
        if (!playback->stream.info.loop) {
            return false;
        }
        frame_index %= frame_count;
    }

    job->valid = true;
    job->target_pending = target_pending;
    job->type = playback->type;
    job->generation_id = playback->generation_id;
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
        playback->buffered_frames >= playback->slot_count || playback->next_slot_to_load != job->slot_index) {
        return false;
    }

    anim_load_job_t expected_job = {0};
    if (!playback_prepare_load_job(playback, job->target_pending, &expected_job) ||
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
    playback_note_read_frame(playback, read_us);
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

static const lv_img_dsc_t *playback_current_descriptor(anim_playback_t *playback) {
    if (playback == NULL || !playback->in_use || playback->current_slot < 0) {
        return NULL;
    }
    return &playback->slots[playback->current_slot].img_dsc;
}

static int ensure_worker_started(void) {
    if (g_player_mutex == NULL) {
        g_player_mutex = xSemaphoreCreateMutex();
        if (g_player_mutex == NULL) {
            return -1;
        }
    }

    if (g_worker_task == NULL) {
        BaseType_t rc =
            xTaskCreate(anim_worker_task, "anim_stream", 6144, NULL, ANIM_WORKER_TASK_PRIORITY, &g_worker_task);
        if (rc != pdPASS) {
            g_worker_task = NULL;
            return -1;
        }
    }

    return 0;
}

static int commit_pending_playback(void) {
    if (!g_pending_playback.in_use || g_pending_playback.buffered_frames <= 0) {
        return -1;
    }

    if (playback_current_descriptor(&g_pending_playback) == NULL) {
        return -1;
    }

    playback_cleanup(&g_active_playback);
    g_active_playback = g_pending_playback;
    memset(&g_pending_playback, 0, sizeof(g_pending_playback));
    playback_reset_slots(&g_pending_playback);

    const lv_img_dsc_t *active_frame = playback_current_descriptor(&g_active_playback);
    if (active_frame == NULL) {
        playback_cleanup(&g_active_playback);
        g_state = ANIM_PLAYER_IDLE;
        g_current_type = EMOJI_ANIM_NONE;
        g_requested_type = EMOJI_ANIM_NONE;
        return -1;
    }

    int64_t initial_draw_us = -1;
    bool initial_draw_failed = false;
    if (g_front_img != NULL) {
        if (g_direct_lcd_ready) {
            configure_anim_layer_for_direct_lcd(g_front_img);
            int64_t draw_started_us = esp_timer_get_time();
            if (draw_frame_direct_lcd(active_frame)) {
                initial_draw_us = esp_timer_get_time() - draw_started_us;
            } else {
                initial_draw_failed = true;
            }
        } else {
            lv_img_set_src(g_front_img, active_frame);
            lv_obj_set_style_opa(g_front_img, LV_OPA_COVER, 0);
        }
    }
    hide_back_layer();

    g_state = ANIM_PLAYER_PLAYING;
    g_current_type = g_active_playback.type;
    g_requested_type = g_active_playback.type;
    int64_t now_us = esp_timer_get_time();
    playback_reset_perf_stats(&g_active_playback, now_us);
    if (initial_draw_us >= 0) {
        playback_note_draw_frame(&g_active_playback, initial_draw_us);
    } else if (initial_draw_failed) {
        playback_note_draw_failure(&g_active_playback);
    }
    g_next_frame_deadline_us =
        now_us + (int64_t)effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index) * 1000LL;
    if (g_frame_timer != NULL && g_active_playback.stream.frame_count > 1) {
        lv_timer_resume(g_frame_timer);
    } else if (g_frame_timer != NULL) {
        lv_timer_pause(g_frame_timer);
    }

    ESP_LOGI(TAG, "Animation switch committed: %s frames=%d loop_ms=%d ring=%d", emoji_type_name(g_current_type),
             g_active_playback.stream.frame_count, playback_loop_duration_ms(&g_active_playback),
             g_active_playback.slot_count);
    return 0;
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
            if (g_pending_playback.in_use && g_pending_playback.buffered_frames < g_pending_playback.slot_count) {
                did_work = playback_prepare_load_job(&g_pending_playback, true, &job);
            } else if (g_active_playback.in_use && g_active_playback.buffered_frames < g_active_playback.slot_count) {
                did_work = playback_prepare_load_job(&g_active_playback, false, &job);
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
            if (anim_stream_open(job.type, &worker_stream) != 0) {
                ESP_LOGW(TAG, "Failed to open worker stream for %s", emoji_type_name(job.type));
                vTaskDelay(pdMS_TO_TICKS(ANIM_WORKER_IDLE_MS));
                continue;
            }
            worker_stream_type = job.type;
        }

        int64_t read_started_us = esp_timer_get_time();
        if (anim_stream_read_frame(&worker_stream, job.frame_index, &worker_frame) != 0) {
            ESP_LOGW(TAG, "Failed to read frame %d for %s", job.frame_index, emoji_type_name(job.type));
            anim_stream_close(&worker_stream);
            worker_stream_type = EMOJI_ANIM_NONE;
            vTaskDelay(pdMS_TO_TICKS(ANIM_WORKER_IDLE_MS));
            continue;
        }
        int64_t read_us = esp_timer_get_time() - read_started_us;

        if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
            anim_playback_t *target = job.target_pending ? &g_pending_playback : &g_active_playback;
            (void)playback_commit_loaded_frame(target, &job, &worker_frame, read_us);
            xSemaphoreGive(g_player_mutex);
        }
    }
}

static void anim_frame_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (g_state != ANIM_PLAYER_PLAYING || g_current_type == EMOJI_ANIM_NONE || g_player_mutex == NULL) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us < g_next_frame_deadline_us) {
        return;
    }

    if (xSemaphoreTake(g_player_mutex, 0) != pdTRUE) {
        return;
    }

    const lv_img_dsc_t *latest_frame = NULL;
    int64_t due_us = g_next_frame_deadline_us;
    if (!playback_advance(&g_active_playback)) {
        playback_note_underrun(&g_active_playback);
        int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
        g_next_frame_deadline_us = now_us + (int64_t)delay_ms * 1000LL;
    } else {
        latest_frame = playback_current_descriptor(&g_active_playback);
        g_current_type = g_active_playback.type;
        int delay_ms = effective_delay_ms(&g_active_playback, g_active_playback.current_frame_index);
        int64_t delay_us = (int64_t)delay_ms * 1000LL;
        int64_t late_us = now_us - due_us;
        g_next_frame_deadline_us = late_us > delay_us ? now_us + delay_us : due_us + delay_us;
        playback_note_displayed_frame(&g_active_playback, now_us, late_us);
    }

    if (latest_frame != NULL && g_front_img != NULL) {
        int64_t draw_started_us = esp_timer_get_time();
        if (g_direct_lcd_ready) {
            if (draw_frame_direct_lcd(latest_frame)) {
                playback_note_draw_frame(&g_active_playback, esp_timer_get_time() - draw_started_us);
            } else {
                playback_note_draw_failure(&g_active_playback);
            }
        } else {
            lv_img_set_src(g_front_img, latest_frame);
        }
    }

    xSemaphoreGive(g_player_mutex);
}

static void anim_service_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (g_state != ANIM_PLAYER_SWITCH_PREPARING || g_player_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_player_mutex, 0) != pdTRUE) {
        return;
    }

    int required_frames = min_int(ANIM_SWITCH_PREFILL_FRAMES, g_pending_playback.slot_count);
    if (g_pending_playback.in_use && g_pending_playback.buffered_frames >= required_frames) {
        commit_pending_playback();
    }

    xSemaphoreGive(g_player_mutex);
}

int emoji_anim_init(lv_obj_t *img_obj) {
    if (img_obj == NULL) {
        ESP_LOGE(TAG, "Invalid image object");
        return -1;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGW(TAG, "Animation catalog init failed");
    }

    g_front_img = img_obj;
    g_current_type = EMOJI_ANIM_NONE;
    g_requested_type = EMOJI_ANIM_NONE;
    g_state = ANIM_PLAYER_IDLE;
    g_latest_generation = 0;
    g_next_frame_deadline_us = 0;

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

    if (ensure_worker_started() != 0) {
        ESP_LOGW(TAG, "Failed to start animation stream worker");
    }
    return 0;
}

int emoji_anim_start(emoji_anim_type_t type) {
    if (g_front_img == NULL) {
        ESP_LOGE(TAG, "Animation not initialized");
        return -1;
    }
    if (!anim_catalog_has_type(type)) {
        ESP_LOGW(TAG, "Animation type unavailable: %s", emoji_type_name(type));
        return -1;
    }
    if (g_player_mutex == NULL && ensure_worker_started() != 0) {
        return -1;
    }
    if (g_player_mutex == NULL || xSemaphoreTake(g_player_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }

    if (g_active_playback.in_use && g_active_playback.type == type && g_state == ANIM_PLAYER_PLAYING) {
        xSemaphoreGive(g_player_mutex);
        return 0;
    }
    if (g_pending_playback.in_use && g_pending_playback.type == type) {
        xSemaphoreGive(g_player_mutex);
        return 0;
    }

    playback_cleanup(&g_pending_playback);
    ++g_latest_generation;

    if (playback_open(&g_pending_playback, type, g_latest_generation) != 0) {
        xSemaphoreGive(g_player_mutex);
        return -1;
    }

    g_requested_type = type;
    g_state = ANIM_PLAYER_SWITCH_PREPARING;

    xSemaphoreGive(g_player_mutex);
    return 0;
}

void emoji_anim_stop(void) {
    if (g_player_mutex != NULL && xSemaphoreTake(g_player_mutex, portMAX_DELAY) == pdTRUE) {
        playback_cleanup(&g_active_playback);
        playback_cleanup(&g_pending_playback);
        xSemaphoreGive(g_player_mutex);
    }

    if (g_frame_timer != NULL) {
        lv_timer_pause(g_frame_timer);
    }
    hide_back_layer();
    g_state = ANIM_PLAYER_IDLE;
    g_current_type = EMOJI_ANIM_NONE;
    g_requested_type = EMOJI_ANIM_NONE;
    g_next_frame_deadline_us = 0;
}

bool emoji_anim_is_running(void) {
    return g_state == ANIM_PLAYER_PLAYING && g_current_type != EMOJI_ANIM_NONE;
}

bool emoji_anim_is_switch_pending(void) {
    return g_state == ANIM_PLAYER_SWITCH_PREPARING;
}

emoji_anim_type_t emoji_anim_get_type(void) {
    return g_current_type;
}

void emoji_anim_set_interval(uint32_t interval_ms) {
    g_override_interval_ms = interval_ms;
}

int emoji_anim_show_static(emoji_anim_type_t type, int frame) {
    if (g_front_img == NULL) {
        return -1;
    }

    emoji_anim_stop();
    if (anim_load_static_frame(type, frame, &g_static_frame) != 0) {
        return -1;
    }

    lv_img_set_src(g_front_img, &g_static_frame.img_dsc);
    lv_obj_set_style_opa(g_front_img, LV_OPA_COVER, 0);
    hide_back_layer();
    g_current_type = type;
    return 0;
}

int emoji_anim_prefetch_type(emoji_anim_type_t type) {
    return anim_catalog_has_type(type) ? 0 : -1;
}

int emoji_anim_get_fps(void) {
    uint32_t interval_ms = g_override_interval_ms > 0 ? g_override_interval_ms : EMOJI_ANIM_INTERVAL_MS;
    return interval_ms > 0 ? (int)(1000U / interval_ms) : 0;
}

void emoji_anim_set_fps(int fps) {
    if (fps < 1) {
        fps = 1;
    } else if (fps > 60) {
        fps = 60;
    }
    emoji_anim_set_interval((uint32_t)(1000 / fps));
}
