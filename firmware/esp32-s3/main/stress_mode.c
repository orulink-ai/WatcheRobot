#include "stress_mode.h"

#include "control_ingress.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mcu_link_bootstrap.h"

#include <string.h>

#define TAG "STRESS_MODE"
#define MCU_OBS_TAG "MCU_OBS"

#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)

#if defined(CONFIG_WATCHER_STRESS_SERVO_PERIOD_MS)
#define WATCHER_STRESS_SERVO_PERIOD_VALUE CONFIG_WATCHER_STRESS_SERVO_PERIOD_MS
#else
#define WATCHER_STRESS_SERVO_PERIOD_VALUE 200
#endif

#if defined(CONFIG_WATCHER_STRESS_STATS_PERIOD_MS)
#define WATCHER_STRESS_STATS_PERIOD_VALUE CONFIG_WATCHER_STRESS_STATS_PERIOD_MS
#else
#define WATCHER_STRESS_STATS_PERIOD_VALUE 5000
#endif

#define WATCHER_STRESS_TASK_TICK_MS 10
#define WATCHER_STRESS_ACTIVE_DURATION_MS 595000
#define WATCHER_STRESS_MAX_INFLIGHT 8
#define WATCHER_STRESS_ACTIVE_WINDOW 1
#define WATCHER_STRESS_DRIVER_TASK_PERIOD_MS 2
#define WATCHER_STRESS_READY_SETTLE_MS 1000

typedef struct {
    uint32_t servo_submit_count;
    uint32_t motion_ack_count;
    uint32_t motion_done_count;
    uint32_t touch_rx_count;
    uint32_t mag_rx_count;
    uint32_t imu_rx_count;
    uint32_t ack_timeout_count;
    uint32_t crc_error_count;
    uint32_t dropped_state_count;
    uint32_t reconnect_count;
    uint32_t motion_done_fault_count;
} stress_stats_t;

static portMUX_TYPE s_stress_lock = portMUX_INITIALIZER_UNLOCKED;
static stress_stats_t s_stats = {0};
static stress_stats_t s_last_logged_stats = {0};
static int64_t s_last_stats_log_us = 0;
static bool s_waiting_for_motion_ack = false;
static int64_t s_ready_since_us = 0;
static int64_t s_next_submit_us = 0;
static TaskHandle_t s_stress_task = NULL;
typedef struct {
    uint32_t ref_seq;
    bool active;
    bool awaiting_ack;
} stress_inflight_t;
static stress_inflight_t s_inflight[WATCHER_STRESS_MAX_INFLIGHT] = {0};
static const int s_x_track[] = {60, 90, 120};
static const int s_y_track[] = {100, 120, 140};
static size_t s_track_index = 0u;

static stress_inflight_t *stress_find_inflight(uint32_t ref_seq) {
    size_t index;

    for (index = 0u; index < WATCHER_STRESS_MAX_INFLIGHT; ++index) {
        if (s_inflight[index].active && s_inflight[index].ref_seq == ref_seq) {
            return &s_inflight[index];
        }
    }

    return NULL;
}

static bool stress_has_free_inflight_slot(void) {
    size_t index;

    for (index = 0u; index < WATCHER_STRESS_MAX_INFLIGHT; ++index) {
        if (!s_inflight[index].active) {
            return true;
        }
    }

    return false;
}

static bool stress_track_inflight(uint32_t ref_seq) {
    size_t index;

    for (index = 0u; index < WATCHER_STRESS_MAX_INFLIGHT; ++index) {
        if (!s_inflight[index].active) {
            s_inflight[index].ref_seq = ref_seq;
            s_inflight[index].active = true;
            s_inflight[index].awaiting_ack = true;
            return true;
        }
    }

    return false;
}

static size_t stress_active_inflight_count(void) {
    size_t index;
    size_t active_count = 0u;

    for (index = 0u; index < WATCHER_STRESS_MAX_INFLIGHT; ++index) {
        if (s_inflight[index].active) {
            active_count++;
        }
    }

    return active_count;
}

static void stress_reset_driver_state_locked(void) {
    s_waiting_for_motion_ack = false;
    s_ready_since_us = 0;
    s_next_submit_us = 0;
    memset(s_inflight, 0, sizeof(s_inflight));
}

static void stress_snapshot_link_stats(stress_stats_t *snapshot) {
    mcu_link_t *link = mcu_link_bootstrap_get_link();
    mcu_link_stats_t link_stats = {0};

    if (snapshot == NULL || link == NULL || mcu_link_copy_stats(link, &link_stats) != ESP_OK) {
        return;
    }

    snapshot->ack_timeout_count = link_stats.ack_timeout_count;
    snapshot->crc_error_count = link_stats.crc_error_count;
    snapshot->dropped_state_count = link_stats.dropped_state_count;
    snapshot->reconnect_count = link_stats.reconnect_count;
    snapshot->motion_done_fault_count = link_stats.motion_done_fault_count;
}

