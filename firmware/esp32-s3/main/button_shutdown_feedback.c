#include "button_shutdown_feedback.h"

static uint32_t min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

button_shutdown_feedback_result_t
button_shutdown_feedback_run(const button_shutdown_feedback_ops_t *ops,
                             const char *sound_id,
                             uint32_t sound_wait_ms,
                             uint32_t sound_poll_ms) {
    button_shutdown_feedback_result_t result = {
        .play_status = -1,
        .sound_finished = false,
        .display_off_called = false,
        .waited_ms = 0,
    };

    if (ops == 0) {
        return result;
    }

    if (ops->play_sound != 0 && sound_id != 0 && sound_id[0] != '\0') {
        result.play_status = ops->play_sound(ops->ctx, sound_id);
    }

    if (result.play_status == 0 && ops->sound_busy != 0 && ops->delay_ms != 0) {
        const uint32_t poll_ms = sound_poll_ms > 0u ? sound_poll_ms : 1u;

        while (ops->sound_busy(ops->ctx)) {
            if (result.waited_ms >= sound_wait_ms) {
                break;
            }

            const uint32_t remaining_ms = sound_wait_ms - result.waited_ms;
            const uint32_t step_ms = min_u32(poll_ms, remaining_ms);
            ops->delay_ms(ops->ctx, step_ms);
            result.waited_ms += step_ms;
        }
        result.sound_finished = !ops->sound_busy(ops->ctx);
    } else {
        result.sound_finished = result.play_status == 0;
    }

    if (ops->display_off != 0) {
        ops->display_off(ops->ctx);
        result.display_off_called = true;
    }

    return result;
}
