#include "behavior_animation_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool string_in_list(const char *value, const char *const *values, size_t count) {
    size_t index;

    if (value == NULL) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        if (strcmp(value, values[index]) == 0) {
            return true;
        }
    }
    return false;
}

behavior_animation_policy_t behavior_animation_policy_resolve(const char *state_id, emoji_anim_type_t type) {
    static const char *const ambient_states[] = {
        "standby", "standby_entry", "standby_loop", "standby1", "standby2", "standby3", "standby4", "blink",
    };
    static const char *const interaction_states[] = {
        "listening_wake", "listening", "thinking", "processing", "speaking", "music",
    };
    static const char *const system_states[] = {
        "upgrade",
        "recharge",
    };
    behavior_animation_policy_t policy = {
        .priority = ANIM_PRIORITY_BEHAVIOR,
        .preempt_policy = ANIM_PREEMPTIBLE,
    };

    if (string_in_list(state_id, ambient_states, sizeof(ambient_states) / sizeof(ambient_states[0]))) {
        policy.priority = ANIM_PRIORITY_AMBIENT;
    } else if (string_in_list(state_id, interaction_states,
                              sizeof(interaction_states) / sizeof(interaction_states[0]))) {
        policy.priority = ANIM_PRIORITY_INTERACTION;
    } else if (string_in_list(state_id, system_states, sizeof(system_states) / sizeof(system_states[0]))) {
        policy.priority = ANIM_PRIORITY_SYSTEM;
    }

    if (type == EMOJI_ANIM_STANDBY_START || type == EMOJI_ANIM_STANDBY_END) {
        policy.preempt_policy = ANIM_PROTECTED_AFTER_COMMIT;
    }
    return policy;
}
