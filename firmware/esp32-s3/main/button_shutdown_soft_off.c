#include "button_shutdown_soft_off.h"

button_shutdown_boot_decision_t button_shutdown_soft_off_decide(bool soft_off_latched,
                                                               bool user_power_pressed) {
    if (!soft_off_latched) {
        return BUTTON_SHUTDOWN_BOOT_CONTINUE;
    }
    return user_power_pressed ? BUTTON_SHUTDOWN_BOOT_CLEAR_LATCH_AND_CONTINUE
                              : BUTTON_SHUTDOWN_BOOT_KEEP_SOFT_OFF;
}
