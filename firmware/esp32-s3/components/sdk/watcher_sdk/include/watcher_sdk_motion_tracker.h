#ifndef WATCHER_SDK_MOTION_TRACKER_H
#define WATCHER_SDK_MOTION_TRACKER_H

#include "watcher_sdk_core.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHER_SDK_MOTION_COMPLETION_GRACE_MS 2000U

typedef enum {
    WATCHER_SDK_MOTION_SIGNAL_ACKED = 0,
    WATCHER_SDK_MOTION_SIGNAL_SUCCESS,
    WATCHER_SDK_MOTION_SIGNAL_STOPPED,
    WATCHER_SDK_MOTION_SIGNAL_INTERRUPTED,
    WATCHER_SDK_MOTION_SIGNAL_REJECTED,
    WATCHER_SDK_MOTION_SIGNAL_FAULT,
} watcher_sdk_motion_signal_t;

typedef enum {
    WATCHER_SDK_MOTION_OUTCOME_NONE = 0,
    WATCHER_SDK_MOTION_OUTCOME_COMPLETED,
    WATCHER_SDK_MOTION_OUTCOME_CANCELLED,
    WATCHER_SDK_MOTION_OUTCOME_FAILED,
} watcher_sdk_motion_outcome_t;

typedef struct {
    uint32_t command_seq;
    watcher_sdk_job_id_t job_id;
    uint32_t deadline_ms;
    bool active;
} watcher_sdk_motion_tracker_t;

void watcher_sdk_motion_tracker_init(watcher_sdk_motion_tracker_t *tracker);
void watcher_sdk_motion_tracker_bind(watcher_sdk_motion_tracker_t *tracker, uint32_t command_seq,
                                     watcher_sdk_job_id_t job_id, uint32_t now_ms, uint32_t duration_ms);
void watcher_sdk_motion_tracker_clear(watcher_sdk_motion_tracker_t *tracker);
bool watcher_sdk_motion_tracker_is_active(const watcher_sdk_motion_tracker_t *tracker);
watcher_sdk_job_id_t watcher_sdk_motion_tracker_job_id(const watcher_sdk_motion_tracker_t *tracker);
watcher_sdk_motion_outcome_t watcher_sdk_motion_tracker_on_signal(watcher_sdk_motion_tracker_t *tracker,
                                                                  uint32_t command_seq,
                                                                  watcher_sdk_motion_signal_t signal);
bool watcher_sdk_motion_tracker_poll_timeout(watcher_sdk_motion_tracker_t *tracker, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_MOTION_TRACKER_H */
