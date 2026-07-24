#include "voice_listening_ui_policy.h"

#include <string.h>

static bool state_is(const char *actual, const char *expected) {
    return actual != NULL && strcmp(actual, expected) == 0;
}

bool voice_listening_ui_should_use_wake_intro(const char *current_behavior_state, bool sleep_animation_visible) {
    if (sleep_animation_visible) {
        return true;
    }

    return state_is(current_behavior_state, "standby_entry") || state_is(current_behavior_state, "standby_start") ||
           state_is(current_behavior_state, "standby_loop") || state_is(current_behavior_state, "listening_wake");
}
