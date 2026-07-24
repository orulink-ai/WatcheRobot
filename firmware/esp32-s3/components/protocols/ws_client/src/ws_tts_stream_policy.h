#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ws_tts_stream_should_continue(uint16_t current_stream_id, uint16_t incoming_stream_id, bool network_eos_seen,
                                   bool playing, uint32_t pending_frames, bool inflight_active);
