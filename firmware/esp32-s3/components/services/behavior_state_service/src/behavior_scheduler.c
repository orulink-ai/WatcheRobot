#include "behavior_scheduler.h"

#include <stddef.h>
#include <string.h>

#define BEHAVIOR_SCHEDULER_ACTION_MOTION_LOOKAHEAD_MS 80U

bool behavior_scheduler_state_motion_overridden(const behavior_action_def_t *action) {
    return action != NULL;
}

bool behavior_scheduler_action_should_loop(const behavior_state_def_t *state, const behavior_action_def_t *action) {
    if (state == NULL || action == NULL) {
        return false;
    }

    return state->loop || state->hold_until_replaced;
}

bool behavior_scheduler_all_action_events_dispatched(const behavior_action_def_t *action, int next_action_motion_index) {
    return action == NULL || next_action_motion_index >= action->motion_count;
}

bool behavior_scheduler_all_state_events_dispatched(const behavior_scheduler_snapshot_t *snapshot) {
    int motion_count;

    if (snapshot == NULL || snapshot->state == NULL) {
        return false;
    }

    motion_count = behavior_scheduler_state_motion_overridden(snapshot->action) ? 0 : snapshot->state->motion_count;
    return snapshot->next_motion_index >= motion_count &&
           snapshot->next_expression_index >= snapshot->state->expression_count &&
           snapshot->next_sound_index >= snapshot->state->sound_count;
}

uint32_t behavior_scheduler_non_loop_done_at_ms(const behavior_state_def_t *state, const behavior_action_def_t *action,
                                                uint32_t default_hold_ms) {
    uint32_t done_at_ms;

    if (state == NULL) {
        return 0;
    }

    done_at_ms = state->timeline_end_ms > 0 ? state->timeline_end_ms : default_hold_ms;
    if (action != NULL && action->total_duration_ms > done_at_ms) {
        done_at_ms = action->total_duration_ms;
    }

    return done_at_ms;
}

static bool append_command(behavior_scheduler_tick_result_t *result, const behavior_scheduler_command_t *command) {
    if (result == NULL || command == NULL) {
        return false;
    }
    if (result->command_count >= BEHAVIOR_SCHEDULER_COMMAND_CAPACITY) {
        result->has_more_due_events = true;
        return false;
    }

    result->commands[result->command_count] = *command;
    result->command_count++;
    return true;
}

static bool action_motion_can_prequeue(const behavior_scheduler_tick_input_t *input, int motion_index) {
    const behavior_motion_event_t *previous = NULL;
    const behavior_motion_event_t *current = NULL;
    uint32_t previous_end_ms;

    if (input == NULL || input->action == NULL || motion_index <= 0 || motion_index >= input->action->motion_count) {
        return false;
    }

    current = &input->action->motion[motion_index];
    if (current->at_ms <= input->action_elapsed_ms) {
        return true;
    }

    previous = &input->action->motion[motion_index - 1];
    previous_end_ms = previous->at_ms + (uint32_t)previous->duration_ms;
    return previous_end_ms >= current->at_ms &&
           current->at_ms <= input->action_elapsed_ms + BEHAVIOR_SCHEDULER_ACTION_MOTION_LOOKAHEAD_MS;
}

void behavior_scheduler_collect_due_events(const behavior_scheduler_tick_input_t *input,
                                           behavior_scheduler_tick_result_t *out_result) {
    behavior_scheduler_command_t command;

    if (out_result == NULL) {
        return;
    }

    memset(out_result, 0, sizeof(*out_result));
    if (input == NULL) {
        return;
    }

    out_result->next_motion_index = input->next_motion_index;
    out_result->next_action_motion_index = input->next_action_motion_index;
    out_result->next_expression_index = input->next_expression_index;
    out_result->next_sound_index = input->next_sound_index;

    if (input->state == NULL) {
        return;
    }

    if (!behavior_scheduler_state_motion_overridden(input->action)) {
        while (out_result->next_motion_index < input->state->motion_count &&
               input->state->motion[out_result->next_motion_index].at_ms <= input->state_elapsed_ms) {
            memset(&command, 0, sizeof(command));
            command.type = BEHAVIOR_SCHEDULER_COMMAND_STATE_MOTION;
            command.motion = &input->state->motion[out_result->next_motion_index];
            if (!append_command(out_result, &command)) {
                return;
            }
            out_result->next_motion_index++;
        }
    }

    while (input->action != NULL && out_result->next_action_motion_index < input->action->motion_count &&
           (input->action->motion[out_result->next_action_motion_index].at_ms <= input->action_elapsed_ms ||
            action_motion_can_prequeue(input, out_result->next_action_motion_index))) {
        memset(&command, 0, sizeof(command));
        command.type = BEHAVIOR_SCHEDULER_COMMAND_ACTION_MOTION;
        command.motion = &input->action->motion[out_result->next_action_motion_index];
        if (!append_command(out_result, &command)) {
            return;
        }
        out_result->next_action_motion_index++;
    }

    while (out_result->next_expression_index < input->state->expression_count &&
           input->state->expression[out_result->next_expression_index].at_ms <= input->state_elapsed_ms) {
        memset(&command, 0, sizeof(command));
        command.type = BEHAVIOR_SCHEDULER_COMMAND_EXPRESSION;
        command.expression = &input->state->expression[out_result->next_expression_index];
        if (!append_command(out_result, &command)) {
            return;
        }
        out_result->next_expression_index++;
    }

    while (out_result->next_sound_index < input->state->sound_count &&
           input->state->sound[out_result->next_sound_index].at_ms <= input->state_elapsed_ms) {
        memset(&command, 0, sizeof(command));
        command.type = BEHAVIOR_SCHEDULER_COMMAND_SOUND;
        command.sound = &input->state->sound[out_result->next_sound_index];
        if (!append_command(out_result, &command)) {
            return;
        }
        out_result->next_sound_index++;
    }
}
