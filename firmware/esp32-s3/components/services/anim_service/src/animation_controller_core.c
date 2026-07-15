/**
 * @file animation_controller_core.c
 * @brief Allocation-free animation arbitration state machine.
 */

#include "animation_controller_core.h"
#include "animation_playback_policy.h"

#include <limits.h>
#include <string.h>

static void output_reset(animation_controller_output_t *output) {
    memset(output, 0, sizeof(*output));
    output->player_command.type = ANIMATION_PLAYER_COMMAND_NONE;
    output->player_command.ticket = ANIMATION_TICKET_INVALID;
}

static animation_controller_ticket_slot_t *find_ticket(animation_controller_t *controller, animation_ticket_t ticket) {
    size_t index;

    if (ticket == ANIMATION_TICKET_INVALID) {
        return NULL;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (controller->tickets[index].in_use && controller->tickets[index].ticket == ticket) {
            return &controller->tickets[index];
        }
    }
    return NULL;
}

static const animation_controller_ticket_slot_t *find_ticket_const(const animation_controller_t *controller,
                                                                   animation_ticket_t ticket) {
    size_t index;

    if (ticket == ANIMATION_TICKET_INVALID) {
        return NULL;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (controller->tickets[index].in_use && controller->tickets[index].ticket == ticket) {
            return &controller->tickets[index];
        }
    }
    return NULL;
}

static animation_controller_ticket_slot_t *find_free_slot(animation_controller_t *controller) {
    size_t index;

    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (!controller->tickets[index].in_use) {
            return &controller->tickets[index];
        }
    }
    return NULL;
}

static animation_ticket_t allocate_ticket(animation_controller_t *controller) {
    uint32_t attempts;

    for (attempts = 0U; attempts < UINT32_MAX; ++attempts) {
        animation_ticket_t candidate = controller->next_ticket;
        controller->next_ticket++;
        if (controller->next_ticket == ANIMATION_TICKET_INVALID) {
            controller->next_ticket = 1U;
        }
        if (candidate != ANIMATION_TICKET_INVALID && find_ticket(controller, candidate) == NULL) {
            return candidate;
        }
    }
    return ANIMATION_TICKET_INVALID;
}

static void add_event(animation_controller_output_t *output, const animation_controller_ticket_slot_t *slot,
                      animation_event_type_t type, animation_failure_t failure) {
    animation_event_t *event;

    if (output->event_count >= ANIMATION_CONTROLLER_OUTPUT_EVENT_CAPACITY) {
        return;
    }
    event = &output->events[output->event_count++];
    event->ticket = slot->ticket;
    event->type = type;
    event->failure = failure;
    event->request = slot->request;
    event->completed_cycles = slot->completed_cycles;
}

static bool is_terminal_event(animation_event_type_t type) {
    return type == ANIMATION_EVENT_COMPLETED || type == ANIMATION_EVENT_PREEMPTED ||
           type == ANIMATION_EVENT_CANCELLED || type == ANIMATION_EVENT_FAILED;
}

void animation_controller_record_terminal_metrics(animation_controller_t *controller,
                                                  animation_event_type_t terminal_type) {
    if (controller == NULL || !is_terminal_event(terminal_type)) {
        return;
    }
    controller->emitted_terminal_count++;
    switch (terminal_type) {
    case ANIMATION_EVENT_COMPLETED:
        controller->completed_count++;
        break;
    case ANIMATION_EVENT_PREEMPTED:
        controller->preempted_count++;
        break;
    case ANIMATION_EVENT_CANCELLED:
        controller->cancelled_count++;
        break;
    case ANIMATION_EVENT_FAILED:
        controller->failed_count++;
        break;
    default:
        break;
    }
}

static void terminate_ticket(animation_controller_t *controller, animation_controller_ticket_slot_t *slot,
                             animation_event_type_t type, animation_failure_t failure,
                             animation_controller_output_t *output) {
    animation_ticket_t ticket = slot->ticket;

    if (!slot->in_use || !is_terminal_event(type)) {
        return;
    }
    add_event(output, slot, type, failure);
    animation_controller_record_terminal_metrics(controller, type);
    if (controller->active_ticket == ticket) {
        controller->active_ticket = ANIMATION_TICKET_INVALID;
    }
    if (controller->desired_ticket == ticket) {
        controller->desired_ticket = ANIMATION_TICKET_INVALID;
    }
    memset(slot, 0, sizeof(*slot));
}

