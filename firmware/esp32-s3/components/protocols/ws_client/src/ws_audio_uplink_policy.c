#include "ws_audio_uplink_policy.h"

static void update_pressure_from_queue(ws_audio_uplink_policy_t *policy, size_t pending_frames,
                                       uint64_t oldest_age_us) {
    if (policy != NULL &&
        (pending_frames >= WS_AUDIO_UPLINK_HIGH_WATER_FRAMES ||
         oldest_age_us >= WS_AUDIO_UPLINK_PRESSURE_AGE_US)) {
        policy->pressure = true;
        policy->recovery_samples = 0U;
    }
}

void ws_audio_uplink_policy_reset(ws_audio_uplink_policy_t *policy) {
    if (policy == NULL) {
        return;
    }
    policy->pressure = false;
    policy->recovery_samples = 0U;
}

bool ws_audio_uplink_should_flush(ws_audio_uplink_policy_t *policy, size_t pending_frames,
                                  bool end_pending, uint64_t oldest_age_us) {
    if (pending_frames == 0U) {
        return false;
    }

    update_pressure_from_queue(policy, pending_frames, oldest_age_us);
    return (policy != NULL && policy->pressure) ||
           pending_frames >= WS_AUDIO_UPLINK_BASE_BATCH_FRAMES || end_pending ||
           oldest_age_us >= WS_AUDIO_UPLINK_MAX_WAIT_US;
}

size_t ws_audio_uplink_batch_frames(ws_audio_uplink_policy_t *policy, size_t pending_frames,
                                    bool end_pending, uint64_t oldest_age_us) {
    size_t target_frames = WS_AUDIO_UPLINK_BASE_BATCH_FRAMES;

    if (pending_frames == 0U) {
        return 0U;
    }

    update_pressure_from_queue(policy, pending_frames, oldest_age_us);
    if (end_pending || (policy != NULL && policy->pressure)) {
        target_frames = WS_AUDIO_UPLINK_MAX_BATCH_FRAMES;
    }

    return pending_frames < target_frames ? pending_frames : target_frames;
}

void ws_audio_uplink_observe_send(ws_audio_uplink_policy_t *policy, size_t batch_frames,
                                  size_t pending_frames, uint64_t frame_interval_us,
                                  uint64_t send_time_us) {
    if (policy == NULL) {
        return;
    }

    if (!ws_audio_uplink_can_keep_up(batch_frames, frame_interval_us, send_time_us)) {
        policy->pressure = true;
        policy->recovery_samples = 0U;
        return;
    }

    if (!policy->pressure || pending_frames > WS_AUDIO_UPLINK_BASE_BATCH_FRAMES) {
        policy->recovery_samples = 0U;
        return;
    }

    if (policy->recovery_samples < WS_AUDIO_UPLINK_RECOVERY_SAMPLES) {
        policy->recovery_samples++;
    }
    if (policy->recovery_samples >= WS_AUDIO_UPLINK_RECOVERY_SAMPLES) {
        policy->pressure = false;
        policy->recovery_samples = 0U;
    }
}

bool ws_audio_uplink_can_keep_up(size_t batch_frames, uint64_t frame_interval_us, uint64_t send_time_us) {
    if (batch_frames == 0U || frame_interval_us == 0U) {
        return false;
    }

    return send_time_us <= (uint64_t)batch_frames * frame_interval_us;
}
