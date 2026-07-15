#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AGENT_ANIMATION_PHASE_IDLE = 0,
    AGENT_ANIMATION_PHASE_STANDBY_ENTRY,
    AGENT_ANIMATION_PHASE_STANDBY_LOOP_WAIT_COMMIT,
    AGENT_ANIMATION_PHASE_ASLEEP,
    AGENT_ANIMATION_PHASE_STANDBY_END,
    AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT,
    AGENT_ANIMATION_PHASE_LISTENING,
    AGENT_ANIMATION_PHASE_ERROR,
} agent_animation_phase_t;

typedef enum {
    AGENT_ANIMATION_CLIP_STANDBY_ENTRY = 0,
    AGENT_ANIMATION_CLIP_STANDBY_LOOP,
    AGENT_ANIMATION_CLIP_STANDBY_END,
    AGENT_ANIMATION_CLIP_LISTENING,
} agent_animation_clip_t;

typedef enum {
    AGENT_ANIMATION_EVENT_COMMITTED = 0,
    AGENT_ANIMATION_EVENT_COMPLETED,
    AGENT_ANIMATION_EVENT_FAILED,
} agent_animation_event_t;

typedef enum {
    AGENT_ANIMATION_ACTION_NONE = 0,
    AGENT_ANIMATION_ACTION_STANDBY_ENTRY,
    AGENT_ANIMATION_ACTION_STANDBY_LOOP,
    AGENT_ANIMATION_ACTION_SLEEPING,
    AGENT_ANIMATION_ACTION_STANDBY_END,
    AGENT_ANIMATION_ACTION_COMPLETE_WAKE,
    AGENT_ANIMATION_ACTION_ERROR,
} agent_animation_action_t;

typedef struct {
    agent_animation_phase_t phase;
    bool wake_pending;
} agent_animation_flow_core_t;

void agent_animation_flow_core_init(agent_animation_flow_core_t *flow);
void agent_animation_flow_core_close(agent_animation_flow_core_t *flow);
agent_animation_action_t agent_animation_flow_on_ready(agent_animation_flow_core_t *flow);
agent_animation_action_t agent_animation_flow_on_wake(agent_animation_flow_core_t *flow);
agent_animation_action_t agent_animation_flow_on_event(agent_animation_flow_core_t *flow, agent_animation_clip_t clip,
                                                       agent_animation_event_t event);
agent_animation_phase_t agent_animation_flow_phase(const agent_animation_flow_core_t *flow);
bool agent_animation_flow_wake_pending(const agent_animation_flow_core_t *flow);

#ifdef __cplusplus
}
#endif
