#include "power_monitor_service.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(expr)                                                                                                   \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "assert failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                 \
            fflush(stderr);                                                                                            \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static power_monitor_sample_t make_sample(bool vbus_present, bool charging, bool standby, uint8_t percent) {
    power_monitor_sample_t sample = {
        .vbus_present = vbus_present,
        .charging = charging,
        .standby = standby,
        .battery_present = true,
        .battery_percent_valid = true,
        .battery_voltage_valid = true,
        .battery_percent = percent,
        .battery_voltage_mv = (uint16_t)(3600u + percent),
        .timestamp_ms = 1000u,
    };
    return sample;
}

static power_monitor_logic_state_t make_state(void) {
    power_monitor_logic_state_t state;
    const power_monitor_logic_config_t config = {
        .vbus_debounce_samples = 2u,
        .battery_report_delta_percent = 2u,
        .low_battery_percent = 0u,
    };
    power_monitor_logic_init(&state, &config);
    return state;
}

static esp_err_t update_sample(power_monitor_logic_state_t *state, bool vbus_present, bool charging, bool standby,
                               uint8_t percent, power_monitor_event_t *event) {
    power_monitor_sample_t sample = make_sample(vbus_present, charging, standby, percent);
    return power_monitor_logic_update(state, &sample, event);
}

static void test_initial_battery_snapshot_reports_charge_mode_when_vbus_present(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_event_t event;

    assert(update_sample(&state, true, true, false, 80u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_INITIAL) != 0u);
    assert((event.flags & POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED) != 0u);
    assert(event.entered_charging_mode);
    assert(!event.exited_charging_mode);
    assert(event.snapshot.vbus_present);
    assert(event.snapshot.battery_percent == 80u);
}

static void test_vbus_change_requires_consecutive_debounce_samples(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_event_t event;

    assert(update_sample(&state, false, false, false, 70u, &event) == ESP_OK);
    assert(!event.entered_charging_mode);

    assert(update_sample(&state, true, true, false, 70u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_VBUS_CHANGED) == 0u);
    assert(!event.snapshot.vbus_present);

    assert(update_sample(&state, true, true, false, 70u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_VBUS_CHANGED) != 0u);
    assert(event.entered_charging_mode);
    assert(event.snapshot.vbus_present);
}

static void test_unplug_requests_immediate_battery_refresh(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_event_t event;

    assert(update_sample(&state, true, true, false, 50u, &event) == ESP_OK);
    assert(update_sample(&state, false, false, false, 50u, &event) == ESP_OK);
    assert(!event.request_battery_refresh);

    assert(update_sample(&state, false, false, false, 50u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_VBUS_CHANGED) != 0u);
    assert(event.exited_charging_mode);
    assert(event.request_battery_refresh);
    assert(!event.snapshot.vbus_present);
}

static void test_battery_delta_threshold_suppresses_small_noise(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_event_t event;

    assert(update_sample(&state, false, false, false, 60u, &event) == ESP_OK);
    assert(update_sample(&state, false, false, false, 61u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED) == 0u);

    assert(update_sample(&state, false, false, false, 62u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED) != 0u);
}

static void test_low_battery_only_fires_without_vbus(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_event_t event;

    assert(update_sample(&state, true, true, false, 0u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_LOW_BATTERY) == 0u);

    assert(update_sample(&state, false, false, false, 0u, &event) == ESP_OK);
    assert(update_sample(&state, false, false, false, 0u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_LOW_BATTERY) != 0u);

    assert(update_sample(&state, false, false, false, 0u, &event) == ESP_OK);
    assert((event.flags & POWER_MONITOR_EVENT_LOW_BATTERY) == 0u);
}

static void test_snapshot_rejects_before_initial_sample(void) {
    power_monitor_logic_state_t state = make_state();
    power_monitor_sample_t snapshot;

    assert(power_monitor_logic_get_snapshot(&state, &snapshot) == ESP_ERR_INVALID_STATE);
}

int main(void) {
    printf("test initial snapshot\n");
    test_initial_battery_snapshot_reports_charge_mode_when_vbus_present();
    printf("test vbus debounce\n");
    test_vbus_change_requires_consecutive_debounce_samples();
    printf("test unplug refresh\n");
    test_unplug_requests_immediate_battery_refresh();
    printf("test battery delta\n");
    test_battery_delta_threshold_suppresses_small_noise();
    printf("test low battery\n");
    test_low_battery_only_fires_without_vbus();
    printf("test snapshot before init\n");
    test_snapshot_rejects_before_initial_sample();
    printf("power monitor logic host tests passed\n");
    return 0;
}