static bool request_is_valid(const animation_request_t *request) {
    return request != NULL && request->type >= 0 && request->type < EMOJI_ANIM_COUNT &&
           request->priority >= ANIM_PRIORITY_AMBIENT && request->priority < ANIM_PRIORITY_COUNT &&
           request->preempt_policy >= ANIM_PREEMPTIBLE && request->preempt_policy < ANIM_PREEMPT_POLICY_COUNT &&
           animation_playback_request_is_valid(request->playback_mode, request->repeat_count) &&
           request->source >= ANIM_SOURCE_UNKNOWN && request->source < ANIM_SOURCE_COUNT;
}

static void set_play_command(animation_controller_output_t *output, const animation_controller_ticket_slot_t *slot) {
    output->player_command.type = ANIMATION_PLAYER_COMMAND_PLAY;
    output->player_command.ticket = slot->ticket;
    output->player_command.request = slot->request;
}

static void begin_preparing(animation_controller_ticket_slot_t *slot, animation_controller_output_t *output) {
    slot->phase = ANIMATION_TICKET_PHASE_PREPARING;
    add_event(output, slot, ANIMATION_EVENT_PREPARING, ANIMATION_FAILURE_NONE);
    set_play_command(output, slot);
}

static void remove_queue_at(animation_controller_t *controller, size_t queue_index) {
    size_t move_count;

    if (queue_index >= controller->queued_count) {
        return;
    }
    move_count = controller->queued_count - queue_index - 1U;
    if (move_count > 0U) {
        memmove(&controller->queued_tickets[queue_index], &controller->queued_tickets[queue_index + 1U],
                move_count * sizeof(controller->queued_tickets[0]));
    }
    controller->queued_count--;
    controller->queued_tickets[controller->queued_count] = ANIMATION_TICKET_INVALID;
}

static size_t select_next_queue_index(const animation_controller_t *controller) {
    size_t best_index = 0U;
    const animation_controller_ticket_slot_t *best = find_ticket_const(controller, controller->queued_tickets[0]);
    size_t index;

    for (index = 1U; index < controller->queued_count; ++index) {
        const animation_controller_ticket_slot_t *candidate =
            find_ticket_const(controller, controller->queued_tickets[index]);
        if (candidate != NULL &&
            (best == NULL || candidate->request.priority > best->request.priority ||
             (candidate->request.priority == best->request.priority && candidate->sequence > best->sequence))) {
            best = candidate;
            best_index = index;
        }
    }
    return best_index;
}

static void recalculate_desired(animation_controller_t *controller) {
    const animation_controller_ticket_slot_t *latest = find_ticket_const(controller, controller->active_ticket);
    size_t index;

    for (index = 0U; index < controller->queued_count; ++index) {
        const animation_controller_ticket_slot_t *candidate =
            find_ticket_const(controller, controller->queued_tickets[index]);
        if (candidate != NULL && (latest == NULL || candidate->sequence > latest->sequence)) {
            latest = candidate;
        }
    }
    controller->desired_ticket = latest == NULL ? ANIMATION_TICKET_INVALID : latest->ticket;
}

static void promote_next(animation_controller_t *controller, animation_controller_output_t *output,
                         bool stop_if_empty) {
    animation_controller_ticket_slot_t *next;
    size_t queue_index;
    animation_ticket_t next_ticket;

    if (controller->queued_count == 0U) {
        if (stop_if_empty) {
            output->player_command.type = ANIMATION_PLAYER_COMMAND_STOP;
            controller->visible_ticket = ANIMATION_TICKET_INVALID;
            controller->visible_type = EMOJI_ANIM_NONE;
        }
        recalculate_desired(controller);
        return;
    }

    queue_index = select_next_queue_index(controller);
    next_ticket = controller->queued_tickets[queue_index];
    remove_queue_at(controller, queue_index);
    next = find_ticket(controller, next_ticket);
    if (next == NULL) {
        promote_next(controller, output, stop_if_empty);
        return;
    }
    controller->active_ticket = next_ticket;
    begin_preparing(next, output);
    recalculate_desired(controller);
}

static bool active_can_be_preempted(const animation_controller_ticket_slot_t *active,
                                    const animation_request_t *incoming) {
    if (active == NULL) {
        return true;
    }
    if (incoming->priority == ANIM_PRIORITY_SYSTEM) {
        return true;
    }
    if (active->request.preempt_policy == ANIM_PROTECTED_AFTER_COMMIT &&
        active->phase == ANIMATION_TICKET_PHASE_COMMITTED) {
        return false;
    }
    if (active->request.source == incoming->source && active->request.owner_epoch == incoming->owner_epoch) {
        return (int32_t)(incoming->correlation_id - active->request.correlation_id) > 0;
    }
    return incoming->priority >= active->request.priority;
}

