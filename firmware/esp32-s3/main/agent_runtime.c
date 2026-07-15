#include "agent_runtime.h"

#include <string.h>

#define AGENT_RUNTIME_DEFAULT_START_DEFER_MS 250U
#define AGENT_RUNTIME_DEFAULT_CONNECT_TIMEOUT_MS 10000U
#define AGENT_RUNTIME_DEFAULT_NO_SPEECH_TIMEOUT_MS 12000U

typedef struct {
    agent_runtime_config_t config;
    agent_runtime_ops_t ops;
    agent_runtime_callbacks_t callbacks;
    agent_runtime_stage_t stage;
    agent_runtime_error_t error;
    uint64_t open_ms;
    bool resources_started;
    bool shutdown_pending;
    uint64_t listening_started_ms;
    bool listening_speech_seen;
    bool audio_playback_pending;
} agent_runtime_ctx_t;

static agent_runtime_ctx_t s_agent_runtime;

static bool has_op_bool(bool (*fn)(void *user_ctx), bool fallback) {
    return fn != NULL ? fn(s_agent_runtime.ops.user_ctx) : fallback;
}

static void notify_stage(agent_runtime_stage_t stage, agent_runtime_error_t error, const char *text) {
    s_agent_runtime.stage = stage;
    s_agent_runtime.error = error;
    if (s_agent_runtime.callbacks.on_stage_changed != NULL) {
        s_agent_runtime.callbacks.on_stage_changed(stage, error, text, s_agent_runtime.callbacks.user_ctx);
    }
}

static void enter_failed(agent_runtime_error_t error, const char *text) {
    notify_stage(AGENT_RUNTIME_STAGE_FAILED, error, text);
}

static void enter_listening(uint64_t now_ms) {
    s_agent_runtime.listening_started_ms = now_ms;
    s_agent_runtime.listening_speech_seen = false;
    s_agent_runtime.audio_playback_pending = false;
    notify_stage(AGENT_RUNTIME_STAGE_LISTENING, AGENT_RUNTIME_ERROR_NONE, "");
}

static bool recorder_is_recording(void) {
    return has_op_bool(s_agent_runtime.ops.is_recording, false);
}

static void request_recorder_close_if_recording(const char *reason) {
    if (!recorder_is_recording() || s_agent_runtime.ops.recorder_request_close == NULL) {
        return;
    }

    (void)reason;
    (void)s_agent_runtime.ops.recorder_request_close(s_agent_runtime.ops.user_ctx);
}

static void process_no_speech_timeout(uint64_t now_ms) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_LISTENING || s_agent_runtime.listening_speech_seen ||
        s_agent_runtime.config.no_speech_timeout_ms == 0U || s_agent_runtime.listening_started_ms == 0U) {
        return;
    }
    if (now_ms - s_agent_runtime.listening_started_ms < (uint64_t)s_agent_runtime.config.no_speech_timeout_ms) {
        return;
    }

    request_recorder_close_if_recording("agent no speech timeout");
    if (s_agent_runtime.ops.recorder_resume_wake_word_for_sleep != NULL) {
        s_agent_runtime.ops.recorder_resume_wake_word_for_sleep(s_agent_runtime.ops.user_ctx);
    }
    notify_stage(AGENT_RUNTIME_STAGE_READY, AGENT_RUNTIME_ERROR_NONE, "Agent ready");
}

static void force_recorder_idle_if_recording(const char *reason) {
    if (!recorder_is_recording()) {
        return;
    }
    if (s_agent_runtime.ops.recorder_suspend_cloud_audio != NULL) {
        (void)reason;
        s_agent_runtime.ops.recorder_suspend_cloud_audio(s_agent_runtime.ops.user_ctx);
        return;
    }
    request_recorder_close_if_recording(reason);
}

