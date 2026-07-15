#include "button_shutdown_feedback.h"
#include "button_shutdown_hold_detector.h"
#include "button_shutdown_low_power.h"
#include "button_shutdown_power_channel.h"
#include "button_shutdown_sequence.h"
#include "button_shutdown_soft_off.h"

#include <assert.h>
#include <stdio.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    int power_status;
    int shutdown_status;
    unsigned power_calls;
    unsigned delay_calls;
    unsigned shutdown_calls;
    unsigned fallback_calls;
    uint32_t last_delay_ms;
} shutdown_sequence_stub_t;

typedef struct {
    const int *levels;
    unsigned level_count;
    unsigned next_level;
    const int *hold_levels;
    unsigned hold_level_count;
    unsigned next_hold_level;
    unsigned level_calls;
    unsigned hold_level_calls;
    unsigned prepare_calls;
    unsigned delay_calls;
    unsigned sleep_calls;
    uint32_t waited_ms;
    uint32_t wake_after_sec;
} low_power_stub_t;

typedef struct {
    int start_status;
    unsigned ready_after_polls;
    unsigned start_calls;
    unsigned poll_calls;
    unsigned ready_calls;
    unsigned delay_calls;
    uint32_t waited_ms;
} power_channel_stub_t;

typedef struct {
    int play_status;
    unsigned play_calls;
    unsigned busy_true_calls;
    unsigned busy_calls;
    unsigned delay_calls;
    unsigned display_off_calls;
    uint32_t waited_ms;
    const char *last_sound_id;
} feedback_stub_t;

static int stub_request_power_off(void *ctx) {
    shutdown_sequence_stub_t *stub = (shutdown_sequence_stub_t *)ctx;

    stub->power_calls++;
    return stub->power_status;
}

static void stub_delay(void *ctx, uint32_t delay_ms) {
    shutdown_sequence_stub_t *stub = (shutdown_sequence_stub_t *)ctx;

    stub->delay_calls++;
    stub->last_delay_ms = delay_ms;
}

static int stub_system_shutdown(void *ctx) {
    shutdown_sequence_stub_t *stub = (shutdown_sequence_stub_t *)ctx;

    stub->shutdown_calls++;
    return stub->shutdown_status;
}

static void stub_low_power_fallback(void *ctx) {
    shutdown_sequence_stub_t *stub = (shutdown_sequence_stub_t *)ctx;

    stub->fallback_calls++;
}

static int stub_wake_line_level(void *ctx) {
    low_power_stub_t *stub = (low_power_stub_t *)ctx;
    int level = 1;

    if (stub->next_level < stub->level_count) {
        level = stub->levels[stub->next_level];
        stub->next_level++;
    } else if (stub->level_count > 0u) {
        level = stub->levels[stub->level_count - 1u];
    }
    stub->level_calls++;
    return level;
}

static int stub_hold_line_level(void *ctx) {
    low_power_stub_t *stub = (low_power_stub_t *)ctx;
    int level = 1;

    if (stub->next_hold_level < stub->hold_level_count) {
        level = stub->hold_levels[stub->next_hold_level];
        stub->next_hold_level++;
    } else if (stub->hold_level_count > 0u) {
        level = stub->hold_levels[stub->hold_level_count - 1u];
    }
    stub->hold_level_calls++;
    return level;
}

static void stub_prepare_idle(void *ctx) {
    low_power_stub_t *stub = (low_power_stub_t *)ctx;

    stub->prepare_calls++;
}

static void stub_low_power_delay(void *ctx, uint32_t delay_ms) {
    low_power_stub_t *stub = (low_power_stub_t *)ctx;

    stub->delay_calls++;
    stub->waited_ms += delay_ms;
}

static void stub_enter_sleep(void *ctx, uint32_t wake_after_sec) {
    low_power_stub_t *stub = (low_power_stub_t *)ctx;

    stub->sleep_calls++;
    stub->wake_after_sec = wake_after_sec;
}

static int stub_power_channel_start(void *ctx) {
    power_channel_stub_t *stub = (power_channel_stub_t *)ctx;

    stub->start_calls++;
    return stub->start_status;
}

