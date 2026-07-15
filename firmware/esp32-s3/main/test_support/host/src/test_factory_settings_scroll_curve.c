#include "factory_settings_scroll_curve.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static unsigned int s_sqrt_call_count = 0;

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

/* Mirrors the integer component returned by LVGL 8.3 lv_sqrt(..., 0x8000). */
static uint16_t reference_lvgl_sqrt_integer(uint32_t value) {
    uint32_t mask = 0x8000;
    uint32_t root = 0;
    uint32_t trial;

    value <<= 8;
    do {
        trial = root + mask;
        if (trial * trial <= value) {
            root = trial;
        }
        mask >>= 1;
    } while (mask != 0);

    return (uint16_t)(root >> 4);
}

static uint16_t counting_sqrt(uint32_t value) {
    s_sqrt_call_count++;
    return reference_lvgl_sqrt_integer(value);
}

static int reference_scroll_x(int32_t diff_y) {
    int64_t distance = diff_y;

    if (distance < 0) {
        distance = -distance;
    }
    if (distance >= FACTORY_SETTINGS_SCROLL_RADIUS) {
        return FACTORY_SETTINGS_SCROLL_RADIUS;
    }

    const uint32_t radius_square = FACTORY_SETTINGS_SCROLL_RADIUS * FACTORY_SETTINGS_SCROLL_RADIUS;
    const uint32_t distance_square = (uint32_t)(distance * distance);
    return FACTORY_SETTINGS_SCROLL_RADIUS - (int)reference_lvgl_sqrt_integer(radius_square - distance_square);
}

static int test_curve_matches_existing_lvgl_projection_for_every_distance(void) {
    factory_settings_scroll_curve_t curve = {0};
    int failures = 0;

    s_sqrt_call_count = 0;
    failures += expect_true(factory_settings_scroll_curve_init(&curve, counting_sqrt), "initialize curve");
    failures += expect_int((int)s_sqrt_call_count, FACTORY_SETTINGS_SCROLL_RADIUS,
                           "precompute each in-radius point exactly once");

    for (int32_t diff_y = 0; diff_y <= FACTORY_SETTINGS_SCROLL_RADIUS; ++diff_y) {
        int16_t actual = -1;
        failures += expect_true(factory_settings_scroll_curve_project(&curve, diff_y, &actual), "project curve point");
        failures += expect_int(actual, reference_scroll_x(diff_y), "curve point preserves LVGL rounding");
    }

    failures += expect_int((int)s_sqrt_call_count, FACTORY_SETTINGS_SCROLL_RADIUS,
                           "scroll projection performs no square roots after initialization");
    return failures;
}

static int test_projection_preserves_symmetry_and_radius_clamping(void) {
    factory_settings_scroll_curve_t curve = {0};
    int16_t positive = 0;
    int16_t negative = 0;
    int16_t outside = 0;
    int16_t min_value = 0;
    int failures = 0;

    failures += expect_true(factory_settings_scroll_curve_init(&curve, reference_lvgl_sqrt_integer),
                            "initialize curve for boundary test");
    failures += expect_true(factory_settings_scroll_curve_project(&curve, 137, &positive), "project positive");
    failures += expect_true(factory_settings_scroll_curve_project(&curve, -137, &negative), "project negative");
    failures += expect_true(factory_settings_scroll_curve_project(&curve, 999, &outside), "project outside");
    failures += expect_true(factory_settings_scroll_curve_project(&curve, INT32_MIN, &min_value),
                            "project minimum integer safely");
    failures += expect_int(negative, positive, "curve remains symmetric");
    failures += expect_int(outside, FACTORY_SETTINGS_SCROLL_RADIUS, "outside point clamps to radius");
    failures += expect_int(min_value, FACTORY_SETTINGS_SCROLL_RADIUS, "minimum integer clamps without overflow");
    return failures;
}

static int test_invalid_inputs_fail_without_partial_state(void) {
    factory_settings_scroll_curve_t curve = {0};
    int16_t output = 123;
    int failures = 0;

    failures +=
        expect_true(!factory_settings_scroll_curve_init(NULL, reference_lvgl_sqrt_integer), "reject null curve");
    failures += expect_true(!factory_settings_scroll_curve_init(&curve, NULL), "reject null square-root callback");
    failures +=
        expect_true(!factory_settings_scroll_curve_project(&curve, 0, &output), "reject an uninitialized curve");
    failures += expect_true(!factory_settings_scroll_curve_project(NULL, 0, &output), "reject null curve projection");
    failures += expect_true(!factory_settings_scroll_curve_project(&curve, 0, NULL), "reject null output");
    failures += expect_int(output, 123, "failed projection leaves output unchanged");
    failures += expect_true(sizeof(curve) <= 1024u, "curve cache stays within one KiB of static RAM");
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_curve_matches_existing_lvgl_projection_for_every_distance();
    failures += test_projection_preserves_symmetry_and_radius_clamping();
    failures += test_invalid_inputs_fail_without_partial_state();

    if (failures != 0) {
        fprintf(stderr, "%d factory settings scroll curve host test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
