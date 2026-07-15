#include "voice_upload_guard.h"

#include <limits.h>
#include <stddef.h>

static uint32_t saturating_add(uint32_t value, uint32_t increment) {
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

void voice_upload_guard_reset(voice_upload_guard_t *guard, uint32_t dropped_frames) {
    if (guard == NULL) {
        return;
    }

    guard->consecutive_send_failures = 0U;
    guard->dropped_frames_since_start = 0U;
    guard->last_dropped_frames = dropped_frames;
    guard->observations_since_start = 0U;
}

voice_upload_guard_result_t voice_upload_guard_observe(voice_upload_guard_t *guard, bool transport_ready,
                                                       bool send_succeeded, uint32_t dropped_frames) {
    uint32_t newly_dropped = 0U;

    if (guard == NULL) {
        return transport_ready ? VOICE_UPLOAD_CONTINUE : VOICE_UPLOAD_ABORT_CLOUD_LOST;
    }
    if (!transport_ready) {
        return VOICE_UPLOAD_ABORT_CLOUD_LOST;
    }

    if (dropped_frames >= guard->last_dropped_frames) {
        newly_dropped = dropped_frames - guard->last_dropped_frames;
    }
    guard->last_dropped_frames = dropped_frames;
    guard->observations_since_start = saturating_add(guard->observations_since_start, 1U);
    if (guard->observations_since_start > VOICE_UPLOAD_DROP_GRACE_OBSERVATIONS) {
        guard->dropped_frames_since_start = saturating_add(guard->dropped_frames_since_start, newly_dropped);
    }

    if (send_succeeded) {
        guard->consecutive_send_failures = 0U;
    } else {
        guard->consecutive_send_failures = saturating_add(guard->consecutive_send_failures, 1U);
    }

    if (guard->consecutive_send_failures >= VOICE_UPLOAD_MAX_CONSECUTIVE_SEND_FAILURES) {
        return VOICE_UPLOAD_ABORT_SEND_FAILURES;
    }
    if (guard->dropped_frames_since_start >= VOICE_UPLOAD_MAX_DROPPED_FRAMES) {
        return VOICE_UPLOAD_ABORT_QUEUE_OVERRUN;
    }
    return VOICE_UPLOAD_CONTINUE;
}
