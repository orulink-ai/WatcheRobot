#ifndef BUTTON_SHUTDOWN_FEEDBACK_H
#define BUTTON_SHUTDOWN_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*button_shutdown_feedback_play_fn_t)(void *ctx, const char *sound_id);
typedef bool (*button_shutdown_feedback_busy_fn_t)(void *ctx);
typedef void (*button_shutdown_feedback_action_fn_t)(void *ctx);
typedef void (*button_shutdown_feedback_delay_fn_t)(void *ctx, uint32_t delay_ms);

typedef struct {
    button_shutdown_feedback_play_fn_t play_sound;
    button_shutdown_feedback_busy_fn_t sound_busy;
    button_shutdown_feedback_action_fn_t display_off;
    button_shutdown_feedback_delay_fn_t delay_ms;
    void *ctx;
} button_shutdown_feedback_ops_t;

typedef struct {
    int play_status;
    bool sound_finished;
    bool display_off_called;
    uint32_t waited_ms;
} button_shutdown_feedback_result_t;

button_shutdown_feedback_result_t
button_shutdown_feedback_run(const button_shutdown_feedback_ops_t *ops,
                             const char *sound_id,
                             uint32_t sound_wait_ms,
                             uint32_t sound_poll_ms);

#endif /* BUTTON_SHUTDOWN_FEEDBACK_H */
