#ifndef BUTTON_SHUTDOWN_SEQUENCE_H
#define BUTTON_SHUTDOWN_SEQUENCE_H

#include <stdint.h>

typedef int (*button_shutdown_action_fn_t)(void *ctx);
typedef void (*button_shutdown_delay_fn_t)(void *ctx, uint32_t delay_ms);
typedef void (*button_shutdown_fallback_fn_t)(void *ctx);

typedef struct {
    button_shutdown_action_fn_t request_5v_off;
    button_shutdown_delay_fn_t delay_ms;
    button_shutdown_action_fn_t system_shutdown;
    button_shutdown_fallback_fn_t low_power_fallback;
    void *ctx;
} button_shutdown_sequence_ops_t;

typedef struct {
    int power_off_status;
    int system_shutdown_status;
} button_shutdown_sequence_result_t;

button_shutdown_sequence_result_t button_shutdown_sequence_run(const button_shutdown_sequence_ops_t *ops,
                                                               uint32_t settle_delay_ms,
                                                               uint32_t fallback_delay_ms);

#endif /* BUTTON_SHUTDOWN_SEQUENCE_H */