static void stress_copy_stats(stress_stats_t *out_stats) {
    if (out_stats == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_stress_lock);
    *out_stats = s_stats;
    portEXIT_CRITICAL(&s_stress_lock);
    stress_snapshot_link_stats(out_stats);
}

static void stress_log_stats(const char *reason, bool force) {
    stress_stats_t snapshot = {0};
    bool changed;
    bool periodic_due;
    int64_t now_us = esp_timer_get_time();

    stress_copy_stats(&snapshot);
    changed = memcmp(&snapshot, &s_last_logged_stats, sizeof(snapshot)) != 0;
    periodic_due = s_last_stats_log_us == 0 ||
                   ((now_us - s_last_stats_log_us) >= ((int64_t)WATCHER_STRESS_STATS_PERIOD_VALUE * 1000LL));

    if (!force && !changed && !periodic_due) {
        return;
    }

    if (!force && reason != NULL && strcmp(reason, "motion_reject") == 0) {
        force = true;
    }

    if (!force && !periodic_due) {
        return;
    }

    ESP_LOGI(MCU_OBS_TAG,
             "evt=stress_stats reason=%s servo_submit_count=%lu motion_ack_count=%lu motion_done_count=%lu "
             "touch_rx_count=%lu mag_rx_count=%lu imu_rx_count=%lu ack_timeout_count=%lu crc_error_count=%lu "
             "dropped_state_count=%lu reconnect_count=%lu motion_done_fault_count=%lu",
             reason ? reason : "periodic", (unsigned long)snapshot.servo_submit_count,
             (unsigned long)snapshot.motion_ack_count, (unsigned long)snapshot.motion_done_count,
             (unsigned long)snapshot.touch_rx_count, (unsigned long)snapshot.mag_rx_count,
             (unsigned long)snapshot.imu_rx_count, (unsigned long)snapshot.ack_timeout_count,
             (unsigned long)snapshot.crc_error_count, (unsigned long)snapshot.dropped_state_count,
             (unsigned long)snapshot.reconnect_count, (unsigned long)snapshot.motion_done_fault_count);

    s_last_logged_stats = snapshot;
    s_last_stats_log_us = now_us;
}

static bool stress_submit_next_servo_command(void) {
    control_servo_request_t request = {
        .has_x = true,
        .has_y = true,
        .x_deg = s_x_track[s_track_index % 3u],
        .y_deg = s_y_track[s_track_index % 3u],
        .duration_ms = 180,
        .source = CONTROL_MOTION_SOURCE_STRESS,
    };
    uint32_t command_seq = 0u;
    esp_err_t ret;

    portENTER_CRITICAL(&s_stress_lock);
    if (!stress_has_free_inflight_slot()) {
        portEXIT_CRITICAL(&s_stress_lock);
        ESP_LOGW(TAG, "stress inflight table full; skip submit");
        return false;
    }
    portEXIT_CRITICAL(&s_stress_lock);

    ret = control_ingress_submit_servo_with_seq(&request, &command_seq);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "stress servo submit failed: %s", esp_err_to_name(ret));
        return false;
    }

    portENTER_CRITICAL(&s_stress_lock);
    if (!stress_track_inflight(command_seq)) {
        portEXIT_CRITICAL(&s_stress_lock);
        ESP_LOGW(TAG, "stress inflight table full; seq=%lu", (unsigned long)command_seq);
        return false;
    }
    s_stats.servo_submit_count++;
    s_waiting_for_motion_ack = true;
    portEXIT_CRITICAL(&s_stress_lock);
    s_track_index++;
    return true;
}

static void stress_drive_once(void) {
    bool ready = mcu_link_bootstrap_is_ready();
    int64_t now_us = esp_timer_get_time();
    bool within_active_window;
    bool due_now = false;
    size_t active_inflight = 0u;

    portENTER_CRITICAL(&s_stress_lock);
    if (!ready) {
        if (s_ready_since_us != 0 || s_waiting_for_motion_ack) {
            stress_reset_driver_state_locked();
        }
        portEXIT_CRITICAL(&s_stress_lock);
        return;
    }

    if (s_ready_since_us == 0) {
        s_ready_since_us = now_us;
        s_next_submit_us = now_us + ((int64_t)WATCHER_STRESS_READY_SETTLE_MS * 1000LL);
    }

    active_inflight = stress_active_inflight_count();
    within_active_window = (now_us - s_ready_since_us) < ((int64_t)WATCHER_STRESS_ACTIVE_DURATION_MS * 1000LL);
    due_now = within_active_window && active_inflight < WATCHER_STRESS_ACTIVE_WINDOW && s_next_submit_us != 0 &&
              now_us >= s_next_submit_us;
    portEXIT_CRITICAL(&s_stress_lock);

    if (due_now && stress_submit_next_servo_command()) {
        int64_t scheduled_next_us;

        stress_log_stats("submit", false);
        portENTER_CRITICAL(&s_stress_lock);
        scheduled_next_us = s_next_submit_us + ((int64_t)WATCHER_STRESS_SERVO_PERIOD_VALUE * 1000LL);
        now_us = esp_timer_get_time();
        while (scheduled_next_us <= now_us) {
            scheduled_next_us += ((int64_t)WATCHER_STRESS_SERVO_PERIOD_VALUE * 1000LL);
        }
        s_next_submit_us = scheduled_next_us;
        portEXIT_CRITICAL(&s_stress_lock);
    }
}

