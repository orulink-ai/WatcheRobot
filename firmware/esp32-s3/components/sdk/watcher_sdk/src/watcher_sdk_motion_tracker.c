#include "watcher_sdk_motion_tracker.h"

#include <string.h>

void watcher_sdk_motion_tracker_init(watcher_sdk_motion_tracker_t *tracker) {
    if (tracker != NULL) {
        memset(tracker, 0, sizeof(*tracker));
    }
}

void watcher_sdk_motion_tracker_bind(watcher_sdk_motion_tracker_t *tracker, uint32_t command_seq,
                                     watcher_sdk_job_id_t job_id, uint32_t now_ms, uint32_t duration_ms) {
    if (tracker == NULL || command_seq == 0U || job_id == WATCHER_SDK_JOB_INVALID) {
        return;
    }
    tracker->command_seq = command_seq;
    tracker->job_id = job_id;
    tracker->deadline_ms = now_ms + duration_ms + WATCHER_SDK_MOTION_COMPLETION_GRACE_MS;
    tracker->active = true;
}

void watcher_sdk_motion_tracker_clear(watcher_sdk_motion_tracker_t *tracker) {
    watcher_sdk_motion_tracker_init(tracker);
}

bool watcher_sdk_motion_tracker_is_active(const watcher_sdk_motion_tracker_t *tracker) {
    return tracker != NULL && tracker->active;
}

watcher_sdk_job_id_t watcher_sdk_motion_tracker_job_id(const watcher_sdk_motion_tracker_t *tracker) {
    return watcher_sdk_motion_tracker_is_active(tracker) ? tracker->job_id : WATCHER_SDK_JOB_INVALID;
}

watcher_sdk_motion_outcome_t watcher_sdk_motion_tracker_on_signal(watcher_sdk_motion_tracker_t *tracker,
                                                                  uint32_t command_seq,
                                                                  watcher_sdk_motion_signal_t signal) {
    watcher_sdk_motion_outcome_t outcome = WATCHER_SDK_MOTION_OUTCOME_NONE;

    if (!watcher_sdk_motion_tracker_is_active(tracker) ||
        (command_seq != tracker->command_seq &&
         !(command_seq == 0U && signal == WATCHER_SDK_MOTION_SIGNAL_FAULT))) {
        return WATCHER_SDK_MOTION_OUTCOME_NONE;
    }
    switch (signal) {
    case WATCHER_SDK_MOTION_SIGNAL_ACKED:
        return WATCHER_SDK_MOTION_OUTCOME_NONE;
    case WATCHER_SDK_MOTION_SIGNAL_SUCCESS:
        outcome = WATCHER_SDK_MOTION_OUTCOME_COMPLETED;
        break;
    case WATCHER_SDK_MOTION_SIGNAL_STOPPED:
    case WATCHER_SDK_MOTION_SIGNAL_INTERRUPTED:
        outcome = WATCHER_SDK_MOTION_OUTCOME_CANCELLED;
        break;
    case WATCHER_SDK_MOTION_SIGNAL_REJECTED:
    case WATCHER_SDK_MOTION_SIGNAL_FAULT:
        outcome = WATCHER_SDK_MOTION_OUTCOME_FAILED;
        break;
    default:
        return WATCHER_SDK_MOTION_OUTCOME_NONE;
    }
    watcher_sdk_motion_tracker_clear(tracker);
    return outcome;
}

bool watcher_sdk_motion_tracker_poll_timeout(watcher_sdk_motion_tracker_t *tracker, uint32_t now_ms) {
    if (!watcher_sdk_motion_tracker_is_active(tracker) || (int32_t)(now_ms - tracker->deadline_ms) < 0) {
        return false;
    }
    watcher_sdk_motion_tracker_clear(tracker);
    return true;
}
