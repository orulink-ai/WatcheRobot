#include "button_shutdown_power_channel.h"

static uint32_t min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

button_shutdown_power_channel_result_t
button_shutdown_power_channel_prepare(const button_shutdown_power_channel_ops_t *ops,
                                      uint32_t ready_wait_ms,
                                      uint32_t poll_ms) {
    button_shutdown_power_channel_result_t result = {
        .started = false,
        .ready = false,
        .start_status = -1,
        .last_poll_status = -1,
        .waited_ms = 0,
    };

    if (ops == 0 || ops->ensure_started == 0 || ops->is_ready == 0) {
        return result;
    }

    result.start_status = ops->ensure_started(ops->ctx);
    if (result.start_status != 0) {
        return result;
    }

    result.started = true;
    if (ops->is_ready(ops->ctx)) {
        result.ready = true;
        return result;
    }

    const uint32_t step_ms = poll_ms > 0u ? poll_ms : 1u;
    while (result.waited_ms < ready_wait_ms) {
        if (ops->poll_once != 0) {
            result.last_poll_status = ops->poll_once(ops->ctx);
        }
        if (ops->is_ready(ops->ctx)) {
            result.ready = true;
            break;
        }
        if (ops->delay_ms == 0) {
            break;
        }

        const uint32_t remaining_ms = ready_wait_ms - result.waited_ms;
        const uint32_t delay_ms = min_u32(step_ms, remaining_ms);
        ops->delay_ms(ops->ctx, delay_ms);
        result.waited_ms += delay_ms;
    }

    return result;
}
