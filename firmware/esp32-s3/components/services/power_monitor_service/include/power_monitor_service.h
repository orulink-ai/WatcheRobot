#ifndef POWER_MONITOR_SERVICE_H
#define POWER_MONITOR_SERVICE_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_MONITOR_EVENT_NONE = 0,
    POWER_MONITOR_EVENT_INITIAL = 1u << 0u,
    POWER_MONITOR_EVENT_VBUS_CHANGED = 1u << 1u,
    POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED = 1u << 2u,
    POWER_MONITOR_EVENT_CHARGING_CHANGED = 1u << 3u,
    POWER_MONITOR_EVENT_STANDBY_CHANGED = 1u << 4u,
    POWER_MONITOR_EVENT_BATTERY_PRESENT_CHANGED = 1u << 5u,
    POWER_MONITOR_EVENT_LOW_BATTERY = 1u << 6u,
} power_monitor_event_flag_t;

typedef struct {
    bool vbus_present;
    bool charging;
    bool standby;
    bool battery_present;
    bool battery_percent_valid;
    bool battery_voltage_valid;
    uint8_t battery_percent;
    uint16_t battery_voltage_mv;
    uint32_t timestamp_ms;
} power_monitor_sample_t;

typedef struct {
    uint8_t vbus_debounce_samples;
    uint8_t battery_report_delta_percent;
    uint8_t low_battery_percent;
} power_monitor_logic_config_t;

typedef struct {
    bool initialized;
    bool stable_vbus_present;
    bool candidate_vbus_present;
    uint8_t candidate_vbus_count;
    bool low_battery_active;
    bool reported_battery_percent_valid;
    uint8_t reported_battery_percent;
    power_monitor_logic_config_t config;
    power_monitor_sample_t latest;
} power_monitor_logic_state_t;

typedef struct {
    uint32_t flags;
    bool entered_charging_mode;
    bool exited_charging_mode;
    bool request_battery_refresh;
    power_monitor_sample_t snapshot;
} power_monitor_event_t;

void power_monitor_logic_init(power_monitor_logic_state_t *state, const power_monitor_logic_config_t *config);
esp_err_t power_monitor_logic_update(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                     power_monitor_event_t *out_event);
esp_err_t power_monitor_logic_get_snapshot(const power_monitor_logic_state_t *state, power_monitor_sample_t *out_sample);

esp_err_t power_monitor_service_init(void);
esp_err_t power_monitor_service_tick(void);
esp_err_t power_monitor_service_get_snapshot(power_monitor_sample_t *out_sample);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MONITOR_SERVICE_H */
