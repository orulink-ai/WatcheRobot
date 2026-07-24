#include "agent_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool wifi_ready;
    bool headroom;
    bool realtime_ready;
    bool recording;
    bool behavior_feedback_enabled;
    int recorder_start_count;
    int recorder_open_count;
    int recorder_close_count;
    int recorder_suspend_count;
    int realtime_start_count;
    int realtime_stop_count;
    int realtime_cancel_count;
    int audio_start_count;
    int audio_stop_count;
    int audio_abort_count;
    int audio_enqueue_count;
    agent_runtime_stage_t last_stage;
    agent_runtime_error_t last_error;
    char last_text[128];
} fake_env_t;

static fake_env_t g_env;

static void expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static bool fake_wifi_ready(void *ctx) {
    return ((fake_env_t *)ctx)->wifi_ready;
}

static bool fake_headroom(void *ctx) {
    return ((fake_env_t *)ctx)->headroom;
}

static bool fake_is_recording(void *ctx) {
    return ((fake_env_t *)ctx)->recording;
}

static void fake_behavior_feedback(bool enabled, void *ctx) {
    ((fake_env_t *)ctx)->behavior_feedback_enabled = enabled;
}

static int fake_recorder_start(void *ctx) {
    ((fake_env_t *)ctx)->recorder_start_count++;
    return 0;
}

static void fake_recorder_close(void *ctx) {
    ((fake_env_t *)ctx)->recorder_close_count++;
}

static void fake_recorder_suspend(void *ctx) {
    fake_env_t *env = (fake_env_t *)ctx;
    if (env->recording) {
        env->recorder_suspend_count++;
    }
    env->recording = false;
}

static esp_err_t fake_recorder_open(void *ctx) {
    ((fake_env_t *)ctx)->recorder_open_count++;
    ((fake_env_t *)ctx)->recording = true;
    return ESP_OK;
}

static esp_err_t fake_recorder_close_request(void *ctx) {
    ((fake_env_t *)ctx)->recording = false;
    return ESP_OK;
}

static esp_err_t fake_audio_start(void *ctx) {
    ((fake_env_t *)ctx)->audio_start_count++;
    return ESP_OK;
}

static void fake_audio_stop(void *ctx) {
    ((fake_env_t *)ctx)->audio_stop_count++;
}

static void fake_audio_abort(void *ctx) {
    ((fake_env_t *)ctx)->audio_abort_count++;
}

static esp_err_t fake_audio_enqueue(const uint8_t *pcm, size_t len, void *ctx) {
    (void)pcm;
    (void)len;
    ((fake_env_t *)ctx)->audio_enqueue_count++;
    return ESP_OK;
}

static esp_err_t fake_realtime_start(void *ctx) {
    ((fake_env_t *)ctx)->realtime_start_count++;
    return ESP_OK;
}

static void fake_realtime_stop(const char *reason, void *ctx) {
    (void)reason;
    ((fake_env_t *)ctx)->realtime_stop_count++;
}

static bool fake_realtime_ready(void *ctx) {
    return ((fake_env_t *)ctx)->realtime_ready;
}

static esp_err_t fake_realtime_cancel(const char *reason, void *ctx) {
    (void)reason;
    ((fake_env_t *)ctx)->realtime_cancel_count++;
    return ESP_OK;
}

static void fake_stage(agent_runtime_stage_t stage, agent_runtime_error_t error, const char *text, void *ctx) {
    fake_env_t *env = (fake_env_t *)ctx;
    env->last_stage = stage;
    env->last_error = error;
    snprintf(env->last_text, sizeof(env->last_text), "%s", text != NULL ? text : "");
}

static void init_runtime(void) {
    const agent_runtime_config_t config = {
        .start_defer_ms = 100,
        .connect_timeout_ms = 1000,
        .no_speech_timeout_ms = 1000,
    };
    const agent_runtime_ops_t ops = {
        .is_wifi_ready = fake_wifi_ready,
        .has_runtime_headroom = fake_headroom,
        .is_recording = fake_is_recording,
        .set_behavior_feedback_enabled = fake_behavior_feedback,
        .recorder_start = fake_recorder_start,
        .recorder_close = fake_recorder_close,
        .recorder_request_open = fake_recorder_open,
        .recorder_request_close = fake_recorder_close_request,
        .recorder_suspend_cloud_audio = fake_recorder_suspend,
        .audio_player_start = fake_audio_start,
        .audio_player_stop = fake_audio_stop,
        .audio_player_abort = fake_audio_abort,
        .audio_player_enqueue = fake_audio_enqueue,
        .realtime_start = fake_realtime_start,
        .realtime_stop = fake_realtime_stop,
        .realtime_is_ready = fake_realtime_ready,
        .realtime_cancel_response = fake_realtime_cancel,
        .user_ctx = &g_env,
    };
    const agent_runtime_callbacks_t callbacks = {
        .on_stage_changed = fake_stage,
        .user_ctx = &g_env,
    };

    memset(&g_env, 0, sizeof(g_env));
    g_env.wifi_ready = true;
    g_env.headroom = true;
    g_env.behavior_feedback_enabled = true;
    agent_runtime_init(&config, &ops, &callbacks);
}

