#include "control_ingress.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

int64_t control_ingress_host_esp_timer_now_us = 0;

typedef struct {
    int interrupt_calls;
    int move_sync_calls;
    int move_axis_calls;
    int jog_axis_calls;
    int jog_vector_calls;
    int stop_calls;
    bool last_is_x_axis;
    int last_x;
    int last_y;
    int last_angle;
    int last_duration;
    int last_velocity;
    int last_x_velocity;
    int last_y_velocity;
    int last_timeout;
    control_motion_source_t last_source;
    uint32_t next_seq;
} control_stub_t;

static control_stub_t s_stub;

static void reset_stub(void) {
    memset(&s_stub, 0, sizeof(s_stub));
    s_stub.next_seq = 42u;
    control_ingress_host_esp_timer_now_us = 0;
}

static esp_err_t stub_interrupt_action(const char *source) {
    assert(source != NULL);
    s_stub.interrupt_calls++;
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t stub_move_sync(int x_deg, int y_deg, int duration_ms, control_motion_source_t source,
                                uint32_t *out_seq) {
    s_stub.move_sync_calls++;
    s_stub.last_x = x_deg;
    s_stub.last_y = y_deg;
    s_stub.last_duration = duration_ms;
    s_stub.last_source = source;
    if (out_seq != NULL) {
        *out_seq = s_stub.next_seq;
    }
    return ESP_OK;
}

static esp_err_t stub_move_axis(bool is_x_axis, int angle_deg, int duration_ms, control_motion_source_t source,
                                uint32_t *out_seq) {
    s_stub.move_axis_calls++;
    s_stub.last_is_x_axis = is_x_axis;
    s_stub.last_angle = angle_deg;
    s_stub.last_duration = duration_ms;
    s_stub.last_source = source;
    if (out_seq != NULL) {
        *out_seq = s_stub.next_seq;
    }
    return ESP_OK;
}

static esp_err_t stub_jog_axis(bool is_x_axis, int velocity_deg_per_sec, int timeout_ms, control_motion_source_t source,
                               uint32_t *out_seq) {
    s_stub.jog_axis_calls++;
    s_stub.last_is_x_axis = is_x_axis;
    s_stub.last_velocity = velocity_deg_per_sec;
    s_stub.last_timeout = timeout_ms;
    s_stub.last_source = source;
    if (out_seq != NULL) {
        *out_seq = s_stub.next_seq;
    }
    return ESP_OK;
}

static esp_err_t stub_jog_vector(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec, int timeout_ms,
                                 control_motion_source_t source, uint32_t *out_seq) {
    s_stub.jog_vector_calls++;
    s_stub.last_x_velocity = x_velocity_deg_per_sec;
    s_stub.last_y_velocity = y_velocity_deg_per_sec;
    s_stub.last_timeout = timeout_ms;
    s_stub.last_source = source;
    if (out_seq != NULL) {
        *out_seq = s_stub.next_seq;
    }
    return ESP_OK;
}

static esp_err_t stub_stop(control_motion_source_t source) {
    s_stub.stop_calls++;
    s_stub.last_source = source;
    return ESP_OK;
}

static void install_stub_ops(void) {
    static const control_ingress_ops_t ops = {
        .interrupt_action = stub_interrupt_action,
        .move_sync = stub_move_sync,
        .move_axis = stub_move_axis,
        .jog_axis = stub_jog_axis,
        .jog_vector = stub_jog_vector,
        .stop = stub_stop,
    };

    control_ingress_set_ops_for_test(&ops);
}

static void test_rejects_invalid_servo_requests(void) {
    control_servo_request_t req = {
        .has_x = true,
        .x_deg = 90,
        .duration_ms = 100,
        .source = CONTROL_MOTION_SOURCE_WS,
    };

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_servo(NULL) == ESP_ERR_INVALID_ARG);

    req.duration_ms = 0;
    assert(control_ingress_submit_servo(&req) == ESP_ERR_INVALID_ARG);

    req.duration_ms = 100;
    req.has_x = false;
    req.has_y = false;
    assert(control_ingress_submit_servo(&req) == ESP_ERR_INVALID_ARG);

    req.has_x = true;
    req.x_deg = 181;
    assert(control_ingress_submit_servo(&req) == ESP_ERR_INVALID_ARG);
    assert(s_stub.interrupt_calls == 0);
    assert(s_stub.move_axis_calls == 0);
    assert(s_stub.move_sync_calls == 0);
}

static void test_single_axis_request_interrupts_once_and_preserves_source(void) {
    control_servo_request_t req = {
        .has_x = true,
        .x_deg = 45,
        .duration_ms = 250,
        .source = CONTROL_MOTION_SOURCE_BLE,
    };
    uint32_t seq = 0;

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_servo_with_seq(&req, &seq) == ESP_OK);
    assert(seq == 42u);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.move_axis_calls == 1);
    assert(s_stub.move_sync_calls == 0);
    assert(s_stub.last_is_x_axis);
    assert(s_stub.last_angle == 45);
    assert(s_stub.last_duration == 250);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_BLE);
}

