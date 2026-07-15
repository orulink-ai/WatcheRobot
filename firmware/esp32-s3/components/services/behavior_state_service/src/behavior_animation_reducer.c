#include "behavior_animation_reducer.h"

behavior_animation_terminal_t behavior_animation_reduce_terminal(animation_ticket_t current_ticket,
                                                                 uint32_t current_owner_epoch,
                                                                 emoji_anim_type_t expected_type,
                                                                 const animation_event_t *event) {
    if (event == NULL || current_ticket == ANIMATION_TICKET_INVALID || event->ticket != current_ticket ||
        event->request.source != ANIM_SOURCE_BEHAVIOR || event->request.owner_epoch != current_owner_epoch ||
        event->request.type != expected_type) {
        return BEHAVIOR_ANIMATION_TERMINAL_IGNORE;
    }

    switch (event->type) {
    case ANIMATION_EVENT_COMPLETED:
        return BEHAVIOR_ANIMATION_TERMINAL_COMPLETED;
    case ANIMATION_EVENT_FAILED:
        return BEHAVIOR_ANIMATION_TERMINAL_FAILED;
    case ANIMATION_EVENT_PREEMPTED:
    case ANIMATION_EVENT_CANCELLED:
        return BEHAVIOR_ANIMATION_TERMINAL_RELEASED;
    case ANIMATION_EVENT_ACCEPTED:
    case ANIMATION_EVENT_PREPARING:
    case ANIMATION_EVENT_COMMITTED:
    case ANIMATION_EVENT_CYCLE_COMPLETED:
    default:
        return BEHAVIOR_ANIMATION_TERMINAL_IGNORE;
    }
}
