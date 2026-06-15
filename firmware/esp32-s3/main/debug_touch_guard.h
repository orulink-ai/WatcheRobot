#ifndef DEBUG_TOUCH_GUARD_H
#define DEBUG_TOUCH_GUARD_H

#include <stdbool.h>
#include <stdint.h>

void debug_touch_guard_suppress_for_ms(uint32_t duration_ms);
bool debug_touch_guard_is_suppressed(void);
uint32_t debug_touch_guard_remaining_ms(void);

#endif /* DEBUG_TOUCH_GUARD_H */
