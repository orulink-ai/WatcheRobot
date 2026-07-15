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

static int64_t s_now_us;
static bool s_vbus_present;
static bool s_charging;
static bool s_standby;
static bool s_gate_allows;
static uint8_t s_battery_percent;
static unsigned s_behavior_set_count;
static unsigned s_gate_call_count;
static unsigned s_battery_voltage_reads;
static unsigned s_battery_percent_reads;
static unsigned s_shutdown_count;
static power_monitor_behavior_t s_last_gate_behavior;
static char s_current_behavior[24];

int64_t esp_timer_get_time(void) {
    return s_now_us;
}

uint32_t bsp_exp_io_get_level(int pin) {
    (void)pin;
    return s_vbus_present ? 0u : 1u;
}

bool bsp_system_is_charging(void) {
    return s_charging;
}

bool bsp_system_is_standby(void) {
    return s_standby;
}

bool bsp_battery_is_present(void) {
    return true;
}

uint16_t bsp_battery_get_voltage(void) {
    s_battery_voltage_reads++;
    return 3800u;
}

uint8_t bsp_battery_get_percent(void) {
    s_battery_percent_reads++;
    return s_battery_percent;
}

esp_err_t bsp_system_shutdown(void) {
    s_shutdown_count++;
    return ESP_OK;
}

esp_err_t behavior_state_set(const char *state_id) {
    s_behavior_set_count++;
    if (state_id == NULL) {
        s_current_behavior[0] = '\0';
        return ESP_OK;
    }
    snprintf(s_current_behavior, sizeof(s_current_behavior), "%s", state_id);
    return ESP_OK;
}

const char *behavior_state_get_current(void) {
    return s_current_behavior[0] != '\0' ? s_current_behavior : NULL;
}

static bool behavior_gate(power_monitor_behavior_t behavior, const power_monitor_event_t *event, void *user_ctx) {
    (void)event;
    (void)user_ctx;
    s_gate_call_count++;
    s_last_gate_behavior = behavior;
    return s_gate_allows;
}

static void reset_fixture(void) {
    s_now_us = 0;
    s_vbus_present = false;
    s_charging = false;
    s_standby = false;
    s_gate_allows = true;
    s_battery_percent = 80u;
    s_behavior_set_count = 0;
    s_gate_call_count = 0;
    s_battery_voltage_reads = 0;
    s_battery_percent_reads = 0;
    s_shutdown_count = 0;
    s_last_gate_behavior = POWER_MONITOR_BEHAVIOR_RECHARGE;
    s_current_behavior[0] = '\0';
    assert(power_monitor_service_init() == ESP_OK);
    power_monitor_service_set_behavior_gate(behavior_gate, NULL);
}

static void tick_after_ms(uint32_t delta_ms, bool battery_display_active) {
    s_now_us += (int64_t)delta_ms * 1000LL;
    assert(power_monitor_service_tick(battery_display_active) == ESP_OK);
}

static void test_gate_can_suppress_initial_recharge_behavior(void) {
    reset_fixture();
    s_gate_allows = false;
    s_vbus_present = true;
    s_charging = true;

    tick_after_ms(1000u, false);

    assert(s_gate_call_count == 1u);
    assert(s_last_gate_behavior == POWER_MONITOR_BEHAVIOR_RECHARGE);
    assert(s_behavior_set_count == 0u);
    assert(behavior_state_get_current() == NULL);
}

static void test_gate_allows_initial_recharge_behavior_by_default(void) {
    reset_fixture();
    s_vbus_present = true;
    s_charging = true;

    tick_after_ms(1000u, false);

    assert(s_gate_call_count == 1u);
    assert(s_behavior_set_count == 1u);
    assert(strcmp(behavior_state_get_current(), "recharge") == 0);
}

static void test_non_display_tick_skips_initial_battery_read(void) {
    power_monitor_sample_t snapshot;

    reset_fixture();

    tick_after_ms(1000u, false);

    assert(s_battery_voltage_reads == 0u);
    assert(s_battery_percent_reads == 0u);
    assert(power_monitor_service_get_snapshot(&snapshot) == ESP_OK);
    assert(!snapshot.battery_percent_valid);
    assert(!snapshot.battery_voltage_valid);
}

static void test_display_tick_refreshes_battery_for_home_status(void) {
    power_monitor_sample_t snapshot;

    reset_fixture();

    tick_after_ms(1000u, true);

    assert(s_battery_voltage_reads == 1u);
    assert(s_battery_percent_reads == 1u);
    assert(s_shutdown_count == 0u);
    assert(power_monitor_service_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.battery_percent_valid);
    assert(snapshot.battery_voltage_valid);
    assert(snapshot.battery_percent == 80u);
}

static void test_non_display_tick_uses_slow_background_battery_check(void) {
    reset_fixture();

    tick_after_ms(1000u, false);
    assert(s_battery_voltage_reads == 0u);
    assert(s_battery_percent_reads == 0u);

    tick_after_ms(300000u, false);

    assert(s_battery_voltage_reads == 1u);
    assert(s_battery_percent_reads == 1u);
    assert(s_shutdown_count == 0u);
}

static void test_low_battery_display_tick_requests_shutdown_without_vbus(void) {
    reset_fixture();
    s_battery_percent = 15u;

    tick_after_ms(1000u, true);

    assert(s_battery_voltage_reads == 1u);
    assert(s_battery_percent_reads == 1u);
    assert(s_shutdown_count == 1u);
}

static void test_low_battery_tick_does_not_shutdown_while_vbus_present(void) {
    reset_fixture();
    s_vbus_present = true;
    s_charging = true;
    s_battery_percent = 15u;

    tick_after_ms(1000u, true);

    assert(s_battery_voltage_reads == 1u);
    assert(s_battery_percent_reads == 1u);
    assert(s_shutdown_count == 0u);
}

int main(void) {
    printf("test behavior gate suppresses recharge\n");
    test_gate_can_suppress_initial_recharge_behavior();
    printf("test behavior gate allows recharge\n");
    test_gate_allows_initial_recharge_behavior_by_default();
    printf("test non-display tick skips initial battery read\n");
    test_non_display_tick_skips_initial_battery_read();
    printf("test display tick refreshes battery\n");
    test_display_tick_refreshes_battery_for_home_status();
    printf("test non-display tick uses slow background battery check\n");
    test_non_display_tick_uses_slow_background_battery_check();
    printf("test low battery display tick requests shutdown\n");
    test_low_battery_display_tick_requests_shutdown_without_vbus();
    printf("test low battery does not shutdown while charging\n");
    test_low_battery_tick_does_not_shutdown_while_vbus_present();
    printf("power monitor behavior gate host tests passed\n");
    return 0;
}
