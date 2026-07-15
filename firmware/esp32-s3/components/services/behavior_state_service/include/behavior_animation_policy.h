#ifndef BEHAVIOR_ANIMATION_POLICY_H
#define BEHAVIOR_ANIMATION_POLICY_H

#include "animation_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    animation_priority_t priority;
    animation_preempt_policy_t preempt_policy;
} behavior_animation_policy_t;

behavior_animation_policy_t behavior_animation_policy_resolve(const char *state_id, emoji_anim_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_ANIMATION_POLICY_H */
