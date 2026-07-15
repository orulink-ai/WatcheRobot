#ifndef ANIMATION_PLAYBACK_POLICY_H
#define ANIMATION_PLAYBACK_POLICY_H

#include "animation_service.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool animation_playback_request_is_valid(animation_playback_mode_t mode, uint16_t repeat_count);
bool animation_playback_should_cycle(animation_playback_mode_t mode, uint16_t repeat_count, bool resource_loop);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_PLAYBACK_POLICY_H */
