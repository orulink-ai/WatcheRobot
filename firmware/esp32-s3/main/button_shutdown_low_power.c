#include "button_shutdown_low_power.h"

static uint32_t min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

button_shutdown_low_power_result_t
button_shutdown_low_power_enter(const button_shutdown_low_power_ops_t *ops,
                                uint32_t wake_after_sec,
                                uint32_t wake_line_idle_wait_ms,
                                uint32_t wake_line_poll_ms) {
    button_shutdown_low_power_result_t result = {
        .sleep_requested = false,
        .wake_line_idle = false,
        .waited_ms = 0,
    };

    if (ops == 0 || ops->enter_sleep == 0) {
        return result;
    }

    if (ops->wake_line_level == 0) {
        result.wake_line_idle = true;
    } else {
        const uint32_t poll_ms = wake_line_poll_ms > 0u ? wake_line_poll_ms : 1u;

        while (true) {
            const int level = ops->wake_line_level(ops->ctx);
            if (level > 0) {
                result.wake_line_idle = true;
                break;
            }
            if (level < 0 || ops->delay_ms == 0 || result.waited_ms >= wake_line_idle_wait_ms) {
                break;
            }

            const uint32_t remaining_ms = wake_line_idle_wait_ms - result.waited_ms;
            const uint32_t step_ms = min_u32(poll_ms, remaining_ms);
            ops->delay_ms(ops->ctx, step_ms);
            result.waited_ms += step_ms;
        }
    }

    result.sleep_requested = true;
    ops->enter_sleep(ops->ctx, wake_after_sec);
    return result;
}
