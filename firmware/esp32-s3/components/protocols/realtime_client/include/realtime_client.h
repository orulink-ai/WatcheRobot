#ifndef REALTIME_CLIENT_H
#define REALTIME_CLIENT_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REALTIME_CLIENT_URL_MAX 160
#define REALTIME_CLIENT_API_KEY_MAX 96
#define REALTIME_CLIENT_VOICE_MAX 32
#define REALTIME_CLIENT_INSTRUCTIONS_MAX 256

typedef enum {
    REALTIME_CLIENT_STATE_IDLE = 0,
    REALTIME_CLIENT_STATE_CONNECTING,
    REALTIME_CLIENT_STATE_READY,
    REALTIME_CLIENT_STATE_ERROR,
} realtime_client_state_t;

typedef struct {
    const char *url;
    const char *api_key;
    const char *voice;
    const char *instructions;
    uint32_t connect_timeout_ms;
    uint32_t response_timeout_ms;
} realtime_client_config_t;

typedef struct {
    void (*on_ready)(void *user_ctx);
    void (*on_transcript)(const char *text, bool is_final, void *user_ctx);
    void (*on_assistant_text)(const char *text, void *user_ctx);
    void (*on_audio)(const uint8_t *pcm, size_t len, void *user_ctx);
    void (*on_audio_done)(void *user_ctx);
    void (*on_response_done)(void *user_ctx);
    void (*on_speech_started)(void *user_ctx);
    void (*on_speech_stopped)(void *user_ctx);
    void (*on_error)(const char *message, void *user_ctx);
    void (*on_closed)(void *user_ctx);
    void *user_ctx;
} realtime_client_callbacks_t;

esp_err_t realtime_client_start(const realtime_client_config_t *config, const realtime_client_callbacks_t *callbacks);
void realtime_client_stop(const char *reason);
void realtime_client_tick(void);
bool realtime_client_is_ready(void);
realtime_client_state_t realtime_client_get_state(void);
esp_err_t realtime_client_send_audio_pcm(const uint8_t *data, size_t len);
esp_err_t realtime_client_finish_turn(void);
esp_err_t realtime_client_cancel_response(const char *reason);

esp_err_t realtime_client_build_session_update_for_test(const realtime_client_config_t *config, char *out,
                                                        size_t out_size);
esp_err_t realtime_client_build_audio_append_for_test(const uint8_t *pcm, size_t len, char *out, size_t out_size);
esp_err_t realtime_client_parse_event_type_for_test(const char *json, char *out, size_t out_size);
esp_err_t realtime_client_decode_audio_delta_for_test(const char *json, uint8_t *out, size_t out_size,
                                                      size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* REALTIME_CLIENT_H */
