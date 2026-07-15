#include "animation_playback_policy.h"

bool animation_playback_request_is_valid(animation_playback_mode_t mode, uint16_t repeat_count) {
    switch (mode) {
    case ANIM_PLAYBACK_RESOURCE_DEFAULT:
        return true;
    case ANIM_PLAYBACK_ONCE:
    case ANIM_PLAYBACK_LOOP_UNTIL_REPLACED:
        return repeat_count == 0U;
    case ANIM_PLAYBACK_REPEAT_COUNT:
        return repeat_count > 0U;
    case ANIM_PLAYBACK_MODE_COUNT:
    default:
        return false;
    }
}

bool animation_playback_should_cycle(animation_playback_mode_t mode, uint16_t repeat_count, bool resource_loop) {
    switch (mode) {
    case ANIM_PLAYBACK_ONCE:
        return false;
    case ANIM_PLAYBACK_REPEAT_COUNT:
    case ANIM_PLAYBACK_LOOP_UNTIL_REPLACED:
        return true;
    case ANIM_PLAYBACK_RESOURCE_DEFAULT:
        return repeat_count > 0U || resource_loop;
    case ANIM_PLAYBACK_MODE_COUNT:
    default:
        return false;
    }
}
