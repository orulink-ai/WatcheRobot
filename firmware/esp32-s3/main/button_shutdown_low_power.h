#ifndef BUTTON_SHUTDOWN_LOW_POWER_H
#define BUTTON_SHUTDOWN_LOW_POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*button_shutdown_wake_level_fn_t)(void *ctx);
typedef void (*button_shutdown_low_power_delay_fn_t)(void *ctx, uint32_t delay_ms);
typedef void (*button_shutdown_enter_sleep_fn_t)(void *ctx, uint32_t wake_after_sec);
typedef void (*button_shutdown_prepare_idle_fn_t)(void *ctx);

typedef struct {
    button_shutdown_wake_level_fn_t wake_line_level;
    button_shutdown_wake_level_fn_t hold_line_level;
    button_shutdown_prepare_idle_fn_t prepare_idle;
    button_shutdown_low_power_delay_fn_t delay_ms;
    button_shutdown_enter_sleep_fn_t enter_sleep;
    void *ctx;
} button_shutdown_low_power_ops_t;

typedef struct {
    bool sleep_requested;
    bool wake_line_idle;
    bool hold_line_idle;
    uint32_t waited_ms;
} button_shutdown_low_power_result_t;

button_shutdown_low_power_result_t
button_shutdown_low_power_enter(const button_shutdown_low_power_ops_t *ops,
                                uint32_t wake_after_sec,
                                uint32_t wake_line_idle_wait_ms,
                                uint32_t wake_line_poll_ms);

#endif /* BUTTON_SHUTDOWN_LOW_POWER_H */