static bool is_committed_protected(const animation_controller_ticket_slot_t *slot) {
    return slot != NULL && slot->request.preempt_policy == ANIM_PROTECTED_AFTER_COMMIT &&
           slot->phase == ANIMATION_TICKET_PHASE_COMMITTED;
}

static const animation_controller_ticket_slot_t *find_latest_owner_ticket(const animation_controller_t *controller,
                                                                          const animation_request_t *request) {
    const animation_controller_ticket_slot_t *latest = NULL;
    size_t index;

    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        const animation_controller_ticket_slot_t *candidate = &controller->tickets[index];
        if (candidate->in_use && candidate->request.source == request->source &&
            candidate->request.owner_epoch == request->owner_epoch &&
            (latest == NULL || candidate->sequence > latest->sequence)) {
            latest = candidate;
        }
    }
    return latest;
}

static size_t count_queued_owner_tickets(const animation_controller_t *controller, const animation_request_t *request) {
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < controller->queued_count; ++index) {
        const animation_controller_ticket_slot_t *queued =
            find_ticket_const(controller, controller->queued_tickets[index]);
        if (queued != NULL && queued->request.source == request->source &&
            queued->request.owner_epoch == request->owner_epoch) {
            count++;
        }
    }
    return count;
}

static void preempt_queued_owner_tickets(animation_controller_t *controller, const animation_request_t *request,
                                         animation_controller_output_t *output) {
    size_t index = 0U;

    while (index < controller->queued_count) {
        animation_controller_ticket_slot_t *queued = find_ticket(controller, controller->queued_tickets[index]);
        if (queued != NULL && queued->request.source == request->source &&
            queued->request.owner_epoch == request->owner_epoch) {
            remove_queue_at(controller, index);
            terminate_ticket(controller, queued, ANIMATION_EVENT_PREEMPTED, ANIMATION_FAILURE_NONE, output);
        } else {
            index++;
        }
    }
}

void animation_controller_init(animation_controller_t *controller) {
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->active_ticket = ANIMATION_TICKET_INVALID;
    controller->desired_ticket = ANIMATION_TICKET_INVALID;
    controller->visible_ticket = ANIMATION_TICKET_INVALID;
    controller->visible_type = EMOJI_ANIM_NONE;
    controller->next_ticket = 1U;
    controller->next_sequence = 1U;
}