static void stop_resources(const char *reason, bool cancel_response) {
    bool should_release_runtime = s_agent_runtime.resources_started;

    if (cancel_response && s_agent_runtime.ops.realtime_cancel_response != NULL) {
        (void)s_agent_runtime.ops.realtime_cancel_response(reason != NULL ? reason : "agent runtime stop",
                                                           s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.realtime_stop != NULL) {
        s_agent_runtime.ops.realtime_stop(reason != NULL ? reason : "agent runtime stop", s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.prepare_transport_for_close != NULL) {
        s_agent_runtime.ops.prepare_transport_for_close(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.audio_player_stop != NULL) {
        s_agent_runtime.ops.audio_player_stop(s_agent_runtime.ops.user_ctx);
    }
    if (should_release_runtime && s_agent_runtime.ops.recorder_close != NULL) {
        s_agent_runtime.ops.recorder_close(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.reset_transport != NULL) {
        s_agent_runtime.ops.reset_transport(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.set_behavior_feedback_enabled != NULL) {
        s_agent_runtime.ops.set_behavior_feedback_enabled(true, s_agent_runtime.ops.user_ctx);
    }
    s_agent_runtime.resources_started = false;
    s_agent_runtime.audio_playback_pending = false;
}

static bool message_mentions_pipeline_busy(const char *message) {
    return message != NULL &&
           (strstr(message, "pipeline") != NULL || strstr(message, "slot") != NULL || strstr(message, "busy") != NULL);
}

static void start_if_due(uint64_t now_ms) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_PENDING) {
        return;
    }
    if (s_agent_runtime.open_ms > 0 &&
        now_ms - s_agent_runtime.open_ms < (uint64_t)s_agent_runtime.config.start_defer_ms) {
        return;
    }
    if (!has_op_bool(s_agent_runtime.ops.is_wifi_ready, true)) {
        enter_failed(AGENT_RUNTIME_ERROR_WIFI_NOT_READY, "Agent Wi-Fi unavailable");
        return;
    }
    if (!has_op_bool(s_agent_runtime.ops.has_runtime_headroom, true)) {
        enter_failed(AGENT_RUNTIME_ERROR_LOW_MEMORY, "Agent low memory");
        return;
    }

    notify_stage(AGENT_RUNTIME_STAGE_AUDIO_STARTING, AGENT_RUNTIME_ERROR_NONE, "Starting Agent");
    if (s_agent_runtime.ops.set_behavior_feedback_enabled != NULL) {
        s_agent_runtime.ops.set_behavior_feedback_enabled(false, s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.recorder_suspend_cloud_audio != NULL) {
        s_agent_runtime.ops.recorder_suspend_cloud_audio(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.reset_transport != NULL) {
        s_agent_runtime.ops.reset_transport(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.configure_transport != NULL) {
        s_agent_runtime.ops.configure_transport(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.recorder_init != NULL) {
        s_agent_runtime.ops.recorder_init(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.ops.audio_player_start != NULL &&
        s_agent_runtime.ops.audio_player_start(s_agent_runtime.ops.user_ctx) != ESP_OK) {
        stop_resources("agent audio player start failed", false);
        enter_failed(AGENT_RUNTIME_ERROR_AUDIO_PLAYER_START_FAILED, "Agent audio failed");
        return;
    }
    if (s_agent_runtime.ops.recorder_start != NULL &&
        s_agent_runtime.ops.recorder_start(s_agent_runtime.ops.user_ctx) != 0) {
        stop_resources("agent recorder start failed", false);
        enter_failed(AGENT_RUNTIME_ERROR_MIC_START_FAILED, "Agent mic failed");
        return;
    }
    s_agent_runtime.resources_started = true;
    notify_stage(AGENT_RUNTIME_STAGE_REALTIME_CONNECTING, AGENT_RUNTIME_ERROR_NONE, "Connecting Agent");
    if (s_agent_runtime.ops.realtime_start != NULL &&
        s_agent_runtime.ops.realtime_start(s_agent_runtime.ops.user_ctx) != ESP_OK) {
        stop_resources("agent realtime start failed", false);
        enter_failed(AGENT_RUNTIME_ERROR_REALTIME_START_FAILED, "Agent connect failed");
    }
}

void agent_runtime_init(const agent_runtime_config_t *config, const agent_runtime_ops_t *ops,
                        const agent_runtime_callbacks_t *callbacks) {
    memset(&s_agent_runtime, 0, sizeof(s_agent_runtime));
    s_agent_runtime.stage = AGENT_RUNTIME_STAGE_STOPPED;
    s_agent_runtime.config.start_defer_ms = AGENT_RUNTIME_DEFAULT_START_DEFER_MS;
    s_agent_runtime.config.connect_timeout_ms = AGENT_RUNTIME_DEFAULT_CONNECT_TIMEOUT_MS;
    s_agent_runtime.config.no_speech_timeout_ms = AGENT_RUNTIME_DEFAULT_NO_SPEECH_TIMEOUT_MS;
    if (config != NULL) {
        if (config->start_defer_ms > 0) {
            s_agent_runtime.config.start_defer_ms = config->start_defer_ms;
        }
        if (config->connect_timeout_ms > 0) {
            s_agent_runtime.config.connect_timeout_ms = config->connect_timeout_ms;
        }
        if (config->no_speech_timeout_ms > 0) {
            s_agent_runtime.config.no_speech_timeout_ms = config->no_speech_timeout_ms;
        }
    }
    if (ops != NULL) {
        s_agent_runtime.ops = *ops;
    }
    if (callbacks != NULL) {
        s_agent_runtime.callbacks = *callbacks;
    }
}

void agent_runtime_open(uint64_t now_ms, const char *reason) {
    (void)reason;
    stop_resources("agent runtime reopen", false);
    s_agent_runtime.open_ms = now_ms;
    s_agent_runtime.shutdown_pending = false;
    s_agent_runtime.listening_started_ms = 0;
    s_agent_runtime.listening_speech_seen = false;
    s_agent_runtime.audio_playback_pending = false;
    notify_stage(AGENT_RUNTIME_STAGE_PENDING, AGENT_RUNTIME_ERROR_NONE, "Connecting Agent");
}

void agent_runtime_tick(uint64_t now_ms) {
    if (s_agent_runtime.shutdown_pending) {
        s_agent_runtime.shutdown_pending = false;
        stop_resources("agent runtime async failure release", false);
    }
    start_if_due(now_ms);
    process_no_speech_timeout(now_ms);
    if (s_agent_runtime.ops.realtime_tick != NULL &&
        (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING ||
         s_agent_runtime.stage == AGENT_RUNTIME_STAGE_READY || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_LISTENING ||
         s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING ||
         s_agent_runtime.stage == AGENT_RUNTIME_STAGE_THINKING ||
         s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SPEAKING)) {
        s_agent_runtime.ops.realtime_tick(s_agent_runtime.ops.user_ctx);
    }
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING &&
        has_op_bool(s_agent_runtime.ops.realtime_is_ready, false)) {
        notify_stage(AGENT_RUNTIME_STAGE_READY, AGENT_RUNTIME_ERROR_NONE, "Agent ready");
    }
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING &&
        now_ms - s_agent_runtime.open_ms > (uint64_t)s_agent_runtime.config.connect_timeout_ms) {
        stop_resources("agent realtime connect timeout", false);
        enter_failed(AGENT_RUNTIME_ERROR_REALTIME_TIMEOUT, "Agent timeout");
    }
}

void agent_runtime_close(const char *reason) {
    s_agent_runtime.shutdown_pending = false;
    s_agent_runtime.listening_started_ms = 0;
    s_agent_runtime.listening_speech_seen = false;
    stop_resources(reason != NULL ? reason : "agent runtime close", false);
    s_agent_runtime.open_ms = 0;
    notify_stage(AGENT_RUNTIME_STAGE_STOPPED, AGENT_RUNTIME_ERROR_NONE, NULL);
}

void agent_runtime_retry(uint64_t now_ms, const char *reason) {
    agent_runtime_open(now_ms, reason != NULL ? reason : "agent retry");
}

void agent_runtime_handle_button(uint64_t now_ms) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_FAILED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_DEGRADED ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_PENDING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING) {
        agent_runtime_retry(now_ms, "agent button retry");
        return;
    }
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SPEAKING) {
        if (s_agent_runtime.ops.audio_player_abort != NULL) {
            s_agent_runtime.ops.audio_player_abort(s_agent_runtime.ops.user_ctx);
        }
        if (s_agent_runtime.ops.realtime_cancel_response != NULL) {
            (void)s_agent_runtime.ops.realtime_cancel_response("agent barge-in", s_agent_runtime.ops.user_ctx);
        }
    }
    if (recorder_is_recording()) {
        request_recorder_close_if_recording("agent button close");
        notify_stage(AGENT_RUNTIME_STAGE_THINKING, AGENT_RUNTIME_ERROR_NONE, "");
    } else {
        notify_stage(AGENT_RUNTIME_STAGE_WAKING, AGENT_RUNTIME_ERROR_NONE, "Waiting for sleep-out animation");
    }
}

void agent_runtime_complete_wake(uint64_t now_ms) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_WAKING) {
        return;
    }
    if (s_agent_runtime.ops.recorder_request_open != NULL &&
        s_agent_runtime.ops.recorder_request_open(s_agent_runtime.ops.user_ctx) != ESP_OK) {
        enter_failed(AGENT_RUNTIME_ERROR_MIC_START_FAILED, "Agent mic failed");
        return;
    }
    enter_listening(now_ms);
}

void agent_runtime_mark_sleeping(void) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_READY) {
        notify_stage(AGENT_RUNTIME_STAGE_SLEEPING, AGENT_RUNTIME_ERROR_NONE, "Agent sleeping");
    }
}