static void test_dual_axis_request_preserves_source(void) {
    control_servo_request_t req = {
        .has_x = true,
        .has_y = true,
        .x_deg = 80,
        .y_deg = 120,
        .duration_ms = 300,
        .source = CONTROL_MOTION_SOURCE_WS,
    };

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_servo(&req) == ESP_OK);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.move_axis_calls == 0);
    assert(s_stub.move_sync_calls == 1);
    assert(s_stub.last_x == 80);
    assert(s_stub.last_y == 120);
    assert(s_stub.last_duration == 300);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_WS);
    assert(control_ingress_is_manual_touch_suppressed());
    assert(control_ingress_manual_touch_remaining_ms() == 10000u);

    control_ingress_host_esp_timer_now_us = 9999000;
    assert(control_ingress_is_manual_touch_suppressed());
    assert(control_ingress_manual_touch_remaining_ms() == 1u);

    control_ingress_host_esp_timer_now_us = 10000000;
    assert(!control_ingress_is_manual_touch_suppressed());
    assert(control_ingress_manual_touch_remaining_ms() == 0u);
}

static void test_behavior_source_does_not_suppress_manual_touch(void) {
    control_servo_request_t req = {
        .has_x = true,
        .has_y = true,
        .x_deg = 80,
        .y_deg = 120,
        .duration_ms = 300,
        .source = CONTROL_MOTION_SOURCE_BEHAVIOR,
    };

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_servo(&req) == ESP_OK);
    assert(!control_ingress_is_manual_touch_suppressed());
}

static void test_jog_request_interrupts_and_preserves_source(void) {
    control_jog_request_t req = {
        .is_x_axis = true,
        .velocity_deg_per_sec = -80,
        .timeout_ms = 300,
        .source = CONTROL_MOTION_SOURCE_BLE,
    };
    uint32_t seq = 0;

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_jog_with_seq(&req, &seq) == ESP_OK);
    assert(seq == 42u);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.jog_axis_calls == 1);
    assert(s_stub.last_is_x_axis);
    assert(s_stub.last_velocity == -80);
    assert(s_stub.last_timeout == 300);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_BLE);

    assert(control_ingress_stop_manual(CONTROL_MOTION_SOURCE_BLE) == ESP_OK);
    assert(s_stub.stop_calls == 1);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_BLE);
}

static void test_stop_interrupts_action_loop_before_servo_stop(void) {
    install_stub_ops();
    reset_stub();

    assert(control_ingress_stop_manual(CONTROL_MOTION_SOURCE_WS) == ESP_OK);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.stop_calls == 1);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_WS);
    assert(control_ingress_is_manual_touch_suppressed());
}

static void test_jog_vector_request_interrupts_and_preserves_both_axes(void) {
    control_jog_vector_request_t req = {
        .has_x = true,
        .has_y = true,
        .x_velocity_deg_per_sec = -90,
        .y_velocity_deg_per_sec = -120,
        .timeout_ms = 220,
        .source = CONTROL_MOTION_SOURCE_BLE,
    };
    uint32_t seq = 0;

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_jog_vector_with_seq(&req, &seq) == ESP_OK);
    assert(seq == 42u);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.jog_axis_calls == 0);
    assert(s_stub.jog_vector_calls == 1);
    assert(s_stub.last_x_velocity == -90);
    assert(s_stub.last_y_velocity == -120);
    assert(s_stub.last_timeout == 220);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_BLE);
}

static void test_state_queue_reports_timeout_when_full(void) {
    control_state_set_request_t req = {.state_id = "standby"};
    int i;

    assert(control_ingress_init() == ESP_OK);
    for (i = 0; i < 16; ++i) {
        assert(control_ingress_submit_state_set(&req) == ESP_OK);
    }
    assert(control_ingress_submit_state_set(&req) == ESP_ERR_TIMEOUT);
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"rejects_invalid_servo_requests", test_rejects_invalid_servo_requests},
        {"single_axis_request_interrupts_once_and_preserves_source",
         test_single_axis_request_interrupts_once_and_preserves_source},
        {"dual_axis_request_preserves_source", test_dual_axis_request_preserves_source},
        {"behavior_source_does_not_suppress_manual_touch", test_behavior_source_does_not_suppress_manual_touch},
        {"jog_request_interrupts_and_preserves_source", test_jog_request_interrupts_and_preserves_source},
        {"stop_interrupts_action_loop_before_servo_stop", test_stop_interrupts_action_loop_before_servo_stop},
        {"jog_vector_request_interrupts_and_preserves_both_axes",
         test_jog_vector_request_interrupts_and_preserves_both_axes},
        {"state_queue_reports_timeout_when_full", test_state_queue_reports_timeout_when_full},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    control_ingress_reset_ops_for_test();
    return 0;
}
