#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>

extern int64_t control_ingress_host_esp_timer_now_us;

static inline int64_t esp_timer_get_time(void) {
    return control_ingress_host_esp_timer_now_us;
}

#endif /* ESP_TIMER_H */
