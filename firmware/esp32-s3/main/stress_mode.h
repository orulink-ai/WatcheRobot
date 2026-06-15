#ifndef WATCHER_STRESS_MODE_H
#define WATCHER_STRESS_MODE_H

#include "mcu_link.h"

#ifdef __cplusplus
extern "C" {
#endif

void stress_mode_init(void);
void stress_mode_on_link_event(const mcu_link_event_t *event);
void stress_mode_notify_ready(void);
void stress_mode_start(void);
void stress_mode_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_STRESS_MODE_H */
