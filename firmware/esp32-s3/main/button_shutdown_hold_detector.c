#include "button_shutdown_hold_detector.h"

#include <stddef.h>
#include <string.h>

static uint32_t add_capped(uint32_t value, uint32_t delta, uint32_t limit) {
    if (value >= limit || delta >= limit - value) {
        return limit;
    }
    return value + delta;
}

void button_shutdown_hold_detector_init(button_shutdown_hold_detector_t *detector, uint32_t debounce_ms,
                                        uint32_t hold_ms, uint32_t rearm_release_ms) {
    if (detector == NULL) {
        return;
    }

    memset(detector, 0, sizeof(*detector));
    detector->debounce_ms = debounce_ms;
    detector->hold_ms = hold_ms;
    detector->rearm_release_ms = rearm_release_ms;
    detector->raw_stable_ms = debounce_ms;
    detector->released_ms = rearm_release_ms;
    detector->armed = true;
}

bool button_shutdown_hold_detector_update(button_shutdown_hold_detector_t *detector, bool raw_pressed,
                                          uint32_t elapsed_ms) {
    if (detector == NULL) {
        return false;
    }

    if (raw_pressed != detector->last_raw_pressed) {
        detector->last_raw_pressed = raw_pressed;
        detector->raw_stable_ms = 0;
    } else {
        detector->raw_stable_ms = add_capped(detector->raw_stable_ms, elapsed_ms, detector->debounce_ms);
    }

    if (detector->raw_stable_ms >= detector->debounce_ms && detector->stable_pressed != raw_pressed) {
        detector->stable_pressed = raw_pressed;
        if (raw_pressed) {
            detector->held_ms = 0;
            detector->released_ms = 0;
            detector->fired_for_current_hold = false;
        } else {
            detector->held_ms = 0;
        }
    }

    if (detector->stable_pressed) {
        detector->released_ms = 0;
        detector->held_ms = add_capped(detector->held_ms, elapsed_ms, detector->hold_ms);
        if (detector->armed && !detector->fired_for_current_hold && detector->held_ms >= detector->hold_ms) {
            detector->fired_for_current_hold = true;
            detector->armed = false;
            return true;
        }
        return false;
    }

    detector->held_ms = 0;
    detector->released_ms = add_capped(detector->released_ms, elapsed_ms, detector->rearm_release_ms);
    if (!detector->armed && detector->released_ms >= detector->rearm_release_ms) {
        detector->armed = true;
    }
    return false;
}
