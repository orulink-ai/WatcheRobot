#include "ws_tts_buffer_policy.h"

#include <limits.h>
#include <stddef.h>

static uint32_t saturating_add(uint32_t value, uint32_t increment) {
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

void ws_tts_buffer_policy_reset(ws_tts_buffer_policy_t *policy) {
    if (policy == NULL) {
        return;
    }
    policy->pending_bytes = 0U;
    policy->rebuffering = false;
}

void ws_tts_buffer_policy_enqueue(ws_tts_buffer_policy_t *policy, uint32_t bytes) {
    if (policy == NULL) {
        return;
    }
    policy->pending_bytes = saturating_add(policy->pending_bytes, bytes);
}

void ws_tts_buffer_policy_dequeue(ws_tts_buffer_policy_t *policy, uint32_t bytes) {
    if (policy == NULL) {
        return;
    }
    policy->pending_bytes = bytes >= policy->pending_bytes ? 0U : policy->pending_bytes - bytes;
}

void ws_tts_buffer_policy_mark_starved(ws_tts_buffer_policy_t *policy) {
    if (policy != NULL) {
        policy->rebuffering = true;
    }
}

void ws_tts_buffer_policy_mark_resumed(ws_tts_buffer_policy_t *policy) {
    if (policy != NULL) {
        policy->rebuffering = false;
    }
}

bool ws_tts_buffer_policy_ready(const ws_tts_buffer_policy_t *policy, bool playback_started, bool end_pending,
                                uint32_t start_buffer_bytes, uint32_t rebuffer_bytes) {
    uint32_t required_bytes;

    if (policy == NULL || policy->pending_bytes == 0U) {
        return false;
    }
    if (end_pending) {
        return true;
    }

    required_bytes = start_buffer_bytes;
    if (playback_started && policy->rebuffering) {
        required_bytes = rebuffer_bytes;
    } else if (playback_started) {
        return true;
    }
    return policy->pending_bytes >= required_bytes;
}

bool ws_tts_buffer_policy_should_finish(bool end_pending, uint32_t pending_frames, bool inflight_active) {
    return end_pending && pending_frames == 0U && !inflight_active;
}

uint32_t ws_tts_buffer_policy_pending_ms(const ws_tts_buffer_policy_t *policy, uint32_t bytes_per_second) {
    uint64_t milliseconds;

    if (policy == NULL || bytes_per_second == 0U) {
        return 0U;
    }
    milliseconds = ((uint64_t)policy->pending_bytes * 1000ULL) / bytes_per_second;
    return milliseconds > UINT32_MAX ? UINT32_MAX : (uint32_t)milliseconds;
}
