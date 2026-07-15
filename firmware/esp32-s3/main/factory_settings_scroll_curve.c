#include "factory_settings_scroll_curve.h"

#include <stddef.h>

bool factory_settings_scroll_curve_init(factory_settings_scroll_curve_t *curve,
                                        factory_settings_scroll_sqrt_fn_t sqrt_fn) {
    const uint32_t radius_square = FACTORY_SETTINGS_SCROLL_RADIUS * FACTORY_SETTINGS_SCROLL_RADIUS;

    if (curve == NULL || sqrt_fn == NULL) {
        return false;
    }
    if (curve->initialized) {
        return true;
    }

    for (int32_t distance = 0; distance < FACTORY_SETTINGS_SCROLL_RADIUS; ++distance) {
        const uint32_t distance_square = (uint32_t)(distance * distance);
        const uint16_t root = sqrt_fn(radius_square - distance_square);

        if (root > FACTORY_SETTINGS_SCROLL_RADIUS) {
            return false;
        }
        curve->x_by_distance[distance] = (int16_t)(FACTORY_SETTINGS_SCROLL_RADIUS - root);
    }
    curve->x_by_distance[FACTORY_SETTINGS_SCROLL_RADIUS] = FACTORY_SETTINGS_SCROLL_RADIUS;
    curve->initialized = true;
    return true;
}

bool factory_settings_scroll_curve_project(const factory_settings_scroll_curve_t *curve, int32_t diff_y,
                                           int16_t *translate_x) {
    int64_t distance = diff_y;

    if (curve == NULL || translate_x == NULL || !curve->initialized) {
        return false;
    }
    if (distance < 0) {
        distance = -distance;
    }
    if (distance >= FACTORY_SETTINGS_SCROLL_RADIUS) {
        distance = FACTORY_SETTINGS_SCROLL_RADIUS;
    }

    *translate_x = curve->x_by_distance[distance];
    return true;
}
