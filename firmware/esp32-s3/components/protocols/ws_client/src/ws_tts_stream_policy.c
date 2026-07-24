#include "ws_tts_stream_policy.h"

static bool stream_id_is_newer(uint16_t candidate, uint16_t current) {
    const uint16_t distance = (uint16_t)(candidate - current);
    return candidate != 0U && current != 0U && distance != 0U && distance < 0x8000U;
}

bool ws_tts_stream_should_continue(uint16_t current_stream_id, uint16_t incoming_stream_id, bool network_eos_seen,
                                   bool playing, uint32_t pending_frames, bool inflight_active) {
    const bool playout_active = playing || pending_frames > 0U || inflight_active;

    return network_eos_seen && playout_active && stream_id_is_newer(incoming_stream_id, current_stream_id);
}
