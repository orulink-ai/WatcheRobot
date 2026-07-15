#include "power_monitor_service.h"

#include <stdlib.h>
#include <string.h>

#define POWER_MONITOR_DEFAULT_VBUS_DEBOUNCE_SAMPLES 2u
#define POWER_MONITOR_DEFAULT_BATTERY_DELTA_PERCENT 1u
#define POWER_MONITOR_DEFAULT_LOW_BATTERY_RECOVER_DELTA_PERCENT 3u

static uint8_t normalized_debounce_samples(const power_monitor_logic_config_t *config) {
    if (config == NULL || config->vbus_debounce_samples == 0u) {
        return POWER_MONITOR_DEFAULT_VBUS_DEBOUNCE_SAMPLES;
    }
    return config->vbus_debounce_samples;
}

static uint8_t normalized_battery_delta(const power_monitor_logic_config_t *config) {
    if (config == NULL || config->battery_report_delta_percent == 0u) {
        return POWER_MONITOR_DEFAULT_BATTERY_DELTA_PERCENT;
    }
    return config->battery_report_delta_percent;
}

static uint8_t normalized_low_battery_percent(const power_monitor_logic_config_t *config) {
    if (config == NULL) {
        return 0u;
    }
    return config->low_battery_percent;
}

static uint8_t normalized_low_battery_recover_percent(const power_monitor_logic_config_t *config, uint8_t low_percent) {
    if (config != NULL && config->low_battery_recover_percent > low_percent) {
        return config->low_battery_recover_percent;
    }
    if (low_percent > (uint8_t)(100u - POWER_MONITOR_DEFAULT_LOW_BATTERY_RECOVER_DELTA_PERCENT)) {
        return 100u;
    }
    return (uint8_t)(low_percent + POWER_MONITOR_DEFAULT_LOW_BATTERY_RECOVER_DELTA_PERCENT);
}

static int percent_delta(uint8_t left, uint8_t right) {
    return abs((int)left - (int)right);
}

void power_monitor_logic_init(power_monitor_logic_state_t *state, const power_monitor_logic_config_t *config) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->config.vbus_debounce_samples = normalized_debounce_samples(config);
    state->config.battery_report_delta_percent = normalized_battery_delta(config);
    state->config.low_battery_percent = normalized_low_battery_percent(config);
    state->config.low_battery_recover_percent =
        normalized_low_battery_recover_percent(config, state->config.low_battery_percent);
}

static void update_debounced_vbus(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                  power_monitor_event_t *event) {
    if (sample->vbus_present == state->stable_vbus_present) {
        state->candidate_vbus_present = sample->vbus_present;
        state->candidate_vbus_count = 0u;
        return;
    }

    if (sample->vbus_present != state->candidate_vbus_present) {
        state->candidate_vbus_present = sample->vbus_present;
        state->candidate_vbus_count = 1u;
    } else if (state->candidate_vbus_count < UINT8_MAX) {
        state->candidate_vbus_count++;
    }

    if (state->candidate_vbus_count < state->config.vbus_debounce_samples) {
        return;
    }

    state->stable_vbus_present = state->candidate_vbus_present;
    state->candidate_vbus_count = 0u;
    event->flags |= POWER_MONITOR_EVENT_VBUS_CHANGED;
    event->entered_charging_mode = state->stable_vbus_present;
    event->exited_charging_mode = !state->stable_vbus_present;
    event->request_battery_refresh = event->exited_charging_mode;
}

static void update_non_debounced_flags(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                       power_monitor_event_t *event) {
    if (sample->charging != state->latest.charging) {
        event->flags |= POWER_MONITOR_EVENT_CHARGING_CHANGED;
    }
    if (sample->standby != state->latest.standby) {
        event->flags |= POWER_MONITOR_EVENT_STANDBY_CHANGED;
    }
    if (sample->battery_present != state->latest.battery_present) {
        event->flags |= POWER_MONITOR_EVENT_BATTERY_PRESENT_CHANGED;
    }
}

static void update_battery_percent_flags(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                         power_monitor_event_t *event) {
    bool battery_changed = false;

    if (!sample->battery_percent_valid) {
        return;
    }

    if (!state->reported_battery_percent_valid) {
        battery_changed = true;
    } else if (percent_delta(sample->battery_percent, state->reported_battery_percent) >=
               (int)state->config.battery_report_delta_percent) {
        battery_changed = true;
    } else if (sample->battery_percent == 0u && state->reported_battery_percent != 0u) {
        battery_changed = true;
    }

    if (battery_changed) {
        event->flags |= POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED;
        state->reported_battery_percent = sample->battery_percent;
        state->reported_battery_percent_valid = true;
    }
}

static void update_low_battery_flags(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                     power_monitor_event_t *event) {
    bool low_now;

    if (state->stable_vbus_present || !sample->battery_present) {
        state->low_battery_active = false;
        return;
    }

    if (!sample->battery_percent_valid) {
        return;
    }

    if (state->low_battery_active) {
        state->low_battery_active = sample->battery_percent < state->config.low_battery_recover_percent;
        return;
    }

    low_now = sample->battery_percent <= state->config.low_battery_percent;
    if (low_now && !state->low_battery_active) {
        event->flags |= POWER_MONITOR_EVENT_LOW_BATTERY;
    }
    state->low_battery_active = low_now;
}

esp_err_t power_monitor_logic_update(power_monitor_logic_state_t *state, const power_monitor_sample_t *sample,
                                     power_monitor_event_t *out_event) {
    power_monitor_sample_t snapshot;

    if (state == NULL || sample == NULL || out_event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));

    if (!state->initialized) {
        state->initialized = true;
        state->stable_vbus_present = sample->vbus_present;
        state->candidate_vbus_present = sample->vbus_present;
        state->candidate_vbus_count = 0u;
        state->latest = *sample;
        state->latest.vbus_present = state->stable_vbus_present;
        out_event->flags = POWER_MONITOR_EVENT_INITIAL;
        if (sample->battery_percent_valid) {
            out_event->flags |= POWER_MONITOR_EVENT_BATTERY_PERCENT_CHANGED;
            state->reported_battery_percent = sample->battery_percent;
            state->reported_battery_percent_valid = true;
        }
        out_event->entered_charging_mode = state->stable_vbus_present;
        update_low_battery_flags(state, sample, out_event);
        out_event->snapshot = state->latest;
        return ESP_OK;
    }

    update_debounced_vbus(state, sample, out_event);
    update_non_debounced_flags(state, sample, out_event);
    update_battery_percent_flags(state, sample, out_event);
    update_low_battery_flags(state, sample, out_event);

    snapshot = *sample;
    snapshot.vbus_present = state->stable_vbus_present;
    state->latest = snapshot;
    out_event->snapshot = snapshot;
    return ESP_OK;
}

esp_err_t power_monitor_logic_get_snapshot(const power_monitor_logic_state_t *state,
                                           power_monitor_sample_t *out_sample) {
    if (state == NULL || out_sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_sample = state->latest;
    return ESP_OK;
}