static void test_open_deferred_then_ready(void) {
    init_runtime();
    agent_runtime_open(1000, "test");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_PENDING, "open enters pending");
    agent_runtime_tick(1050);
    expect_true(g_env.realtime_start_count == 0, "start waits for defer");
    agent_runtime_tick(1101);
    expect_true(g_env.recorder_start_count == 1, "recorder started");
    expect_true(g_env.realtime_start_count == 1, "realtime started");
    expect_true(!g_env.behavior_feedback_enabled, "behavior feedback disabled while agent owns recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING, "connecting after start");
    g_env.realtime_ready = true;
    agent_runtime_tick(1110);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_READY, "ready after realtime ready");
    agent_runtime_close("test close");
    expect_true(g_env.behavior_feedback_enabled, "behavior feedback restored after close");
}

static void test_low_memory_fails_before_start(void) {
    init_runtime();
    g_env.headroom = false;
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_FAILED, "low memory enters failed");
    expect_true(g_env.last_error == AGENT_RUNTIME_ERROR_LOW_MEMORY, "low memory error set");
    expect_true(g_env.realtime_start_count == 0, "realtime not started");
}

static void test_timeout_releases_resources(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    agent_runtime_tick(1101);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_FAILED, "timeout enters failed");
    expect_true(g_env.last_error == AGENT_RUNTIME_ERROR_REALTIME_TIMEOUT, "timeout error set");
    expect_true(g_env.realtime_stop_count >= 1, "timeout stops realtime");
    expect_true(g_env.recorder_close_count >= 1, "timeout closes recorder");
    expect_true(g_env.realtime_cancel_count == 0, "timeout does not cancel response");
}

static void test_button_toggle_and_barge_in(void) {
    uint8_t pcm[4] = {1, 2, 3, 4};
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_handle_button(103);
    expect_true(!g_env.recording, "button keeps recorder closed while sleep-out runs");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_WAKING, "button waits for sleep-out completion");
    agent_runtime_tick(263);
    expect_true(!g_env.recording, "time cannot bypass sleep-out completion");
    agent_runtime_complete_wake(264);
    expect_true(g_env.recording, "sleep-out completion opens recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_LISTENING, "sleep-out completion enters listening");
    agent_runtime_handle_button(265);
    expect_true(!g_env.recording, "button closes recording");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_THINKING, "close enters thinking");
    agent_runtime_on_realtime_audio(pcm, sizeof(pcm));
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_SPEAKING, "audio enters speaking");
    agent_runtime_handle_button(105);
    expect_true(g_env.audio_abort_count == 1, "barge-in aborts audio");
    expect_true(g_env.realtime_cancel_count >= 1, "barge-in cancels response");
}

static void test_final_transcript_closes_recorder_and_ignores_late_delta(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_handle_button(103);
    agent_runtime_complete_wake(263);
    expect_true(g_env.recording, "completed wake opens recorder before final transcript");
    agent_runtime_on_realtime_transcript("hello", true);
    expect_true(!g_env.recording, "final transcript closes recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_THINKING, "final transcript enters thinking");
    agent_runtime_on_realtime_transcript("late partial", false);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_THINKING, "late partial does not return to listening");
}