void agent_runtime_fail_wake(const char *reason) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_WAKING) {
        return;
    }
    request_recorder_close_if_recording("agent wake animation failed");
    enter_failed(AGENT_RUNTIME_ERROR_ANIMATION_TRANSITION_FAILED,
                 reason != NULL && reason[0] != '\0' ? reason : "Agent sleep-out animation failed");
}

void agent_runtime_on_realtime_ready(void) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_REALTIME_CONNECTING) {
        notify_stage(AGENT_RUNTIME_STAGE_READY, AGENT_RUNTIME_ERROR_NONE, "Agent ready");
    }
}

void agent_runtime_on_realtime_transcript(const char *text, bool is_final) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING) {
        return;
    }
    if (is_final) {
        request_recorder_close_if_recording("agent final transcript");
        notify_stage(AGENT_RUNTIME_STAGE_THINKING, AGENT_RUNTIME_ERROR_NONE, text != NULL ? text : "");
        return;
    }
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_READY || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_LISTENING) {
        if (text != NULL && text[0] != '\0') {
            s_agent_runtime.listening_speech_seen = true;
        }
        notify_stage(AGENT_RUNTIME_STAGE_LISTENING, AGENT_RUNTIME_ERROR_NONE, text != NULL ? text : "");
    }
}

