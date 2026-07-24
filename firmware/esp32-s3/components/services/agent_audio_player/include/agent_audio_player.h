#ifndef AGENT_AUDIO_PLAYER_H
#define AGENT_AUDIO_PLAYER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*agent_audio_player_done_cb_t)(void *user_ctx);

esp_err_t agent_audio_player_start(agent_audio_player_done_cb_t done_cb, void *user_ctx);
void agent_audio_player_stop(void);
esp_err_t agent_audio_player_enqueue(const uint8_t *pcm, size_t len);
void agent_audio_player_mark_stream_done(void);
void agent_audio_player_abort(void);
bool agent_audio_player_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* AGENT_AUDIO_PLAYER_H */
