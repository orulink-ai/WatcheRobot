#ifndef WS_TTS_BUFFER_POLICY_H
#define WS_TTS_BUFFER_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t pending_bytes;
    bool rebuffering;
} ws_tts_buffer_policy_t;

void ws_tts_buffer_policy_reset(ws_tts_buffer_policy_t *policy);
void ws_tts_buffer_policy_enqueue(ws_tts_buffer_policy_t *policy, uint32_t bytes);
void ws_tts_buffer_policy_dequeue(ws_tts_buffer_policy_t *policy, uint32_t bytes);
void ws_tts_buffer_policy_mark_starved(ws_tts_buffer_policy_t *policy);
void ws_tts_buffer_policy_mark_resumed(ws_tts_buffer_policy_t *policy);
bool ws_tts_buffer_policy_ready(const ws_tts_buffer_policy_t *policy, bool playback_started, bool end_pending,
                                uint32_t start_buffer_bytes, uint32_t rebuffer_bytes);
bool ws_tts_buffer_policy_should_finish(bool end_pending, uint32_t pending_frames, bool inflight_active);
uint32_t ws_tts_buffer_policy_pending_ms(const ws_tts_buffer_policy_t *policy, uint32_t bytes_per_second);

#endif