static int stub_power_channel_poll(void *ctx) {
    power_channel_stub_t *stub = (power_channel_stub_t *)ctx;

    stub->poll_calls++;
    return -2;
}

static bool stub_power_channel_ready(void *ctx) {
    power_channel_stub_t *stub = (power_channel_stub_t *)ctx;

    stub->ready_calls++;
    return stub->poll_calls >= stub->ready_after_polls;
}

static void stub_power_channel_delay(void *ctx, uint32_t delay_ms) {
    power_channel_stub_t *stub = (power_channel_stub_t *)ctx;

    stub->delay_calls++;
    stub->waited_ms += delay_ms;
}

static int stub_feedback_play(void *ctx, const char *sound_id) {
    feedback_stub_t *stub = (feedback_stub_t *)ctx;

    stub->play_calls++;
    stub->last_sound_id = sound_id;
    return stub->play_status;
}

static bool stub_feedback_busy(void *ctx) {
    feedback_stub_t *stub = (feedback_stub_t *)ctx;
    bool busy = stub->busy_calls < stub->busy_true_calls;

    stub->busy_calls++;
    return busy;
}

static void stub_feedback_delay(void *ctx, uint32_t delay_ms) {
    feedback_stub_t *stub = (feedback_stub_t *)ctx;

    stub->delay_calls++;
    stub->waited_ms += delay_ms;
}

static void stub_feedback_display_off(void *ctx) {
    feedback_stub_t *stub = (feedback_stub_t *)ctx;

    stub->display_off_calls++;
}

static void test_sequence_shuts_down_even_when_power_request_fails(void) {
    shutdown_sequence_stub_t stub = {
        .power_status = -7,
        .shutdown_status = 0,
    };
    const button_shutdown_sequence_ops_t ops = {
        .request_5v_off = stub_request_power_off,
        .delay_ms = stub_delay,
        .system_shutdown = stub_system_shutdown,
        .low_power_fallback = NULL,
        .ctx = &stub,
    };

    const button_shutdown_sequence_result_t result = button_shutdown_sequence_run(&ops, 300u, 0u);

    assert(result.power_off_status == -7);
    assert(result.system_shutdown_status == 0);
    assert(stub.power_calls == 1u);
    assert(stub.delay_calls == 1u);
    assert(stub.last_delay_ms == 300u);
    assert(stub.shutdown_calls == 1u);
}

static void test_sequence_enters_low_power_fallback_if_shutdown_returns(void) {
    shutdown_sequence_stub_t stub = {
        .power_status = 0,
        .shutdown_status = 0,
    };
    const button_shutdown_sequence_ops_t ops = {
        .request_5v_off = stub_request_power_off,
        .delay_ms = stub_delay,
        .system_shutdown = stub_system_shutdown,
        .low_power_fallback = stub_low_power_fallback,
        .ctx = &stub,
    };

    const button_shutdown_sequence_result_t result = button_shutdown_sequence_run(&ops, 300u, 800u);

    assert(result.power_off_status == 0);
    assert(result.system_shutdown_status == 0);
    assert(stub.power_calls == 1u);
    assert(stub.delay_calls == 2u);
    assert(stub.last_delay_ms == 800u);
    assert(stub.shutdown_calls == 1u);
    assert(stub.fallback_calls == 1u);
}

static void test_low_power_enters_sleep_when_wake_line_is_idle(void) {
    const int levels[] = {1};
    low_power_stub_t stub = {
        .levels = levels,
        .level_count = ARRAY_SIZE(levels),
    };
    const button_shutdown_low_power_ops_t ops = {
        .wake_line_level = stub_wake_line_level,
        .delay_ms = stub_low_power_delay,
        .enter_sleep = stub_enter_sleep,
        .ctx = &stub,
    };

    const button_shutdown_low_power_result_t result = button_shutdown_low_power_enter(&ops, 0u, 200u, 50u);

    assert(result.sleep_requested);
    assert(result.wake_line_idle);
    assert(result.waited_ms == 0u);
    assert(stub.level_calls == 1u);
    assert(stub.delay_calls == 0u);
    assert(stub.sleep_calls == 1u);
    assert(stub.wake_after_sec == 0u);
}

