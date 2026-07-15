#ifndef WATCHER_FACTORY_SETTINGS_SCROLL_CURVE_H
#define WATCHER_FACTORY_SETTINGS_SCROLL_CURVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACTORY_SETTINGS_SCROLL_RADIUS 412
#define FACTORY_SETTINGS_SCROLL_CURVE_POINT_COUNT (FACTORY_SETTINGS_SCROLL_RADIUS + 1)

typedef uint16_t (*factory_settings_scroll_sqrt_fn_t)(uint32_t value);

typedef struct {
    int16_t x_by_distance[FACTORY_SETTINGS_SCROLL_CURVE_POINT_COUNT];
    bool initialized;
} factory_settings_scroll_curve_t;

bool factory_settings_scroll_curve_init(factory_settings_scroll_curve_t *curve,
                                        factory_settings_scroll_sqrt_fn_t sqrt_fn);
bool factory_settings_scroll_curve_project(const factory_settings_scroll_curve_t *curve, int32_t diff_y,
                                           int16_t *translate_x);

#ifdef __cplusplus
}
#endif

#endif
