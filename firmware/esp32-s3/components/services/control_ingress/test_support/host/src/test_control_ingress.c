#include "control_ingress.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

int64_t control_ingress_host_esp_timer_now_us = 0;
unsigned control_ingress_host_last_task_stack_depth = 0;
char control_ingress_host_last_state_id[64] = "";
char control_ingress_host_last_anim_id[64] = "";
char control_ingress_host_last_action_id[64] = "";
char control_ingress_host_last_sound_id[64] = "";
bool control_ingress_host_uses_default_sound = false;

typedef struct {
    int interrupt_calls;
    int move_sync_calls;
    int move_axis_calls;
    int move_sync_direct_calls;
    int move_axis_direct_calls;
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
    control_ingress_host_last_state_id[0] = '\0';
    control_ingress_host_last_anim_id[0] = '\0';
    control_ingress_host_last_action_id[0] = '\0';
    control_ingress_host_last_sound_id[0] = '\0';
    control_ingress_host_uses_default_sound = false;
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

static esp_err_t stub_move_sync_direct(int x_deg, int y_deg, control_motion_source_t source, uint32_t *out_seq) {
    s_stub.move_sync_direct_calls++;
    s_stub.last_x = x_deg;
    s_stub.last_y = y_deg;
    s_stub.last_source = source;
    if (out_seq != NULL) {
        *out_seq = s_stub.next_seq;
    }
    return ESP_OK;
}

static esp_err_t stub_move_axis_direct(bool is_x_axis, int angle_deg, control_motion_source_t source,
                                       uint32_t *out_seq) {
    s_stub.move_axis_direct_calls++;
    s_stub.last_is_x_axis = is_x_axis;
    s_stub.last_angle = angle_deg;
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
        .move_sync_direct = stub_move_sync_direct,
        .move_axis_direct = stub_move_axis_direct,
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
    assert(s_stub.move_axis_direct_calls == 0);
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
    assert(s_stub.move_sync_calls == 0);
    assert(s_stub.move_sync_direct_calls == 1);
    assert(s_stub.last_x == 80);
    assert(s_stub.last_y == 120);
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

static void test_ws_single_axis_request_uses_direct_target(void) {
    control_servo_request_t req = {
        .has_y = true,
        .y_deg = 110,
        .duration_ms = 250,
        .source = CONTROL_MOTION_SOURCE_WS,
    };
    uint32_t seq = 0;

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_servo_with_seq(&req, &seq) == ESP_OK);
    assert(seq == 42u);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.move_axis_calls == 0);
    assert(s_stub.move_axis_direct_calls == 1);
    assert(!s_stub.last_is_x_axis);
    assert(s_stub.last_angle == 110);
    assert(s_stub.last_source == CONTROL_MOTION_SOURCE_WS);
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

static void test_jog_vector_stream_interrupts_only_on_stream_start(void) {
    control_jog_vector_request_t req = {
        .has_x = true,
        .has_y = true,
        .x_velocity_deg_per_sec = 60,
        .y_velocity_deg_per_sec = -45,
        .timeout_ms = 240,
        .source = CONTROL_MOTION_SOURCE_BLE,
    };

    install_stub_ops();
    reset_stub();
    assert(control_ingress_submit_jog_vector(&req) == ESP_OK);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.jog_vector_calls == 1);

    control_ingress_host_esp_timer_now_us = 120000;
    req.x_velocity_deg_per_sec = 80;
    req.y_velocity_deg_per_sec = -20;
    assert(control_ingress_submit_jog_vector(&req) == ESP_OK);
    assert(s_stub.interrupt_calls == 1);
    assert(s_stub.jog_vector_calls == 2);
    assert(s_stub.last_x_velocity == 80);
    assert(s_stub.last_y_velocity == -20);

    control_ingress_host_esp_timer_now_us = 361000;
    req.x_velocity_deg_per_sec = -70;
    req.y_velocity_deg_per_sec = 30;
    assert(control_ingress_submit_jog_vector(&req) == ESP_OK);
    assert(s_stub.interrupt_calls == 2);
    assert(s_stub.jog_vector_calls == 3);
    assert(s_stub.last_x_velocity == -70);
    assert(s_stub.last_y_velocity == 30);
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

static void test_state_task_has_crash_safe_stack_budget(void) {
    control_ingress_host_last_task_stack_depth = 0;

    assert(control_ingress_init() == ESP_OK);
    assert(control_ingress_host_last_task_stack_depth >= 10240u);
    assert(control_ingress_state_stack_size() == control_ingress_host_last_task_stack_depth);
}

static void assert_normalizes_resource(const char *raw, const char *expected) {
    char normalized[64];

    memset(normalized, 0xa5, sizeof(normalized));
    control_ingress_normalize_resource_name_for_test(raw, normalized, sizeof(normalized));
    assert(strcmp(normalized, expected) == 0);
}

static void test_resource_names_accept_design_export_aliases(void) {
    assert_normalizes_resource("watcher_smile_10fps.json", "smile");
    assert_normalizes_resource("2026.05.09/actions/watcher_fondle_love_10fps.json", "fondle_love");
    assert_normalizes_resource("watcher-fondle-anger-10fps.json", "fondle_anger");
    assert_normalizes_resource("look1.gif", "standby1");
    assert_normalizes_resource("look2", "standby2");
    assert_normalizes_resource("look3.gif", "standby3");
    assert_normalizes_resource("look4.gif", "standby4");
}

static void test_ai_status_voice_flow_states_clear_text(void) {
    control_ai_status_request_t req = {0};
    const char *text;

    snprintf(req.status, sizeof(req.status), "%s", "thinking");
    snprintf(req.message, sizeof(req.message), "%s", "Thinking about reply");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);

    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "brain_update");
    snprintf(req.message, sizeof(req.message), "%s", "processing request");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);

    memset(&req, 0, sizeof(req));
    snprintf(req.image_name, sizeof(req.image_name), "%s", "watcher_speaking_10fps.json");
    snprintf(req.message, sizeof(req.message), "%s", "Speaking response");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);

    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "custom3");
    snprintf(req.message, sizeof(req.message), "%s", "codex MCP tool call started: codex");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);
}

