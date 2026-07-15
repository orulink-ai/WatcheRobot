#ifndef BUTTON_SHUTDOWN_POWER_CHANNEL_H
#define BUTTON_SHUTDOWN_POWER_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*button_shutdown_power_action_fn_t)(void *ctx);
typedef bool (*button_shutdown_power_ready_fn_t)(void *ctx);
typedef void (*button_shutdown_power_delay_fn_t)(void *ctx, uint32_t delay_ms);

typedef struct {
    button_shutdown_power_action_fn_t ensure_started;
    button_shutdown_power_action_fn_t poll_once;
    button_shutdown_power_ready_fn_t is_ready;
    button_shutdown_power_delay_fn_t delay_ms;
    void *ctx;
} button_shutdown_power_channel_ops_t;

typedef struct {
    bool started;
    bool ready;
    int start_status;
    int last_poll_status;
    uint32_t waited_ms;
} button_shutdown_power_channel_result_t;

button_shutdown_power_channel_result_t
button_shutdown_power_channel_prepare(const button_shutdown_power_channel_ops_t *ops,
                                      uint32_t ready_wait_ms,
                                      uint32_t poll_ms);

#endif /* BUTTON_SHUTDOWN_POWER_CHANNEL_H */
