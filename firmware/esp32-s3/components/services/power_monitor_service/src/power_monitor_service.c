#include "power_monitor_service.h"

#include "behavior_state_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sensecap-watcher.h"

#include <string.h>

#define POWER_MONITOR_POLL_INTERVAL_MS 1000u
#define POWER_MONITOR_BATTERY_DISPLAY_INTERVAL_MS 30000u
#define POWER_MONITOR_BATTERY_BACKGROUND_INTERVAL_MS 300000u
#define POWER_MONITOR_LOW_BATTERY_PERCENT 15u
#define POWER_MONITOR_LOW_BATTERY_RECOVER_PERCENT 18u
#define POWER_MONITOR_RECHARGE_STATE "recharge"
#define POWER_MONITOR_UNPLUG_FALLBACK_STATE "standby"

static const char *TAG = "POWER_MON";
static const char *OBS_TAG = "POWER_OBS";

static power_monitor_logic_state_t s_logic;
static int64_t s_last_poll_us;
static int64_t s_last_battery_read_us;
static bool s_battery_cache_valid;
static uint8_t s_cached_battery_percent;
static uint16_t s_cached_battery_voltage_mv;
static power_monitor_behavior_gate_cb_t s_behavior_gate_cb;
static void *s_behavior_gate_ctx;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static bool read_vbus_present(void) {
    return bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0u;
}

static void refresh_battery_cache(void) {
    s_cached_battery_voltage_mv = bsp_battery_get_voltage();
    s_cached_battery_percent = bsp_battery_get_percent();
    s_battery_cache_valid = true;
    s_last_battery_read_us = esp_timer_get_time();
}

static bool battery_refresh_due(int64_t now_us, bool force_battery_read, bool battery_display_active) {
    int64_t interval_us;

    if (force_battery_read) {
        return true;
    }

    if (battery_display_active) {
        interval_us = (int64_t)POWER_MONITOR_BATTERY_DISPLAY_INTERVAL_MS * 1000LL;
        return !s_battery_cache_valid || (now_us - s_last_battery_read_us) >= interval_us;
    }

    interval_us = (int64_t)POWER_MONITOR_BATTERY_BACKGROUND_INTERVAL_MS * 1000LL;
    return (now_us - s_last_battery_read_us) >= interval_us;
}

static void fill_sample(power_monitor_sample_t *sample, bool force_battery_read, bool battery_display_active) {
    int64_t now_us = esp_timer_get_time();

    if (sample == NULL) {
        return;
    }

    if (battery_refresh_due(now_us, force_battery_read, battery_display_active)) {
        refresh_battery_cache();
    }

    memset(sample, 0, sizeof(*sample));
    sample->vbus_present = read_vbus_present();
    sample->charging = bsp_system_is_charging();
    sample->standby = bsp_system_is_standby();
    sample->battery_present = bsp_battery_is_present();
    sample->battery_percent_valid = s_battery_cache_valid;
    sample->battery_voltage_valid = s_battery_cache_valid;
    sample->battery_percent = s_cached_battery_percent;
    sample->battery_voltage_mv = s_cached_battery_voltage_mv;
    sample->timestamp_ms = now_ms();
}

static void apply_charge_behavior(const power_monitor_event_t *event) {
    const char *current_state;
    esp_err_t ret;

    if (event == NULL) {
        return;
    }

    if (event->entered_charging_mode) {
        if (s_behavior_gate_cb != NULL &&
            !s_behavior_gate_cb(POWER_MONITOR_BEHAVIOR_RECHARGE, event, s_behavior_gate_ctx)) {
            ESP_LOGI(TAG, "Recharge behavior suppressed by behavior gate");
            return;
        }
        ret = behavior_state_set(POWER_MONITOR_RECHARGE_STATE);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to enter recharge behavior: %s", esp_err_to_name(ret));
        }
        return;
    }

    if (!event->exited_charging_mode) {
        return;
    }

    current_state = behavior_state_get_current();
    if (current_state == NULL || strcmp(current_state, POWER_MONITOR_RECHARGE_STATE) != 0) {
        return;
    }

    if (s_behavior_gate_cb != NULL &&
        !s_behavior_gate_cb(POWER_MONITOR_BEHAVIOR_STANDBY_AFTER_UNPLUG, event, s_behavior_gate_ctx)) {
        ESP_LOGI(TAG, "Unplug fallback behavior suppressed by behavior gate");
        return;
    }

    ret = behavior_state_set(POWER_MONITOR_UNPLUG_FALLBACK_STATE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to leave recharge behavior: %s", esp_err_to_name(ret));
    }
}