void agent_runtime_on_realtime_assistant_text(const char *text) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_STOPPED && s_agent_runtime.stage != AGENT_RUNTIME_STAGE_SLEEPING &&
        s_agent_runtime.stage != AGENT_RUNTIME_STAGE_WAKING && text != NULL && text[0] != '\0') {
        request_recorder_close_if_recording("agent assistant text");
        notify_stage(AGENT_RUNTIME_STAGE_THINKING, AGENT_RUNTIME_ERROR_NONE, text);
    }
}

void agent_runtime_on_realtime_audio(const uint8_t *pcm, size_t len) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING || pcm == NULL || len == 0) {
        return;
    }
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_SPEAKING && !s_agent_runtime.audio_playback_pending) {
        force_recorder_idle_if_recording("agent audio response start");
        if (s_agent_runtime.ops.recorder_pause_wake_word != NULL) {
            s_agent_runtime.ops.recorder_pause_wake_word(s_agent_runtime.ops.user_ctx);
        }
        s_agent_runtime.audio_playback_pending = true;
    }
    if (s_agent_runtime.ops.audio_player_enqueue != NULL &&
        s_agent_runtime.ops.audio_player_enqueue(pcm, len, s_agent_runtime.ops.user_ctx) != ESP_OK) {
        s_agent_runtime.shutdown_pending = true;
        s_agent_runtime.audio_playback_pending = false;
        enter_failed(AGENT_RUNTIME_ERROR_AUDIO_QUEUE_FULL, "Agent audio queue full");
    }
}

void agent_runtime_on_realtime_audio_done(void) {
    if (s_agent_runtime.ops.audio_player_mark_stream_done != NULL) {
        s_agent_runtime.ops.audio_player_mark_stream_done(s_agent_runtime.ops.user_ctx);
    }
}

void agent_runtime_on_realtime_response_done(void) {
    agent_runtime_on_realtime_audio_done();
}

void agent_runtime_on_audio_playback_started(void) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING) {
        return;
    }

    s_agent_runtime.audio_playback_pending = false;
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_SPEAKING) {
        notify_stage(AGENT_RUNTIME_STAGE_SPEAKING, AGENT_RUNTIME_ERROR_NONE, "");
    }
}

void agent_runtime_on_realtime_speech_started(void) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SPEAKING) {
        return;
    }
    if (recorder_is_recording()) {
        s_agent_runtime.listening_speech_seen = true;
        notify_stage(AGENT_RUNTIME_STAGE_LISTENING, AGENT_RUNTIME_ERROR_NONE, "");
    }
}

void agent_runtime_on_realtime_speech_stopped(void) {
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_STOPPED || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SLEEPING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_WAKING) {
        return;
    }
    request_recorder_close_if_recording("agent server vad stopped");
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_READY || s_agent_runtime.stage == AGENT_RUNTIME_STAGE_LISTENING) {
        notify_stage(AGENT_RUNTIME_STAGE_THINKING, AGENT_RUNTIME_ERROR_NONE, "");
    }
}