static void test_response_audio_closes_stale_recorder_before_playback(void) {
    uint8_t pcm[4] = {1, 2, 3, 4};
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_handle_button(103);
    agent_runtime_complete_wake(263);
    expect_true(g_env.recording, "completed wake opens recorder before response audio");
    agent_runtime_on_realtime_audio(pcm, sizeof(pcm));
    expect_true(!g_env.recording, "response audio closes stale recorder");
    expect_true(g_env.recorder_suspend_count == 1, "response audio synchronously suspends recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_SPEAKING, "response audio enters speaking");
}

static void test_server_vad_stopped_closes_recorder(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_handle_button(103);
    agent_runtime_complete_wake(263);
    expect_true(g_env.recording, "completed wake opens recorder before server vad stop");
    agent_runtime_on_realtime_speech_stopped();
    expect_true(!g_env.recording, "server vad stop closes recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_THINKING, "server vad stop enters thinking");
}

static void test_no_speech_timeout_returns_ready(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_handle_button(103);
    agent_runtime_complete_wake(263);
    expect_true(g_env.recording, "completed wake opens recorder before no speech timeout");
    agent_runtime_tick(1262);
    expect_true(g_env.recording, "no speech timeout waits full budget");
    agent_runtime_tick(1264);
    expect_true(!g_env.recording, "no speech timeout closes recorder");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_READY, "no speech timeout returns ready");
}

static void test_wake_animation_failure_is_fatal_and_never_opens_recorder(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);

    agent_runtime_handle_button(103);
    agent_runtime_on_realtime_ready();
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_WAKING, "duplicate realtime ready cannot bypass sleep-out");
    agent_runtime_tick(10000);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_WAKING, "elapsed time cannot complete wake");
    expect_true(g_env.recorder_open_count == 0, "recorder remains closed before animation terminal event");

    agent_runtime_fail_wake("sleep-out render failed");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_FAILED, "sleep-out failure enters failed stage");
    expect_true(g_env.last_error == AGENT_RUNTIME_ERROR_ANIMATION_TRANSITION_FAILED,
                "sleep-out failure has explicit error");
    expect_true(g_env.recorder_open_count == 0, "sleep-out failure never opens recorder");

    agent_runtime_complete_wake(10001);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_FAILED, "late completion cannot escape fatal failure");
    expect_true(g_env.recorder_open_count == 0, "late completion still cannot open recorder");
}

static void test_sleep_gate_blocks_late_interaction_events_until_wake_completes(void) {
    uint8_t pcm[4] = {1, 2, 3, 4};
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    g_env.realtime_ready = true;
    agent_runtime_tick(102);
    agent_runtime_mark_sleeping();
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_SLEEPING, "ready flow enters explicit sleeping gate");

    agent_runtime_on_realtime_transcript("late", false);
    agent_runtime_on_realtime_transcript("late", true);
    agent_runtime_on_realtime_assistant_text("late");
    agent_runtime_on_realtime_audio(pcm, sizeof(pcm));
    agent_runtime_on_realtime_speech_started();
    agent_runtime_on_realtime_speech_stopped();
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_SLEEPING, "late realtime events cannot bypass sleep-out");
    expect_true(g_env.recorder_open_count == 0, "late realtime events cannot open recorder");
    expect_true(g_env.audio_enqueue_count == 0, "late response audio is not rendered during sleep transition");

    agent_runtime_handle_button(103);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_WAKING, "sleeping button begins explicit wake gate");
    agent_runtime_complete_wake(104);
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_LISTENING, "completed sleep-out releases interaction gate");
    expect_true(g_env.recorder_open_count == 1, "recorder opens once after completed sleep-out");
}

static void test_pipeline_busy_classified(void) {
    init_runtime();
    agent_runtime_open(0, "test");
    agent_runtime_tick(101);
    agent_runtime_on_realtime_error("all pipeline slots in use");
    expect_true(g_env.last_stage == AGENT_RUNTIME_STAGE_FAILED, "pipeline busy enters failed");
    expect_true(g_env.last_error == AGENT_RUNTIME_ERROR_PIPELINE_BUSY, "pipeline busy classified");
    agent_runtime_tick(102);
    expect_true(g_env.realtime_stop_count >= 1, "pipeline busy releases realtime on tick");
    expect_true(g_env.realtime_cancel_count == 0, "pipeline busy cleanup does not cancel response");
}

int main(void) {
    test_open_deferred_then_ready();
    test_low_memory_fails_before_start();
    test_timeout_releases_resources();
    test_button_toggle_and_barge_in();
    test_final_transcript_closes_recorder_and_ignores_late_delta();
    test_response_audio_closes_stale_recorder_before_playback();
    test_server_vad_stopped_closes_recorder();
    test_no_speech_timeout_returns_ready();
    test_wake_animation_failure_is_fatal_and_never_opens_recorder();
    test_sleep_gate_blocks_late_interaction_events_until_wake_completes();
    test_pipeline_busy_classified();
    printf("agent_runtime host tests passed\n");
    return 0;
}