static void stress_mode_task(void *arg) {
    (void)arg;

    while (true) {
        stress_mode_tick();
        vTaskDelay(pdMS_TO_TICKS(WATCHER_STRESS_DRIVER_TASK_PERIOD_MS));
    }
}

void stress_mode_init(void) {
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_last_logged_stats, 0, sizeof(s_last_logged_stats));
    s_last_stats_log_us = 0;
    s_waiting_for_motion_ack = false;
    s_ready_since_us = 0;
    s_next_submit_us = 0;
    memset(s_inflight, 0, sizeof(s_inflight));
    s_track_index = 0u;

    stress_log_stats("init", true);
}

void stress_mode_start(void) {
    if (s_stress_task != NULL) {
        return;
    }

    if (xTaskCreate(stress_mode_task, "stress_mode", 4096, NULL, 5, &s_stress_task) != pdPASS) {
        s_stress_task = NULL;
        ESP_LOGE(TAG, "Failed to create stress mode task");
    }
}

void stress_mode_notify_ready(void) {
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_stress_lock);
    if (s_ready_since_us == 0) {
        s_ready_since_us = now_us;
    }
    if (s_next_submit_us == 0 || s_next_submit_us > now_us) {
        s_next_submit_us = now_us + ((int64_t)WATCHER_STRESS_READY_SETTLE_MS * 1000LL);
    }
    portEXIT_CRITICAL(&s_stress_lock);
}

void stress_mode_on_link_event(const mcu_link_event_t *event) {
    uint32_t ref_seq = 0u;
    stress_inflight_t *entry = NULL;
    const char *log_reason = NULL;
    bool log_drain_complete = false;
    int64_t now_us = esp_timer_get_time();

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case MCU_LINK_RX_EVENT_ACK:
    case MCU_LINK_RX_EVENT_NACK:
    case MCU_LINK_RX_EVENT_FAULT:
    case MCU_LINK_RX_EVENT_MOTION_DONE:
        ref_seq = ((uint32_t)event->frame.payload[0]) | ((uint32_t)event->frame.payload[1] << 8u) |
                  ((uint32_t)event->frame.payload[2] << 16u) | ((uint32_t)event->frame.payload[3] << 24u);
        break;
    default:
        break;
    }

    portENTER_CRITICAL(&s_stress_lock);
    if (ref_seq != 0u) {
        entry = stress_find_inflight(ref_seq);
    }
    switch (event->type) {
    case MCU_LINK_RX_EVENT_ACK:
        if (entry != NULL && entry->awaiting_ack) {
            s_stats.motion_ack_count++;
            entry->awaiting_ack = false;
            log_reason = "motion_ack";
        }
        break;
    case MCU_LINK_RX_EVENT_NACK:
    case MCU_LINK_RX_EVENT_FAULT:
        if (entry != NULL) {
            s_waiting_for_motion_ack = false;
            entry->active = false;
            entry->awaiting_ack = false;
            log_reason = "motion_reject";
        }
        break;
    case MCU_LINK_RX_EVENT_MOTION_DONE:
        if (entry != NULL) {
            s_stats.motion_done_count++;
            s_waiting_for_motion_ack = false;
            entry->active = false;
            entry->awaiting_ack = false;
            log_reason = "motion_done";
            if (s_ready_since_us != 0 &&
                (now_us - s_ready_since_us) >= ((int64_t)WATCHER_STRESS_ACTIVE_DURATION_MS * 1000LL) &&
                stress_active_inflight_count() == 0u) {
                log_drain_complete = true;
            }
        }
        break;
    case MCU_LINK_RX_EVENT_TOUCH_EVENT:
        s_stats.touch_rx_count++;
        break;
    case MCU_LINK_RX_EVENT_MAG_STATE:
        s_stats.mag_rx_count++;
        break;
    case MCU_LINK_RX_EVENT_IMU_STATE:
        s_stats.imu_rx_count++;
        break;
    default:
        break;
    }
    portEXIT_CRITICAL(&s_stress_lock);

    if (log_reason != NULL) {
        stress_log_stats(log_reason, false);
    }
    if (log_drain_complete) {
        stress_log_stats("drain_complete", true);
    }
}

void stress_mode_tick(void) {
    stress_drive_once();
    stress_log_stats("periodic", false);
}

#else

void stress_mode_init(void) {}

void stress_mode_on_link_event(const mcu_link_event_t *event) {
    (void)event;
}

void stress_mode_notify_ready(void) {}

void stress_mode_start(void) {}

void stress_mode_tick(void) {}

#endif