static void test_ai_status_empty_message_clears_text(void) {
    control_ai_status_request_t req = {0};
    const char *text;

    snprintf(req.status, sizeof(req.status), "%s", "happy");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);
}

static void test_all_ai_statuses_clear_text(void) {
    control_ai_status_request_t req = {0};
    const char *text;

    snprintf(req.status, sizeof(req.status), "%s", "error");
    snprintf(req.message, sizeof(req.message), "%s", "Cloud Offline");
    text = control_ingress_ai_status_text_for_test(&req);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);
}

static void submit_ai_status_for_completion_state(const char *status, const char *message) {
    control_ai_status_request_t req = {0};

    snprintf(req.status, sizeof(req.status), "%s", status);
    if (message != NULL) {
        snprintf(req.message, sizeof(req.message), "%s", message);
    }
    (void)control_ingress_submit_ai_status(&req);
}

static void test_tts_completion_defaults_to_happy_without_task_status(void) {
    control_ingress_reset_ai_task_state_for_test();

    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
    submit_ai_status_for_completion_state("thinking", "");
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_tts_completion_keeps_task_working_after_first_reply(void) {
    control_ingress_reset_ai_task_state_for_test();

    submit_ai_status_for_completion_state("custom2", "");
    assert(strcmp(control_ingress_tts_completion_state(), "custom2") == 0);

    submit_ai_status_for_completion_state("speaking", "");
    assert(strcmp(control_ingress_tts_completion_state(), "custom2") == 0);
}

static void test_tts_completion_uses_terminal_task_status(void) {
    control_ingress_reset_ai_task_state_for_test();

    submit_ai_status_for_completion_state("custom2", "");
    submit_ai_status_for_completion_state("happy", "done");
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);

    submit_ai_status_for_completion_state("custom2", "");
    submit_ai_status_for_completion_state("failed", "");
    assert(strcmp(control_ingress_tts_completion_state(), "error") == 0);

    submit_ai_status_for_completion_state("thinking", "");
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_deferred_ai_status_updates_completion_without_replacing_speaking(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "completed");
    req.defer_ui_until_tts_complete = true;

    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_suppressed_ai_status_updates_lifecycle_without_enqueuing_ui(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "thinking");
    req.suppress_ui = true;

    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_authoritative_task_count_keeps_foreground_execution_active(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "custom2");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "task");
    req.has_active_task_count = true;
    req.active_task_count = 2;
    (void)control_ingress_submit_ai_status(&req);
    assert(control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "custom2") == 0);

    snprintf(req.status, sizeof(req.status), "%s", "happy");
    req.active_task_count = 1;
    (void)control_ingress_submit_ai_status(&req);
    assert(control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "custom2") == 0);

    req.active_task_count = 0;
    (void)control_ingress_submit_ai_status(&req);
    assert(!control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_dialogue_terminal_does_not_release_active_task(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "custom2");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "task");
    req.has_active_task_count = true;
    req.active_task_count = 1;
    (void)control_ingress_submit_ai_status(&req);

    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "happy");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "dialogue");
    (void)control_ingress_submit_ai_status(&req);
    assert(control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "custom2") == 0);
}

