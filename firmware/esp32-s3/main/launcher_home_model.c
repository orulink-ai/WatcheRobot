#include "launcher_home_model.h"

#include <stddef.h>
#include <stdio.h>

int launcher_home_wrap_index(int index, int count) {
    if (count <= 0) {
        return -1;
    }

    index %= count;
    if (index < 0) {
        index += count;
    }
    return index;
}

int launcher_home_next_index(int selected, int direction, int count) {
    if (count <= 0) {
        return -1;
    }
    if (direction == 0) {
        return launcher_home_wrap_index(selected, count);
    }
    return launcher_home_wrap_index(selected + direction, count);
}

static uint32_t launcher_home_sqrt_u32(uint32_t value) {
    uint32_t root = 0;
    uint32_t bit = 1UL << 30;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }

    return root;
}

bool launcher_home_scroll_project(int32_t diff_y, int32_t radius, launcher_home_scroll_projection_t *projection) {
    int32_t translate_x;

    if (projection == NULL || radius <= 0 || radius > 46340) {
        return false;
    }

    if (diff_y < 0) {
        diff_y = -diff_y;
    }

    if (diff_y >= radius) {
        translate_x = -radius;
    } else {
        uint32_t y_square = (uint32_t)(diff_y * diff_y);
        uint32_t radius_square = (uint32_t)(radius * radius);
        uint32_t x = launcher_home_sqrt_u32(radius_square - y_square);
        translate_x = -(radius - (int32_t)x);
    }

    projection->translate_x = (int16_t)translate_x;
    projection->opacity = (uint8_t)(250 + translate_x + translate_x);
    return true;
}

bool launcher_home_format_time_text(int hour, int minute, char *out, size_t out_size) {
    if (out == NULL || out_size < LAUNCHER_HOME_TIME_TEXT_LEN || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59) {
        return false;
    }

    snprintf(out, out_size, "%02d:%02d", hour, minute);
    return true;
}

bool launcher_home_format_battery_text(int percent, char *out, size_t out_size) {
    if (out == NULL || out_size < LAUNCHER_HOME_BATTERY_TEXT_LEN || percent > 100) {
        return false;
    }

    if (percent < 0) {
        snprintf(out, out_size, "--");
        return true;
    }

    snprintf(out, out_size, "%d", percent);
    return true;
}

launcher_home_battery_state_t launcher_home_resolve_battery_state(bool sample_valid,
                                                                  bool vbus_present,
                                                                  bool charging,
                                                                  bool battery_present,
                                                                  bool percent_valid,
                                                                  int percent) {
    if (!sample_valid) {
        return LAUNCHER_HOME_BATTERY_UNKNOWN;
    }

    (void)charging;

    if (vbus_present) {
        return LAUNCHER_HOME_BATTERY_CHARGING;
    }

    if (battery_present && percent_valid && percent <= LAUNCHER_HOME_LOW_BATTERY_PERCENT) {
        return LAUNCHER_HOME_BATTERY_LOW;
    }

    if (battery_present && percent_valid) {
        return LAUNCHER_HOME_BATTERY_NORMAL;
    }

    return LAUNCHER_HOME_BATTERY_UNKNOWN;
}
