#ifndef AGENT_RUNTIME_H
#define AGENT_RUNTIME_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AGENT_RUNTIME_STAGE_STOPPED = 0,
    AGENT_RUNTIME_STAGE_PENDING,
    AGENT_RUNTIME_STAGE_AUDIO_STARTING,
    AGENT_RUNTIME_STAGE_REALTIME_CONNECTING,
    AGENT_RUNTIME_STAGE_READY,
    AGENT_RUNTIME_STAGE_SLEEPING,
    AGENT_RUNTIME_STAGE_WAKING,
    AGENT_RUNTIME_STAGE_LISTENING,
    AGENT_RUNTIME_STAGE_THINKING,
    AGENT_RUNTIME_STAGE_SPEAKING,
    AGENT_RUNTIME_STAGE_FAILED,
    AGENT_RUNTIME_STAGE_DEGRADED,
} agent_runtime_stage_t;

typedef enum {
    AGENT_RUNTIME_ERROR_NONE = 0,
    AGENT_RUNTIME_ERROR_WIFI_NOT_READY,
    AGENT_RUNTIME_ERROR_LOW_MEMORY,
    AGENT_RUNTIME_ERROR_AUDIO_PLAYER_START_FAILED,
    AGENT_RUNTIME_ERROR_MIC_START_FAILED,
    AGENT_RUNTIME_ERROR_REALTIME_START_FAILED,
    AGENT_RUNTIME_ERROR_REALTIME_TIMEOUT,
    AGENT_RUNTIME_ERROR_REALTIME_CLOSED,
    AGENT_RUNTIME_ERROR_PROTOCOL,
    AGENT_RUNTIME_ERROR_PIPELINE_BUSY,
    AGENT_RUNTIME_ERROR_AUDIO_QUEUE_FULL,
    AGENT_RUNTIME_ERROR_ANIMATION_TRANSITION_FAILED,
} agent_runtime_error_t;

typedef struct {
    uint32_t start_defer_ms;
    uint32_t connect_timeout_ms;
    uint32_t no_speech_timeout_ms;
} agent_runtime_config_t;

typedef struct {
    bool (*is_wifi_ready)(void *user_ctx);
    bool (*has_runtime_headroom)(void *user_ctx);
    bool (*is_recording)(void *user_ctx);
    void (*configure_transport)(void *user_ctx);
    void (*reset_transport)(void *user_ctx);
    void (*prepare_transport_for_close)(void *user_ctx);
    void (*set_behavior_feedback_enabled)(bool enabled, void *user_ctx);
    void (*recorder_init)(void *user_ctx);
    int (*recorder_start)(void *user_ctx);
    void (*recorder_close)(void *user_ctx);
    esp_err_t (*recorder_request_open)(void *user_ctx);
    esp_err_t (*recorder_request_close)(void *user_ctx);
    void (*recorder_pause_wake_word)(void *user_ctx);
    void (*recorder_resume_wake_word_for_sleep)(void *user_ctx);
    void (*recorder_suspend_cloud_audio)(void *user_ctx);
    esp_err_t (*audio_player_start)(void *user_ctx);
    void (*audio_player_stop)(void *user_ctx);
    void (*audio_player_abort)(void *user_ctx);
    esp_err_t (*audio_player_enqueue)(const uint8_t *pcm, size_t len, void *user_ctx);
    void (*audio_player_mark_stream_done)(void *user_ctx);
    esp_err_t (*realtime_start)(void *user_ctx);
    void (*realtime_stop)(const char *reason, void *user_ctx);
    void (*realtime_tick)(void *user_ctx);
    bool (*realtime_is_ready)(void *user_ctx);
    esp_err_t (*realtime_cancel_response)(const char *reason, void *user_ctx);
    void *user_ctx;
} agent_runtime_ops_t;

typedef struct {
    void (*on_stage_changed)(agent_runtime_stage_t stage, agent_runtime_error_t error, const char *text,
                             void *user_ctx);
    void *user_ctx;
} agent_runtime_callbacks_t;

void agent_runtime_init(const agent_runtime_config_t *config, const agent_runtime_ops_t *ops,
                        const agent_runtime_callbacks_t *callbacks);
void agent_runtime_open(uint64_t now_ms, const char *reason);
void agent_runtime_tick(uint64_t now_ms);
void agent_runtime_close(const char *reason);
void agent_runtime_retry(uint64_t now_ms, const char *reason);
void agent_runtime_handle_button(uint64_t now_ms);
void agent_runtime_mark_sleeping(void);
void agent_runtime_complete_wake(uint64_t now_ms);
void agent_runtime_fail_wake(const char *reason);

void agent_runtime_on_realtime_ready(void);
void agent_runtime_on_realtime_transcript(const char *text, bool is_final);
void agent_runtime_on_realtime_assistant_text(const char *text);
void agent_runtime_on_realtime_audio(const uint8_t *pcm, size_t len);
void agent_runtime_on_realtime_audio_done(void);
void agent_runtime_on_realtime_response_done(void);
void agent_runtime_on_audio_playback_started(void);
void agent_runtime_on_realtime_speech_started(void);
void agent_runtime_on_realtime_speech_stopped(void);
void agent_runtime_on_realtime_error(const char *message);
void agent_runtime_on_realtime_closed(void);
void agent_runtime_on_audio_playback_done(void);

agent_runtime_stage_t agent_runtime_get_stage(void);
agent_runtime_error_t agent_runtime_get_error(void);
bool agent_runtime_is_ready(void);

const char *agent_runtime_stage_name(agent_runtime_stage_t stage);
const char *agent_runtime_error_name(agent_runtime_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* AGENT_RUNTIME_H */
