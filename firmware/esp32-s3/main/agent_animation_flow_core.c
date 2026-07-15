#include "agent_animation_flow_core.h"

#include <stddef.h>

void agent_animation_flow_core_init(agent_animation_flow_core_t *flow) {
    if (flow == NULL) {
        return;
    }
    flow->phase = AGENT_ANIMATION_PHASE_IDLE;
    flow->wake_pending = false;
}

void agent_animation_flow_core_close(agent_animation_flow_core_t *flow) {
    agent_animation_flow_core_init(flow);
}

agent_animation_action_t agent_animation_flow_on_ready(agent_animation_flow_core_t *flow) {
    if (flow == NULL || flow->phase != AGENT_ANIMATION_PHASE_IDLE) {
        return AGENT_ANIMATION_ACTION_NONE;
    }
    flow->phase = AGENT_ANIMATION_PHASE_STANDBY_ENTRY;
    return AGENT_ANIMATION_ACTION_STANDBY_ENTRY;
}

agent_animation_action_t agent_animation_flow_on_wake(agent_animation_flow_core_t *flow) {
    if (flow == NULL) {
        return AGENT_ANIMATION_ACTION_NONE;
    }
    switch (flow->phase) {
    case AGENT_ANIMATION_PHASE_STANDBY_ENTRY:
    case AGENT_ANIMATION_PHASE_STANDBY_LOOP_WAIT_COMMIT:
        flow->wake_pending = true;
        return AGENT_ANIMATION_ACTION_NONE;
    case AGENT_ANIMATION_PHASE_ASLEEP:
        flow->wake_pending = false;
        flow->phase = AGENT_ANIMATION_PHASE_STANDBY_END;
        return AGENT_ANIMATION_ACTION_STANDBY_END;
    case AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT:
        return AGENT_ANIMATION_ACTION_NONE;
    case AGENT_ANIMATION_PHASE_LISTENING:
        return AGENT_ANIMATION_ACTION_COMPLETE_WAKE;
    default:
        return AGENT_ANIMATION_ACTION_NONE;
    }
}

agent_animation_action_t agent_animation_flow_on_event(agent_animation_flow_core_t *flow, agent_animation_clip_t clip,
                                                       agent_animation_event_t event) {
    if (flow == NULL) {
        return AGENT_ANIMATION_ACTION_NONE;
    }
    if (flow->phase == AGENT_ANIMATION_PHASE_STANDBY_ENTRY && clip == AGENT_ANIMATION_CLIP_STANDBY_ENTRY) {
        if (event == AGENT_ANIMATION_EVENT_COMPLETED) {
            flow->phase = AGENT_ANIMATION_PHASE_STANDBY_LOOP_WAIT_COMMIT;
            return AGENT_ANIMATION_ACTION_STANDBY_LOOP;
        }
        if (event == AGENT_ANIMATION_EVENT_FAILED) {
            flow->phase = AGENT_ANIMATION_PHASE_ERROR;
            flow->wake_pending = false;
            return AGENT_ANIMATION_ACTION_ERROR;
        }
    }
    if (flow->phase == AGENT_ANIMATION_PHASE_STANDBY_LOOP_WAIT_COMMIT && clip == AGENT_ANIMATION_CLIP_STANDBY_LOOP) {
        if (event == AGENT_ANIMATION_EVENT_COMMITTED) {
            if (flow->wake_pending) {
                flow->wake_pending = false;
                flow->phase = AGENT_ANIMATION_PHASE_STANDBY_END;
                return AGENT_ANIMATION_ACTION_STANDBY_END;
            }
            flow->phase = AGENT_ANIMATION_PHASE_ASLEEP;
            return AGENT_ANIMATION_ACTION_SLEEPING;
        }
        if (event == AGENT_ANIMATION_EVENT_FAILED) {
            flow->phase = AGENT_ANIMATION_PHASE_ERROR;
            flow->wake_pending = false;
            return AGENT_ANIMATION_ACTION_ERROR;
        }
    }
    if (flow->phase == AGENT_ANIMATION_PHASE_STANDBY_END && clip == AGENT_ANIMATION_CLIP_STANDBY_END) {
        if (event == AGENT_ANIMATION_EVENT_COMPLETED) {
            flow->phase = AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT;
            return AGENT_ANIMATION_ACTION_NONE;
        }
        if (event == AGENT_ANIMATION_EVENT_FAILED) {
            flow->phase = AGENT_ANIMATION_PHASE_ERROR;
            flow->wake_pending = false;
            return AGENT_ANIMATION_ACTION_ERROR;
        }
    }
    if (flow->phase == AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT && clip == AGENT_ANIMATION_CLIP_LISTENING) {
        if (event == AGENT_ANIMATION_EVENT_COMMITTED) {
            flow->phase = AGENT_ANIMATION_PHASE_LISTENING;
            return AGENT_ANIMATION_ACTION_COMPLETE_WAKE;
        }
        if (event == AGENT_ANIMATION_EVENT_FAILED) {
            flow->phase = AGENT_ANIMATION_PHASE_ERROR;
            flow->wake_pending = false;
            return AGENT_ANIMATION_ACTION_ERROR;
        }
    }
    return AGENT_ANIMATION_ACTION_NONE;
}

agent_animation_phase_t agent_animation_flow_phase(const agent_animation_flow_core_t *flow) {
    return flow != NULL ? flow->phase : AGENT_ANIMATION_PHASE_IDLE;
}

bool agent_animation_flow_wake_pending(const agent_animation_flow_core_t *flow) {
    return flow != NULL && flow->wake_pending;
}
