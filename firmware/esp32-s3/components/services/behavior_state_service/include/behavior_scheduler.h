#ifndef BEHAVIOR_SCHEDULER_H
#define BEHAVIOR_SCHEDULER_H

#include "behavior_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const behavior_state_def_t *state;
    const behavior_action_def_t *action;
    int next_motion_index;
    int next_expression_index;
    int next_sound_index;
    int next_light_index;
    int next_action_motion_index;
} behavior_scheduler_snapshot_t;

#define BEHAVIOR_SCHEDULER_COMMAND_CAPACITY 8U

typedef enum {
    BEHAVIOR_SCHEDULER_COMMAND_NONE = 0,
    BEHAVIOR_SCHEDULER_COMMAND_STATE_MOTION,
    BEHAVIOR_SCHEDULER_COMMAND_ACTION_MOTION,
    BEHAVIOR_SCHEDULER_COMMAND_EXPRESSION,
    BEHAVIOR_SCHEDULER_COMMAND_SOUND,
    BEHAVIOR_SCHEDULER_COMMAND_LIGHT,
} behavior_scheduler_command_type_t;

typedef struct {
    behavior_scheduler_command_type_t type;
    const behavior_motion_event_t *motion;
    const behavior_expression_event_t *expression;
    const behavior_sound_event_t *sound;
    const behavior_light_event_t *light;
} behavior_scheduler_command_t;

typedef struct {
    const behavior_state_def_t *state;
    const behavior_action_def_t *action;
    uint32_t state_elapsed_ms;
    uint32_t action_elapsed_ms;
    int next_motion_index;
    int next_action_motion_index;
    int next_expression_index;
    int next_sound_index;
    int next_light_index;
} behavior_scheduler_tick_input_t;

typedef struct {
    int next_motion_index;
    int next_action_motion_index;
    int next_expression_index;
    int next_sound_index;
    int next_light_index;
    behavior_scheduler_command_t commands[BEHAVIOR_SCHEDULER_COMMAND_CAPACITY];
    size_t command_count;
    bool has_more_due_events;
} behavior_scheduler_tick_result_t;

bool behavior_scheduler_state_motion_overridden(const behavior_action_def_t *action);
bool behavior_scheduler_action_should_loop(const behavior_state_def_t *state, const behavior_action_def_t *action,
                                           bool loop_enabled);
animation_playback_mode_t behavior_scheduler_effective_animation_playback(animation_playback_mode_t state_mode,
                                                                          bool resources_one_shot);
bool behavior_scheduler_all_state_events_dispatched(const behavior_scheduler_snapshot_t *snapshot);
bool behavior_scheduler_all_action_events_dispatched(const behavior_action_def_t *action, int next_action_motion_index);
uint32_t behavior_scheduler_non_loop_done_at_ms(const behavior_state_def_t *state, const behavior_action_def_t *action,
                                                uint32_t default_hold_ms);
const char *behavior_scheduler_animation_transition_target(const behavior_state_def_t *state,
                                                           const char *completed_animation);
bool behavior_scheduler_should_defer_animation_transition_target(const behavior_state_def_t *state,
                                                                 const char *requested_state,
                                                                 const char *completed_animation);
void behavior_scheduler_collect_due_events(const behavior_scheduler_tick_input_t *input,
                                           behavior_scheduler_tick_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_SCHEDULER_H */