static void test_low_power_waits_for_wake_line_release_before_sleep(void) {
    const int levels[] = {0, 0, 1};
    low_power_stub_t stub = {
        .levels = levels,
        .level_count = ARRAY_SIZE(levels),
    };
    const button_shutdown_low_power_ops_t ops = {
        .wake_line_level = stub_wake_line_level,
        .prepare_idle = stub_prepare_idle,
        .delay_ms = stub_low_power_delay,
        .enter_sleep = stub_enter_sleep,
        .ctx = &stub,
    };

    const button_shutdown_low_power_result_t result = button_shutdown_low_power_enter(&ops, 0u, 200u, 50u);

    assert(result.sleep_requested);
    assert(result.wake_line_idle);
    assert(result.waited_ms == 100u);
    assert(stub.level_calls == 3u);
    assert(stub.prepare_calls == 3u);
    assert(stub.delay_calls == 2u);
    assert(stub.waited_ms == 100u);
    assert(stub.sleep_calls == 1u);
}

static void test_low_power_reports_wake_line_still_active_after_timeout(void) {
    const int levels[] = {0};
    low_power_stub_t stub = {
        .levels = levels,
        .level_count = ARRAY_SIZE(levels),
    };
    const button_shutdown_low_power_ops_t ops = {
        .wake_line_level = stub_wake_line_level,
        .delay_ms = stub_low_power_delay,
        .enter_sleep = stub_enter_sleep,
        .ctx = &stub,
    };

    const button_shutdown_low_power_result_t result = button_shutdown_low_power_enter(&ops, 0u, 120u, 50u);

    assert(!result.sleep_requested);
    assert(!result.wake_line_idle);
    assert(result.hold_line_idle);
    assert(result.waited_ms == 120u);
    assert(stub.level_calls == 4u);
    assert(stub.delay_calls == 3u);
    assert(stub.waited_ms == 120u);
    assert(stub.sleep_calls == 0u);
}

static void test_low_power_waits_for_button_release_before_sleep(void) {
    const int wake_levels[] = {1};
    const int hold_levels[] = {0, 0, 1};
    low_power_stub_t stub = {
        .levels = wake_levels,
        .level_count = ARRAY_SIZE(wake_levels),
        .hold_levels = hold_levels,
        .hold_level_count = ARRAY_SIZE(hold_levels),
    };
    const button_shutdown_low_power_ops_t ops = {
        .wake_line_level = stub_wake_line_level,
        .hold_line_level = stub_hold_line_level,
        .delay_ms = stub_low_power_delay,
        .enter_sleep = stub_enter_sleep,
        .ctx = &stub,
    };

    const button_shutdown_low_power_result_t result = button_shutdown_low_power_enter(&ops, 0u, 200u, 50u);

    assert(result.sleep_requested);
    assert(result.wake_line_idle);
    assert(result.hold_line_idle);
    assert(result.waited_ms == 100u);
    assert(stub.hold_level_calls == 3u);
    assert(stub.sleep_calls == 1u);
}

static void test_power_channel_polls_until_ready(void) {
    power_channel_stub_t stub = {
        .start_status = 0,
        .ready_after_polls = 3u,
    };
    const button_shutdown_power_channel_ops_t ops = {
        .ensure_started = stub_power_channel_start,
        .poll_once = stub_power_channel_poll,
        .is_ready = stub_power_channel_ready,
        .delay_ms = stub_power_channel_delay,
        .ctx = &stub,
    };

    const button_shutdown_power_channel_result_t result = button_shutdown_power_channel_prepare(&ops, 500u, 50u);

    assert(result.started);
    assert(result.ready);
    assert(result.start_status == 0);
    assert(result.last_poll_status == -2);
    assert(result.waited_ms == 100u);
    assert(stub.start_calls == 1u);
    assert(stub.poll_calls == 3u);
    assert(stub.delay_calls == 2u);
}

