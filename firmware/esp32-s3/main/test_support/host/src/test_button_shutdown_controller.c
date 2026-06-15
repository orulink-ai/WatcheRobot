#include "button_shutdown_sequence.h"
#include "button_shutdown_low_power.h"

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
    unsigned level_calls;
    unsigned delay_calls;
    unsigned sleep_calls;
    uint32_t waited_ms;
    uint32_t wake_after_sec;
} low_power_stub_t;

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
        .delay_ms = stub_low_power_delay,
        .enter_sleep = stub_enter_sleep,
        .ctx = &stub,
    };

    const button_shutdown_low_power_result_t result = button_shutdown_low_power_enter(&ops, 0u, 200u, 50u);

    assert(result.sleep_requested);
    assert(result.wake_line_idle);
    assert(result.waited_ms == 100u);
    assert(stub.level_calls == 3u);
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

    assert(result.sleep_requested);
    assert(!result.wake_line_idle);
    assert(result.waited_ms == 120u);
    assert(stub.level_calls == 4u);
    assert(stub.delay_calls == 3u);
    assert(stub.waited_ms == 120u);
    assert(stub.sleep_calls == 1u);
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"sequence_shuts_down_even_when_power_request_fails", test_sequence_shuts_down_even_when_power_request_fails},
        {"sequence_enters_low_power_fallback_if_shutdown_returns", test_sequence_enters_low_power_fallback_if_shutdown_returns},
        {"low_power_enters_sleep_when_wake_line_is_idle", test_low_power_enters_sleep_when_wake_line_is_idle},
        {"low_power_waits_for_wake_line_release_before_sleep", test_low_power_waits_for_wake_line_release_before_sleep},
        {"low_power_reports_wake_line_still_active_after_timeout",
         test_low_power_reports_wake_line_still_active_after_timeout},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