void agent_runtime_on_realtime_error(const char *message) {
    agent_runtime_error_t error =
        message_mentions_pipeline_busy(message) ? AGENT_RUNTIME_ERROR_PIPELINE_BUSY : AGENT_RUNTIME_ERROR_PROTOCOL;
    s_agent_runtime.shutdown_pending = true;
    enter_failed(error, message != NULL && message[0] != '\0' ? message : "Agent protocol error");
}

void agent_runtime_on_realtime_closed(void) {
    if (s_agent_runtime.stage != AGENT_RUNTIME_STAGE_STOPPED) {
        s_agent_runtime.shutdown_pending = true;
        enter_failed(AGENT_RUNTIME_ERROR_REALTIME_CLOSED, "Agent disconnected");
    }
}

void agent_runtime_on_audio_playback_done(void) {
    s_agent_runtime.audio_playback_pending = false;
    if (s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SPEAKING ||
        s_agent_runtime.stage == AGENT_RUNTIME_STAGE_THINKING) {
        if (s_agent_runtime.ops.recorder_resume_wake_word_for_sleep != NULL) {
            s_agent_runtime.ops.recorder_resume_wake_word_for_sleep(s_agent_runtime.ops.user_ctx);
        }
        notify_stage(AGENT_RUNTIME_STAGE_READY, AGENT_RUNTIME_ERROR_NONE, "Agent ready");
    }
}

agent_runtime_stage_t agent_runtime_get_stage(void) {
    return s_agent_runtime.stage;
}

agent_runtime_error_t agent_runtime_get_error(void) {
    return s_agent_runtime.error;
}

bool agent_runtime_is_ready(void) {
    return s_agent_runtime.stage == AGENT_RUNTIME_STAGE_READY ||
           s_agent_runtime.stage == AGENT_RUNTIME_STAGE_LISTENING ||
           s_agent_runtime.stage == AGENT_RUNTIME_STAGE_THINKING ||
           s_agent_runtime.stage == AGENT_RUNTIME_STAGE_SPEAKING;
}

const char *agent_runtime_stage_name(agent_runtime_stage_t stage) {
    switch (stage) {
    case AGENT_RUNTIME_STAGE_STOPPED:
        return "stopped";
    case AGENT_RUNTIME_STAGE_PENDING:
        return "pending";
    case AGENT_RUNTIME_STAGE_AUDIO_STARTING:
        return "audio_starting";
    case AGENT_RUNTIME_STAGE_REALTIME_CONNECTING:
        return "realtime_connecting";
    case AGENT_RUNTIME_STAGE_READY:
        return "ready";
    case AGENT_RUNTIME_STAGE_SLEEPING:
        return "sleeping";
    case AGENT_RUNTIME_STAGE_WAKING:
        return "waking";
    case AGENT_RUNTIME_STAGE_LISTENING:
        return "listening";
    case AGENT_RUNTIME_STAGE_THINKING:
        return "thinking";
    case AGENT_RUNTIME_STAGE_SPEAKING:
        return "speaking";
    case AGENT_RUNTIME_STAGE_FAILED:
        return "failed";
    case AGENT_RUNTIME_STAGE_DEGRADED:
        return "degraded";
    default:
        return "unknown";
    }
}

const char *agent_runtime_error_name(agent_runtime_error_t error) {
    switch (error) {
    case AGENT_RUNTIME_ERROR_NONE:
        return "none";
    case AGENT_RUNTIME_ERROR_WIFI_NOT_READY:
        return "wifi_not_ready";
    case AGENT_RUNTIME_ERROR_LOW_MEMORY:
        return "low_memory";
    case AGENT_RUNTIME_ERROR_AUDIO_PLAYER_START_FAILED:
        return "audio_player_start_failed";
    case AGENT_RUNTIME_ERROR_MIC_START_FAILED:
        return "mic_start_failed";
    case AGENT_RUNTIME_ERROR_REALTIME_START_FAILED:
        return "realtime_start_failed";
    case AGENT_RUNTIME_ERROR_REALTIME_TIMEOUT:
        return "realtime_timeout";
    case AGENT_RUNTIME_ERROR_REALTIME_CLOSED:
        return "realtime_closed";
    case AGENT_RUNTIME_ERROR_PROTOCOL:
        return "protocol";
    case AGENT_RUNTIME_ERROR_PIPELINE_BUSY:
        return "pipeline_busy";
    case AGENT_RUNTIME_ERROR_AUDIO_QUEUE_FULL:
        return "audio_queue_full";
    case AGENT_RUNTIME_ERROR_ANIMATION_TRANSITION_FAILED:
        return "animation_transition_failed";
    default:
        return "unknown";
    }
}
