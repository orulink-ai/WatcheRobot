#ifndef BUTTON_SHUTDOWN_HOLD_DETECTOR_H
#define BUTTON_SHUTDOWN_HOLD_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t debounce_ms;
    uint32_t hold_ms;
    uint32_t rearm_release_ms;
    uint32_t raw_stable_ms;
    uint32_t held_ms;
    uint32_t released_ms;
    bool last_raw_pressed;
    bool stable_pressed;
    bool fired_for_current_hold;
    bool armed;
} button_shutdown_hold_detector_t;

void button_shutdown_hold_detector_init(button_shutdown_hold_detector_t *detector, uint32_t debounce_ms,
                                        uint32_t hold_ms, uint32_t rearm_release_ms);
bool button_shutdown_hold_detector_update(button_shutdown_hold_detector_t *detector, bool raw_pressed,
                                          uint32_t elapsed_ms);

#endif /* BUTTON_SHUTDOWN_HOLD_DETECTOR_H */
