#ifndef BEHAVIOR_ANIMATION_REDUCER_H
#define BEHAVIOR_ANIMATION_REDUCER_H

#include "animation_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BEHAVIOR_ANIMATION_TERMINAL_IGNORE = 0,
    BEHAVIOR_ANIMATION_TERMINAL_COMPLETED,
    BEHAVIOR_ANIMATION_TERMINAL_FAILED,
    BEHAVIOR_ANIMATION_TERMINAL_RELEASED,
} behavior_animation_terminal_t;

behavior_animation_terminal_t behavior_animation_reduce_terminal(animation_ticket_t current_ticket,
                                                                 uint32_t current_owner_epoch,
                                                                 emoji_anim_type_t expected_type,
                                                                 const animation_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_ANIMATION_REDUCER_H */