animation_service_result_t animation_controller_submit(animation_controller_t *controller,
                                                       const animation_request_t *request,
                                                       animation_ticket_t *out_ticket,
                                                       animation_controller_output_t *output) {
    animation_controller_ticket_slot_t *active;
    animation_controller_ticket_slot_t *slot;
    const animation_controller_ticket_slot_t *latest_owner;
    animation_ticket_t ticket;
    bool preempts_active;
    size_t queued_owner_count;

    if (out_ticket != NULL) {
        *out_ticket = ANIMATION_TICKET_INVALID;
    }
    if (output == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    output_reset(output);
    if (controller == NULL || out_ticket == NULL || !request_is_valid(request)) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }

    active = find_ticket(controller, controller->active_ticket);
    latest_owner = find_latest_owner_ticket(controller, request);
    if (latest_owner != NULL && (int32_t)(request->correlation_id - latest_owner->request.correlation_id) <= 0) {
        return ANIMATION_SERVICE_INVALID_TRANSITION;
    }
    queued_owner_count = count_queued_owner_tickets(controller, request);
    preempts_active = active_can_be_preempted(active, request);
    if (active != NULL && !preempts_active) {
        if (controller->queued_count - queued_owner_count >= ANIMATION_CONTROLLER_QUEUE_CAPACITY) {
            return ANIMATION_SERVICE_QUEUE_FULL;
        }
    }

    slot = find_free_slot(controller);
    if (slot == NULL) {
        output_reset(output);
        return ANIMATION_SERVICE_TICKET_TABLE_FULL;
    }

    ticket = allocate_ticket(controller);
    if (ticket == ANIMATION_TICKET_INVALID) {
        output_reset(output);
        return ANIMATION_SERVICE_TICKET_TABLE_FULL;
    }
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->ticket = ticket;
    slot->request = *request;
    slot->sequence = controller->next_sequence++;
    slot->phase = ANIMATION_TICKET_PHASE_ACCEPTED;

    if (active != NULL && preempts_active) {
        terminate_ticket(controller, active, ANIMATION_EVENT_PREEMPTED, ANIMATION_FAILURE_NONE, output);
    }
    preempt_queued_owner_tickets(controller, request, output);

    add_event(output, slot, ANIMATION_EVENT_ACCEPTED, ANIMATION_FAILURE_NONE);
    controller->accepted_count++;
    *out_ticket = ticket;
    controller->desired_ticket = ticket;

    if (controller->active_ticket == ANIMATION_TICKET_INVALID) {
        controller->active_ticket = ticket;
        begin_preparing(slot, output);
    } else {
        controller->queued_tickets[controller->queued_count++] = ticket;
    }
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_controller_cancel(animation_controller_t *controller, animation_ticket_t ticket,
                                                       animation_controller_output_t *output) {
    animation_controller_ticket_slot_t *slot;
    bool was_active;
    size_t index;

    if (output == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    output_reset(output);
    if (controller == NULL || ticket == ANIMATION_TICKET_INVALID) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    slot = find_ticket(controller, ticket);
    if (slot == NULL) {
        return ANIMATION_SERVICE_NOT_FOUND;
    }
    was_active = controller->active_ticket == ticket;
    if (was_active && is_committed_protected(slot)) {
        return ANIMATION_SERVICE_INVALID_TRANSITION;
    }
    if (!was_active) {
        for (index = 0U; index < controller->queued_count; ++index) {
            if (controller->queued_tickets[index] == ticket) {
                remove_queue_at(controller, index);
                break;
            }
        }
    }
    terminate_ticket(controller, slot, ANIMATION_EVENT_CANCELLED, ANIMATION_FAILURE_NONE, output);
    if (was_active) {
        promote_next(controller, output, true);
    } else {
        recalculate_desired(controller);
    }
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_controller_cancel_owner(animation_controller_t *controller,
                                                             animation_source_t source, uint32_t owner_epoch,
                                                             animation_controller_output_t *output) {
    bool cancelled_any = false;
    bool cancelled_active = false;
    bool protected_active_matches = false;
    animation_controller_ticket_slot_t *active;
    size_t index = 0U;

    if (output == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    output_reset(output);
    if (controller == NULL || source < ANIM_SOURCE_UNKNOWN || source >= ANIM_SOURCE_COUNT) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }

    active = find_ticket(controller, controller->active_ticket);
    if (active != NULL && active->request.source == source && active->request.owner_epoch == owner_epoch) {
        if (is_committed_protected(active)) {
            protected_active_matches = true;
        } else {
            terminate_ticket(controller, active, ANIMATION_EVENT_CANCELLED, ANIMATION_FAILURE_NONE, output);
            cancelled_any = true;
            cancelled_active = true;
        }
    }

    while (index < controller->queued_count) {
        animation_controller_ticket_slot_t *queued = find_ticket(controller, controller->queued_tickets[index]);
        if (queued != NULL && queued->request.source == source && queued->request.owner_epoch == owner_epoch) {
            remove_queue_at(controller, index);
            terminate_ticket(controller, queued, ANIMATION_EVENT_CANCELLED, ANIMATION_FAILURE_NONE, output);
            cancelled_any = true;
        } else {
            index++;
        }
    }

    if (!cancelled_any) {
        return protected_active_matches ? ANIMATION_SERVICE_INVALID_TRANSITION : ANIMATION_SERVICE_NOT_FOUND;
    }
    if (cancelled_active) {
        promote_next(controller, output, true);
    } else {
        recalculate_desired(controller);
    }
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_controller_handle_player_event(animation_controller_t *controller,
                                                                    animation_ticket_t ticket,
                                                                    animation_player_event_type_t event,
                                                                    animation_failure_t failure,
                                                                    animation_controller_output_t *output) {
    animation_controller_ticket_slot_t *slot;

    if (output == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    output_reset(output);
    if (controller == NULL || ticket == ANIMATION_TICKET_INVALID) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    slot = find_ticket(controller, ticket);
    if (slot == NULL || controller->active_ticket != ticket) {
        controller->orphan_player_event_count++;
        return ANIMATION_SERVICE_NOT_FOUND;
    }

    switch (event) {
    case ANIMATION_PLAYER_EVENT_COMMITTED:
        if (slot->phase != ANIMATION_TICKET_PHASE_PREPARING) {
            controller->wrong_transition_count++;
            return ANIMATION_SERVICE_INVALID_TRANSITION;
        }
        slot->phase = ANIMATION_TICKET_PHASE_COMMITTED;
        controller->visible_ticket = ticket;
        controller->visible_type = slot->request.type;
        add_event(output, slot, ANIMATION_EVENT_COMMITTED, ANIMATION_FAILURE_NONE);
        return ANIMATION_SERVICE_OK;

    case ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED:
        if (slot->phase != ANIMATION_TICKET_PHASE_COMMITTED) {
            controller->wrong_transition_count++;
            return ANIMATION_SERVICE_INVALID_TRANSITION;
        }
        slot->completed_cycles++;
        add_event(output, slot, ANIMATION_EVENT_CYCLE_COMPLETED, ANIMATION_FAILURE_NONE);
        if (slot->request.repeat_count > 0U && slot->completed_cycles >= slot->request.repeat_count) {
            bool has_queued_successor = controller->queued_count > 0U;
            terminate_ticket(controller, slot, ANIMATION_EVENT_COMPLETED, ANIMATION_FAILURE_NONE, output);
            if (has_queued_successor) {
                promote_next(controller, output, false);
            } else {
                output->player_command.type = ANIMATION_PLAYER_COMMAND_STOP;
                recalculate_desired(controller);
            }
        }
        return ANIMATION_SERVICE_OK;

    case ANIMATION_PLAYER_EVENT_COMPLETED:
        if (slot->phase != ANIMATION_TICKET_PHASE_COMMITTED) {
            controller->wrong_transition_count++;
            return ANIMATION_SERVICE_INVALID_TRANSITION;
        }
        terminate_ticket(controller, slot, ANIMATION_EVENT_COMPLETED, ANIMATION_FAILURE_NONE, output);
        promote_next(controller, output, false);
        return ANIMATION_SERVICE_OK;

    case ANIMATION_PLAYER_EVENT_FAILED:
        if (failure == ANIMATION_FAILURE_NONE) {
            return ANIMATION_SERVICE_INVALID_ARGUMENT;
        }
        terminate_ticket(controller, slot, ANIMATION_EVENT_FAILED, failure, output);
        promote_next(controller, output, false);
        return ANIMATION_SERVICE_OK;

    default:
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
}

void animation_controller_get_snapshot(const animation_controller_t *controller, animation_snapshot_t *snapshot) {
    const animation_controller_ticket_slot_t *active;
    const animation_controller_ticket_slot_t *desired;
    size_t index;

    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active_ticket = ANIMATION_TICKET_INVALID;
    snapshot->desired_ticket = ANIMATION_TICKET_INVALID;
    snapshot->preparing_ticket = ANIMATION_TICKET_INVALID;
    snapshot->visible_ticket = ANIMATION_TICKET_INVALID;
    snapshot->active_type = EMOJI_ANIM_NONE;
    snapshot->desired_type = EMOJI_ANIM_NONE;
    snapshot->preparing_type = EMOJI_ANIM_NONE;
    snapshot->visible_type = EMOJI_ANIM_NONE;
    if (controller == NULL) {
        return;
    }

    snapshot->active_ticket = controller->active_ticket;
    snapshot->desired_ticket = controller->desired_ticket;
    snapshot->visible_ticket = controller->visible_ticket;
    snapshot->visible_type = controller->visible_type;
    snapshot->queued_count = controller->queued_count;
    snapshot->emitted_terminal_count = controller->emitted_terminal_count;
    snapshot->accepted_count = controller->accepted_count;
    snapshot->terminal_count = controller->emitted_terminal_count;
    snapshot->completed_count = controller->completed_count;
    snapshot->preempted_count = controller->preempted_count;
    snapshot->cancelled_count = controller->cancelled_count;
    snapshot->failed_count = controller->failed_count;
    snapshot->orphan_player_event_count = controller->orphan_player_event_count;
    snapshot->wrong_transition_count = controller->wrong_transition_count;

    active = find_ticket_const(controller, controller->active_ticket);
    if (active != NULL) {
        snapshot->active_type = active->request.type;
        if (active->phase == ANIMATION_TICKET_PHASE_PREPARING) {
            snapshot->preparing_ticket = active->ticket;
            snapshot->preparing_type = active->request.type;
        }
    }
    desired = find_ticket_const(controller, controller->desired_ticket);
    if (desired != NULL) {
        snapshot->desired_type = desired->request.type;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (controller->tickets[index].in_use) {
            snapshot->live_ticket_count++;
        }
    }
}
