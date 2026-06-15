#include "button_shutdown_sequence.h"

button_shutdown_sequence_result_t button_shutdown_sequence_run(const button_shutdown_sequence_ops_t *ops,
                                                               uint32_t settle_delay_ms,
                                                               uint32_t fallback_delay_ms) {
    button_shutdown_sequence_result_t result = {
        .power_off_status = -1,
        .system_shutdown_status = -1,
    };

    if (ops == 0 || ops->system_shutdown == 0) {
        return result;
    }

    if (ops->request_5v_off != 0) {
        result.power_off_status = ops->request_5v_off(ops->ctx);
    }

    if (ops->delay_ms != 0 && settle_delay_ms > 0u) {
        ops->delay_ms(ops->ctx, settle_delay_ms);
    }

    result.system_shutdown_status = ops->system_shutdown(ops->ctx);
    if (result.system_shutdown_status == 0 && ops->low_power_fallback != 0) {
        if (ops->delay_ms != 0 && fallback_delay_ms > 0u) {
            ops->delay_ms(ops->ctx, fallback_delay_ms);
        }
        ops->low_power_fallback(ops->ctx);
    }
    return result;
}
