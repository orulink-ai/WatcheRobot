#ifndef BUTTON_SHUTDOWN_SOFT_OFF_H
#define BUTTON_SHUTDOWN_SOFT_OFF_H

#include <stdbool.h>

typedef enum {
    BUTTON_SHUTDOWN_BOOT_CONTINUE = 0,
    BUTTON_SHUTDOWN_BOOT_CLEAR_LATCH_AND_CONTINUE,
    BUTTON_SHUTDOWN_BOOT_KEEP_SOFT_OFF,
} button_shutdown_boot_decision_t;

button_shutdown_boot_decision_t button_shutdown_soft_off_decide(bool soft_off_latched,
                                                               bool user_power_pressed);

#endif /* BUTTON_SHUTDOWN_SOFT_OFF_H */