static void test_power_channel_keeps_shutdown_available_when_not_ready(void) {
    power_channel_stub_t stub = {
        .start_status = 0,
        .ready_after_polls = 100u,
    };
    const button_shutdown_power_channel_ops_t ops = {
        .ensure_started = stub_power_channel_start,
        .poll_once = stub_power_channel_poll,
        .is_ready = stub_power_channel_ready,
        .delay_ms = stub_power_channel_delay,
        .ctx = &stub,
    };

    const button_shutdown_power_channel_result_t result = button_shutdown_power_channel_prepare(&ops, 120u, 50u);

    assert(result.started);
    assert(!result.ready);
    assert(result.start_status == 0);
    assert(result.waited_ms == 120u);
    assert(stub.start_calls == 1u);
    assert(stub.poll_calls == 3u);
}

static void test_feedback_waits_for_sound_before_turning_display_off(void) {
    feedback_stub_t stub = {
        .play_status = 0,
        .busy_true_calls = 2u,
    };
    const button_shutdown_feedback_ops_t ops = {
        .play_sound = stub_feedback_play,
        .sound_busy = stub_feedback_busy,
        .display_off = stub_feedback_display_off,
        .delay_ms = stub_feedback_delay,
        .ctx = &stub,
    };

    const button_shutdown_feedback_result_t result = button_shutdown_feedback_run(&ops, "error", 500u, 50u);

    assert(result.play_status == 0);
    assert(result.sound_finished);
    assert(result.display_off_called);
    assert(result.waited_ms == 100u);
    assert(stub.play_calls == 1u);
    assert(stub.last_sound_id != NULL);
    assert(stub.delay_calls == 2u);
    assert(stub.waited_ms == 100u);
    assert(stub.display_off_calls == 1u);
}

static void test_feedback_turns_display_off_when_sound_wait_times_out(void) {
    feedback_stub_t stub = {
        .play_status = 0,
        .busy_true_calls = 10u,
    };
    const button_shutdown_feedback_ops_t ops = {
        .play_sound = stub_feedback_play,
        .sound_busy = stub_feedback_busy,
        .display_off = stub_feedback_display_off,
        .delay_ms = stub_feedback_delay,
        .ctx = &stub,
    };

    const button_shutdown_feedback_result_t result = button_shutdown_feedback_run(&ops, "error", 120u, 50u);

    assert(result.play_status == 0);
    assert(!result.sound_finished);
    assert(result.display_off_called);
    assert(result.waited_ms == 120u);
    assert(stub.delay_calls == 3u);
    assert(stub.display_off_calls == 1u);
}

static void test_feedback_turns_display_off_when_sound_request_fails(void) {
    feedback_stub_t stub = {
        .play_status = -7,
        .busy_true_calls = 10u,
    };
    const button_shutdown_feedback_ops_t ops = {
        .play_sound = stub_feedback_play,
        .sound_busy = stub_feedback_busy,
        .display_off = stub_feedback_display_off,
        .delay_ms = stub_feedback_delay,
        .ctx = &stub,
    };

    const button_shutdown_feedback_result_t result = button_shutdown_feedback_run(&ops, "error", 500u, 50u);

    assert(result.play_status == -7);
    assert(!result.sound_finished);
    assert(result.display_off_called);
    assert(result.waited_ms == 0u);
    assert(stub.busy_calls == 0u);
    assert(stub.delay_calls == 0u);
    assert(stub.display_off_calls == 1u);
}

static void test_soft_off_boot_decision_continues_when_not_latched(void) {
    assert(button_shutdown_soft_off_decide(false, false) == BUTTON_SHUTDOWN_BOOT_CONTINUE);
    assert(button_shutdown_soft_off_decide(false, true) == BUTTON_SHUTDOWN_BOOT_CONTINUE);
}

static void test_soft_off_boot_decision_clears_latch_on_user_press(void) {
    assert(button_shutdown_soft_off_decide(true, true) == BUTTON_SHUTDOWN_BOOT_CLEAR_LATCH_AND_CONTINUE);
}

static void test_soft_off_boot_decision_keeps_soft_off_without_user_press(void) {
    assert(button_shutdown_soft_off_decide(true, false) == BUTTON_SHUTDOWN_BOOT_KEEP_SOFT_OFF);
}

static void test_hold_detector_debounces_and_fires_once_per_hold(void) {
    button_shutdown_hold_detector_t detector;
    unsigned fire_count = 0u;

    button_shutdown_hold_detector_init(&detector, 150u, 3000u, 500u);
    for (unsigned elapsed = 0u; elapsed < 4000u; elapsed += 50u) {
        if (button_shutdown_hold_detector_update(&detector, true, 50u)) {
            ++fire_count;
        }
    }

    assert(fire_count == 1u);
}

