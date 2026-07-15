#ifndef BEHAVIOR_MOTION_SEQUENCE_H
#define BEHAVIOR_MOTION_SEQUENCE_H

#include "behavior_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t behavior_motion_sequence_play(const behavior_motion_event_t *events, int event_count);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_MOTION_SEQUENCE_H */
