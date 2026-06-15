#include "debug_touch_guard.h"

#include "control_ingress.h"

void debug_touch_guard_suppress_for_ms(uint32_t duration_ms) {
    control_ingress_suppress_manual_touch_for_ms(duration_ms);
}

bool debug_touch_guard_is_suppressed(void) {
    return control_ingress_is_manual_touch_suppressed();
}

uint32_t debug_touch_guard_remaining_ms(void) {
    return control_ingress_manual_touch_remaining_ms();
}