static void log_event(const power_monitor_event_t *event) {
    const power_monitor_sample_t *s;

    if (event == NULL || event->flags == POWER_MONITOR_EVENT_NONE) {
        return;
    }

    s = &event->snapshot;
    ESP_LOGI(TAG,
             "power flags=0x%08lx vbus=%u charging=%u standby=%u bat_present=%u bat=%u%% voltage=%umV",
             (unsigned long)event->flags, s->vbus_present ? 1u : 0u, s->charging ? 1u : 0u,
             s->standby ? 1u : 0u, s->battery_present ? 1u : 0u,
             s->battery_percent_valid ? (unsigned)s->battery_percent : 0u,
             s->battery_voltage_valid ? (unsigned)s->battery_voltage_mv : 0u);
    ESP_LOGI(OBS_TAG,
             "evt=power_status flags=0x%08lx vbus=%u charging=%u standby=%u battery_present=%u battery_percent=%u "
             "voltage_mv=%u",
             (unsigned long)event->flags, s->vbus_present ? 1u : 0u, s->charging ? 1u : 0u,
             s->standby ? 1u : 0u, s->battery_present ? 1u : 0u,
             s->battery_percent_valid ? (unsigned)s->battery_percent : 0u,
             s->battery_voltage_valid ? (unsigned)s->battery_voltage_mv : 0u);

    if ((event->flags & POWER_MONITOR_EVENT_LOW_BATTERY) != 0u) {
        ESP_LOGW(TAG, "Battery is critically low and no VBUS is present");
    }
}

static void apply_low_battery_shutdown(const power_monitor_event_t *event) {
    esp_err_t ret;

    if (event == NULL || (event->flags & POWER_MONITOR_EVENT_LOW_BATTERY) == 0u) {
        return;
    }

    ESP_LOGW(TAG, "Requesting system shutdown for low battery");
    ret = bsp_system_shutdown();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Low battery shutdown request failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t update_once(bool force_battery_read, bool battery_display_active) {
    power_monitor_sample_t sample;
    power_monitor_event_t event;
    esp_err_t ret;

    fill_sample(&sample, force_battery_read, battery_display_active);
    ret = power_monitor_logic_update(&s_logic, &sample, &event);
    if (ret != ESP_OK) {
        return ret;
    }

    apply_charge_behavior(&event);
    log_event(&event);
    apply_low_battery_shutdown(&event);

    if (!event.request_battery_refresh || force_battery_read) {
        return ESP_OK;
    }

    fill_sample(&sample, true, battery_display_active);
    ret = power_monitor_logic_update(&s_logic, &sample, &event);
    if (ret == ESP_OK) {
        log_event(&event);
        apply_low_battery_shutdown(&event);
    }
    return ret;
}

esp_err_t power_monitor_service_init(void) {
    const power_monitor_logic_config_t config = {
        .vbus_debounce_samples = 2u,
        .battery_report_delta_percent = 1u,
        .low_battery_percent = POWER_MONITOR_LOW_BATTERY_PERCENT,
        .low_battery_recover_percent = POWER_MONITOR_LOW_BATTERY_RECOVER_PERCENT,
    };

    power_monitor_logic_init(&s_logic, &config);
    s_last_poll_us = 0;
    s_last_battery_read_us = esp_timer_get_time();
    s_battery_cache_valid = false;
    s_cached_battery_percent = 0u;
    s_cached_battery_voltage_mv = 0u;
    s_behavior_gate_cb = NULL;
    s_behavior_gate_ctx = NULL;
    return ESP_OK;
}

void power_monitor_service_set_behavior_gate(power_monitor_behavior_gate_cb_t cb, void *user_ctx) {
    s_behavior_gate_cb = cb;
    s_behavior_gate_ctx = user_ctx;
}

esp_err_t power_monitor_service_tick(bool battery_display_active) {
    int64_t now_us = esp_timer_get_time();

    if (s_last_poll_us != 0 && (now_us - s_last_poll_us) < ((int64_t)POWER_MONITOR_POLL_INTERVAL_MS * 1000LL)) {
        return ESP_OK;
    }

    s_last_poll_us = now_us;
    return update_once(false, battery_display_active);
}

esp_err_t power_monitor_service_get_snapshot(power_monitor_sample_t *out_sample) {
    return power_monitor_logic_get_snapshot(&s_logic, out_sample);
}