static void test_transport_disconnect_clears_active_task_lifecycle(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "custom2");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "task");
    req.has_active_task_count = true;
    req.active_task_count = 1;
    (void)control_ingress_submit_ai_status(&req);
    assert(control_ingress_has_active_ai_task());

    control_ingress_clear_active_ai_tasks();

    assert(!control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_processing_and_tool_calling_use_distinct_existing_states(void) {
    control_ai_status_request_t req = {0};

    reset_stub();
    snprintf(req.status, sizeof(req.status), "%s", "processing");
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(strcmp(control_ingress_host_last_state_id, "processing") == 0);

    reset_stub();
    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "tool_calling");
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(strcmp(control_ingress_host_last_state_id, "custom3") == 0);

    reset_stub();
    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "custom3");
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(strcmp(control_ingress_host_last_state_id, "custom3") == 0);
}

static void test_ai_status_empty_sound_uses_default_and_nonempty_sound_overrides(void) {
    control_ai_status_request_t req = {0};

    /* Transport parsers intentionally collapse an omitted sound_file and an
     * explicitly empty sound_file into the same empty request buffer. */
    reset_stub();
    snprintf(req.status, sizeof(req.status), "%s", "thinking");
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(control_ingress_host_uses_default_sound);
    assert(strcmp(control_ingress_host_last_sound_id, "") == 0);

    reset_stub();
    snprintf(req.sound_file, sizeof(req.sound_file), "%s", "thinking");
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(!control_ingress_host_uses_default_sound);
    assert(strcmp(control_ingress_host_last_sound_id, "thinking") == 0);
}

static void test_explicit_empty_action_file_does_not_infer_status_action(void) {
    control_ai_status_request_t req = {0};

    reset_stub();
    snprintf(req.status, sizeof(req.status), "%s", "custom2");
    snprintf(req.image_name, sizeof(req.image_name), "%s", "custom2");
    req.has_action_file = true;
    req.action_file[0] = '\0';

    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(strcmp(control_ingress_host_last_state_id, "custom2") == 0);
    assert(strcmp(control_ingress_host_last_anim_id, "custom2") == 0);
    assert(strcmp(control_ingress_host_last_action_id, "") == 0);

    reset_stub();
    req.has_action_file = false;
    assert(control_ingress_apply_ai_status_for_test(&req) == ESP_OK);
    assert(strcmp(control_ingress_host_last_action_id, "custom2") == 0);
}

static void test_processing_and_tool_calling_hold_foreground_lease(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "processing");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "dialogue_pending");
    req.has_foreground_active = true;
    req.foreground_active = true;
    req.suppress_ui = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(control_ingress_has_foreground_ai_lease());
    assert(!control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "processing") == 0);

    control_ingress_clear_active_ai_tasks();
    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "tool_calling");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "task");
    req.has_foreground_active = true;
    req.foreground_active = true;
    req.suppress_ui = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(control_ingress_has_foreground_ai_lease());
    assert(control_ingress_has_active_ai_task());
    assert(strcmp(control_ingress_tts_completion_state(), "custom3") == 0);

    memset(&req, 0, sizeof(req));
    snprintf(req.status, sizeof(req.status), "%s", "happy");
    snprintf(req.state_domain, sizeof(req.state_domain), "%s", "task");
    req.has_active_task_count = true;
    req.active_task_count = 0;
    req.has_foreground_active = true;
    req.foreground_active = false;
    req.suppress_ui = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(!control_ingress_has_foreground_ai_lease());
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
}

