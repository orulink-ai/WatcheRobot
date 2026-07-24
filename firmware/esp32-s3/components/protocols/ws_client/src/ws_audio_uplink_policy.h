#ifndef WS_AUDIO_UPLINK_POLICY_H
#define WS_AUDIO_UPLINK_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the normal path latency low with two 60 ms PCM frames per write. When
 * the queue or measured send time shows pressure, use the largest batch that
 * still fits in the 8192-byte WebSocket transport buffer. */
#define WS_AUDIO_UPLINK_BASE_BATCH_FRAMES 2U
#define WS_AUDIO_UPLINK_MAX_BATCH_FRAMES 4U
#define WS_AUDIO_UPLINK_HIGH_WATER_FRAMES 4U
#define WS_AUDIO_UPLINK_MAX_WAIT_US 75000U
#define WS_AUDIO_UPLINK_PRESSURE_AGE_US 240000U
#define WS_AUDIO_UPLINK_RECOVERY_SAMPLES 2U

typedef struct {
    bool pressure;
    uint8_t recovery_samples;
} ws_audio_uplink_policy_t;

void ws_audio_uplink_policy_reset(ws_audio_uplink_policy_t *policy);
bool ws_audio_uplink_should_flush(ws_audio_uplink_policy_t *policy, size_t pending_frames, bool end_pending,
                                  uint64_t oldest_age_us);
size_t ws_audio_uplink_batch_frames(ws_audio_uplink_policy_t *policy, size_t pending_frames, bool end_pending,
                                    uint64_t oldest_age_us);
void ws_audio_uplink_observe_send(ws_audio_uplink_policy_t *policy, size_t batch_frames, size_t pending_frames,
                                  uint64_t frame_interval_us, uint64_t send_time_us);
bool ws_audio_uplink_can_keep_up(size_t batch_frames, uint64_t frame_interval_us, uint64_t send_time_us);

#endif