static void test_hold_detector_requires_stable_release_before_rearming(void) {
    button_shutdown_hold_detector_t detector;
    unsigned fire_count = 0u;

    button_shutdown_hold_detector_init(&detector, 150u, 3000u, 500u);
    for (unsigned elapsed = 0u; elapsed < 3400u; elapsed += 50u) {
        fire_count += button_shutdown_hold_detector_update(&detector, true, 50u) ? 1u : 0u;
    }
    for (unsigned elapsed = 0u; elapsed < 300u; elapsed += 50u) {
        (void)button_shutdown_hold_detector_update(&detector, false, 50u);
    }
    for (unsigned elapsed = 0u; elapsed < 3400u; elapsed += 50u) {
        fire_count += button_shutdown_hold_detector_update(&detector, true, 50u) ? 1u : 0u;
    }
    assert(fire_count == 1u);

    for (unsigned elapsed = 0u; elapsed < 700u; elapsed += 50u) {
        (void)button_shutdown_hold_detector_update(&detector, false, 50u);
    }
    for (unsigned elapsed = 0u; elapsed < 3400u; elapsed += 50u) {
        fire_count += button_shutdown_hold_detector_update(&detector, true, 50u) ? 1u : 0u;
    }
    assert(fire_count == 2u);
}

static void test_hold_detector_rejects_bounce_before_debounce_window(void) {
    button_shutdown_hold_detector_t detector;

    button_shutdown_hold_detector_init(&detector, 150u, 3000u, 500u);
    for (unsigned cycle = 0u; cycle < 20u; ++cycle) {
        assert(!button_shutdown_hold_detector_update(&detector, true, 50u));
        assert(!button_shutdown_hold_detector_update(&detector, false, 50u));
    }
    assert(!detector.stable_pressed);
    assert(detector.held_ms == 0u);
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"sequence_shuts_down_even_when_power_request_fails", test_sequence_shuts_down_even_when_power_request_fails},
        {"sequence_enters_low_power_fallback_if_shutdown_returns",
         test_sequence_enters_low_power_fallback_if_shutdown_returns},
        {"low_power_enters_sleep_when_wake_line_is_idle", test_low_power_enters_sleep_when_wake_line_is_idle},
        {"low_power_waits_for_wake_line_release_before_sleep", test_low_power_waits_for_wake_line_release_before_sleep},
        {"low_power_reports_wake_line_still_active_after_timeout",
         test_low_power_reports_wake_line_still_active_after_timeout},
        {"low_power_waits_for_button_release_before_sleep", test_low_power_waits_for_button_release_before_sleep},
        {"power_channel_polls_until_ready", test_power_channel_polls_until_ready},
        {"power_channel_keeps_shutdown_available_when_not_ready",
         test_power_channel_keeps_shutdown_available_when_not_ready},
        {"feedback_waits_for_sound_before_turning_display_off",
         test_feedback_waits_for_sound_before_turning_display_off},
        {"feedback_turns_display_off_when_sound_wait_times_out",
         test_feedback_turns_display_off_when_sound_wait_times_out},
        {"feedback_turns_display_off_when_sound_request_fails",
         test_feedback_turns_display_off_when_sound_request_fails},
        {"soft_off_boot_decision_continues_when_not_latched", test_soft_off_boot_decision_continues_when_not_latched},
        {"soft_off_boot_decision_clears_latch_on_user_press", test_soft_off_boot_decision_clears_latch_on_user_press},
        {"soft_off_boot_decision_keeps_soft_off_without_user_press",
         test_soft_off_boot_decision_keeps_soft_off_without_user_press},
        {"hold_detector_debounces_and_fires_once_per_hold", test_hold_detector_debounces_and_fires_once_per_hold},
        {"hold_detector_requires_stable_release_before_rearming",
         test_hold_detector_requires_stable_release_before_rearming},
        {"hold_detector_rejects_bounce_before_debounce_window",
         test_hold_detector_rejects_bounce_before_debounce_window},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
