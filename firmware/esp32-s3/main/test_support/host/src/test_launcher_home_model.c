#include "launcher_home_model.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int expect_int(int actual, int expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s actual=%d expected=%d\n", message, actual, expected);
        return 1;
    }
    return 0;
}

static int expect_text(const char *actual, const char *expected, const char *message) {
    const char *left = actual;
    const char *right = expected;

    while (*left != '\0' && *right != '\0' && *left == *right) {
        left++;
        right++;
    }
    if (*left != *right) {
        fprintf(stderr, "FAIL: %s actual=%s expected=%s\n", message, actual, expected);
        return 1;
    }
    return 0;
}

static int test_selection_wraps_without_rebuilding_home(void) {
    int failures = 0;

    failures += expect_int(launcher_home_wrap_index(-1, 4), 3, "negative wrap");
    failures += expect_int(launcher_home_wrap_index(4, 4), 0, "positive wrap");
    failures += expect_int(launcher_home_wrap_index(-1, 5), 4, "negative wrap with phone control");
    failures += expect_int(launcher_home_wrap_index(5, 5), 0, "positive wrap with phone control");
    failures += expect_int(launcher_home_next_index(0, 1, 4), 1, "next wraps forward");
    failures += expect_int(launcher_home_next_index(0, -1, 4), 3, "next wraps backward");
    failures += expect_int(launcher_home_next_index(4, 1, 5), 0, "fifth entry wraps forward");
    failures += expect_int(launcher_home_next_index(0, -1, 5), 4, "fifth entry wraps backward");
    failures += expect_int(launcher_home_next_index(2, 0, 4), 2, "zero direction keeps focus");
    failures += expect_int(launcher_home_next_index(0, 5, 4), 1, "large direction wraps");
    failures += expect_int(launcher_home_next_index(0, 1, 0), -1, "invalid count rejected");

    return failures;
}

static int test_factory_scroll_projection_matches_right_arc(void) {
    int failures = 0;
    launcher_home_scroll_projection_t center = {0};
    launcher_home_scroll_projection_t halfway = {0};
    launcher_home_scroll_projection_t outside = {0};

    failures += expect_true(launcher_home_scroll_project(0, 245, &center), "project center");
    failures += expect_int(center.translate_x, 0, "center stays at right edge");
    failures += expect_int(center.opacity, 250, "center opacity follows factory curve");

    failures += expect_true(launcher_home_scroll_project(122, 245, &halfway), "project halfway");
    failures += expect_true(halfway.translate_x < 0 && halfway.translate_x > -245, "halfway follows arc");

    failures += expect_true(launcher_home_scroll_project(300, 245, &outside), "project outside");
    failures += expect_int(outside.translate_x, -245, "outside clamps to radius");
    failures += expect_int(outside.opacity, (uint8_t)(250 - 245 - 245), "outside opacity follows factory cast");
    failures += expect_true(halfway.opacity < center.opacity && halfway.opacity > outside.opacity,
                            "halfway opacity fades gradually");

    return failures;
}

static int test_projection_rejects_invalid_inputs(void) {
    int failures = 0;
    launcher_home_scroll_projection_t projection = {0};

    failures += expect_true(!launcher_home_scroll_project(0, 0, &projection), "reject zero radius");
    failures += expect_true(!launcher_home_scroll_project(0, 245, NULL), "reject null output");

    return failures;
}

static int test_status_text_uses_real_values_or_unknown_placeholders(void) {
    int failures = 0;
    char time_text[LAUNCHER_HOME_TIME_TEXT_LEN] = {0};
    char battery_text[LAUNCHER_HOME_BATTERY_TEXT_LEN] = {0};

    failures += expect_true(launcher_home_format_time_text(7, 5, time_text, sizeof(time_text)), "format time");
    failures += expect_text(time_text, "07:05", "time is zero padded");
    failures += expect_true(!launcher_home_format_time_text(24, 0, time_text, sizeof(time_text)), "reject bad hour");

    failures += expect_true(launcher_home_format_battery_text(100, battery_text, sizeof(battery_text)),
                            "format full battery");
    failures += expect_text(battery_text, "100", "battery accepts three digits");
    failures += expect_true(launcher_home_format_battery_text(-1, battery_text, sizeof(battery_text)),
                            "format unknown battery");
    failures += expect_text(battery_text, "--", "unknown battery is explicit");
    failures += expect_true(!launcher_home_format_battery_text(101, battery_text, sizeof(battery_text)),
                            "reject impossible battery");

    return failures;
}

static int test_battery_state_matches_factory_status_behavior(void) {
    int failures = 0;

    failures += expect_int(launcher_home_resolve_battery_state(false, false, false, true, true, 80),
                           LAUNCHER_HOME_BATTERY_UNKNOWN, "missing sample is unknown");
    failures += expect_int(launcher_home_resolve_battery_state(true, true, false, true, true, 80),
                           LAUNCHER_HOME_BATTERY_CHARGING, "vbus maps to charging");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, true, true, true, 80),
                           LAUNCHER_HOME_BATTERY_NORMAL, "pmu charging alone does not drive factory home charging icon");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, false, true, true, 0),
                           LAUNCHER_HOME_BATTERY_LOW, "zero percent without vbus maps to low");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, false, true, true,
                                                               LAUNCHER_HOME_LOW_BATTERY_PERCENT),
                           LAUNCHER_HOME_BATTERY_LOW, "low threshold maps to low");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, false, true, true,
                                                               LAUNCHER_HOME_LOW_BATTERY_PERCENT + 1),
                           LAUNCHER_HOME_BATTERY_NORMAL, "above low threshold maps to normal");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, false, true, true, 36),
                           LAUNCHER_HOME_BATTERY_NORMAL, "valid battery maps to normal");
    failures += expect_int(launcher_home_resolve_battery_state(true, false, false, true, false, 0),
                           LAUNCHER_HOME_BATTERY_UNKNOWN, "invalid percent is unknown");

    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_selection_wraps_without_rebuilding_home();
    failures += test_factory_scroll_projection_matches_right_arc();
    failures += test_projection_rejects_invalid_inputs();
    failures += test_status_text_uses_real_values_or_unknown_placeholders();
    failures += test_battery_state_matches_factory_status_behavior();

    if (failures != 0) {
        fprintf(stderr, "%d launcher_home_model host test(s) failed\n", failures);
        return 1;
    }

    return 0;
}
