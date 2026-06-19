#include "power_monitor_service.h"

#include "behavior_state_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sensecap-watcher.h"

#include <string.h>

#define POWER_MONITOR_POLL_INTERVAL_MS 1000u
#define POWER_MONITOR_BATTERY_INTERVAL_MS 30000u
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

static void fill_sample(power_monitor_sample_t *sample, bool force_battery_read) {
    int64_t now_us = esp_timer_get_time();
    bool battery_due;

    if (sample == NULL) {
        return;
    }

    battery_due = !s_battery_cache_valid ||
                  (now_us - s_last_battery_read_us) >= ((int64_t)POWER_MONITOR_BATTERY_INTERVAL_MS * 1000LL);
    if (force_battery_read || battery_due) {
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

static esp_err_t update_once(bool force_battery_read) {
    power_monitor_sample_t sample;
    power_monitor_event_t event;
    esp_err_t ret;

    fill_sample(&sample, force_battery_read);
    ret = power_monitor_logic_update(&s_logic, &sample, &event);
    if (ret != ESP_OK) {
        return ret;
    }

    apply_charge_behavior(&event);
    log_event(&event);

    if (!event.request_battery_refresh || force_battery_read) {
        return ESP_OK;
    }

    fill_sample(&sample, true);
    ret = power_monitor_logic_update(&s_logic, &sample, &event);
    if (ret == ESP_OK) {
        log_event(&event);
    }
    return ret;
}

esp_err_t power_monitor_service_init(void) {
    const power_monitor_logic_config_t config = {
        .vbus_debounce_samples = 2u,
        .battery_report_delta_percent = 1u,
        .low_battery_percent = 0u,
    };

    power_monitor_logic_init(&s_logic, &config);
    s_last_poll_us = 0;
    s_last_battery_read_us = 0;
    s_battery_cache_valid = false;
    s_cached_battery_percent = 0u;
    s_cached_battery_voltage_mv = 0u;
    return ESP_OK;
}

esp_err_t power_monitor_service_tick(void) {
    int64_t now_us = esp_timer_get_time();

    if (s_last_poll_us != 0 && (now_us - s_last_poll_us) < ((int64_t)POWER_MONITOR_POLL_INTERVAL_MS * 1000LL)) {
        return ESP_OK;
    }

    s_last_poll_us = now_us;
    return update_once(false);
}

esp_err_t power_monitor_service_get_snapshot(power_monitor_sample_t *out_sample) {
    return power_monitor_logic_get_snapshot(&s_logic, out_sample);
}