static void test_foreground_lease_combines_server_foreground_and_active_task_count(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "happy");
    req.has_active_task_count = true;
    req.active_task_count = 1;
    req.has_foreground_active = true;
    req.foreground_active = false;
    req.suppress_ui = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(control_ingress_has_foreground_ai_lease());

    req.active_task_count = 0;
    req.foreground_active = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(control_ingress_has_foreground_ai_lease());

    req.foreground_active = false;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(!control_ingress_has_foreground_ai_lease());
}

static void test_message_fallback_preserves_processing_and_terminal_lifecycle(void) {
    control_ai_status_request_t req = {0};

    control_ingress_reset_ai_task_state_for_test();
    snprintf(req.status, sizeof(req.status), "%s", "brain_update");
    snprintf(req.message, sizeof(req.message), "%s", "processing request");
    req.suppress_ui = true;
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(control_ingress_has_foreground_ai_lease());
    assert(strcmp(control_ingress_tts_completion_state(), "processing") == 0);

    snprintf(req.message, sizeof(req.message), "%s", "completed successfully");
    assert(control_ingress_submit_ai_status(&req) == ESP_OK);
    assert(!control_ingress_has_foreground_ai_lease());
    assert(strcmp(control_ingress_tts_completion_state(), "happy") == 0);
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
        {"ws_single_axis_request_uses_direct_target", test_ws_single_axis_request_uses_direct_target},
        {"jog_request_interrupts_and_preserves_source", test_jog_request_interrupts_and_preserves_source},
        {"stop_interrupts_action_loop_before_servo_stop", test_stop_interrupts_action_loop_before_servo_stop},
        {"jog_vector_request_interrupts_and_preserves_both_axes",
         test_jog_vector_request_interrupts_and_preserves_both_axes},
        {"jog_vector_stream_interrupts_only_on_stream_start", test_jog_vector_stream_interrupts_only_on_stream_start},
        {"state_task_has_crash_safe_stack_budget", test_state_task_has_crash_safe_stack_budget},
        {"state_queue_reports_timeout_when_full", test_state_queue_reports_timeout_when_full},
        {"resource_names_accept_design_export_aliases", test_resource_names_accept_design_export_aliases},
        {"ai_status_voice_flow_states_clear_text", test_ai_status_voice_flow_states_clear_text},
        {"ai_status_empty_message_clears_text", test_ai_status_empty_message_clears_text},
        {"all_ai_statuses_clear_text", test_all_ai_statuses_clear_text},
        {"tts_completion_defaults_to_happy_without_task_status",
         test_tts_completion_defaults_to_happy_without_task_status},
        {"tts_completion_keeps_task_working_after_first_reply",
         test_tts_completion_keeps_task_working_after_first_reply},
        {"tts_completion_uses_terminal_task_status", test_tts_completion_uses_terminal_task_status},
        {"deferred_ai_status_updates_completion_without_replacing_speaking",
         test_deferred_ai_status_updates_completion_without_replacing_speaking},
        {"suppressed_ai_status_updates_lifecycle_without_enqueuing_ui",
         test_suppressed_ai_status_updates_lifecycle_without_enqueuing_ui},
        {"authoritative_task_count_keeps_foreground_execution_active",
         test_authoritative_task_count_keeps_foreground_execution_active},
        {"dialogue_terminal_does_not_release_active_task", test_dialogue_terminal_does_not_release_active_task},
        {"transport_disconnect_clears_active_task_lifecycle", test_transport_disconnect_clears_active_task_lifecycle},
        {"processing_and_tool_calling_use_distinct_existing_states",
         test_processing_and_tool_calling_use_distinct_existing_states},
        {"ai_status_empty_sound_uses_default_and_nonempty_sound_overrides",
         test_ai_status_empty_sound_uses_default_and_nonempty_sound_overrides},
        {"explicit_empty_action_file_does_not_infer_status_action",
         test_explicit_empty_action_file_does_not_infer_status_action},
        {"processing_and_tool_calling_hold_foreground_lease", test_processing_and_tool_calling_hold_foreground_lease},
        {"foreground_lease_combines_server_foreground_and_active_task_count",
         test_foreground_lease_combines_server_foreground_and_active_task_count},
        {"message_fallback_preserves_processing_and_terminal_lifecycle",
         test_message_fallback_preserves_processing_and_terminal_lifecycle},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    control_ingress_reset_ops_for_test();
    return 0;
}
