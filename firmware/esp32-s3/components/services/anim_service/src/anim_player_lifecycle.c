#include "anim_player_lifecycle.h"

#include <string.h>

static anim_player_lifecycle_events_t no_events(void) {
    anim_player_lifecycle_events_t events;
    memset(&events, 0, sizeof(events));
    return events;
}

static void append_event(anim_player_lifecycle_t *lifecycle, anim_player_lifecycle_events_t *events,
                         anim_player_event_type_t type, anim_player_failure_t failure, uint32_t generation,
                         uint32_t ticket, int32_t animation_type) {
    if (lifecycle == NULL || events == NULL || events->count >= ANIM_PLAYER_LIFECYCLE_MAX_EVENTS) {
        return;
    }

    anim_player_lifecycle_event_t event = {
        .type = type,
        .failure = failure,
        .generation = generation,
        .ticket = ticket,
        .animation_type = animation_type,
        .completed_cycles = (uint16_t)lifecycle->snapshot.completed_cycles,
    };
    events->items[events->count++] = event;
    lifecycle->snapshot.last_event = event;
    lifecycle->snapshot.event_sequence++;
}

void anim_player_lifecycle_init(anim_player_lifecycle_t *lifecycle) {
    if (lifecycle == NULL) {
        return;
    }

    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_IDLE;
    lifecycle->snapshot.preparing_type = -1;
    lifecycle->snapshot.visible_type = -1;
    lifecycle->snapshot.last_event.animation_type = -1;
}

anim_player_lifecycle_events_t anim_player_lifecycle_begin_prepare(anim_player_lifecycle_t *lifecycle,
                                                                   uint32_t generation, uint32_t ticket,
                                                                   int32_t animation_type) {
    anim_player_lifecycle_events_t events = no_events();
    if (lifecycle == NULL || generation == 0U) {
        return events;
    }

    lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_PREPARING;
    lifecycle->snapshot.preparing_generation = generation;
    lifecycle->snapshot.preparing_ticket = ticket;
    lifecycle->snapshot.preparing_type = animation_type;
    lifecycle->snapshot.prepare_failure_count = 0U;
    lifecycle->terminal_emitted = false;
    lifecycle->has_last_frame = false;
    append_event(lifecycle, &events, ANIM_PLAYER_EVENT_PREPARING, ANIM_PLAYER_FAILURE_NONE, generation, ticket,
                 animation_type);
    return events;
}

anim_player_lifecycle_events_t anim_player_lifecycle_commit(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                            uint32_t ticket, int32_t animation_type,
                                                            uint32_t frame_count, bool loop) {
    anim_player_lifecycle_events_t events = no_events();
    if (lifecycle == NULL || lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_PREPARING ||
        lifecycle->snapshot.preparing_generation != generation || lifecycle->snapshot.preparing_ticket != ticket ||
        lifecycle->snapshot.preparing_type != animation_type || frame_count == 0U) {
        return events;
    }

    lifecycle->snapshot.visible_generation = generation;
    lifecycle->snapshot.visible_ticket = ticket;
    lifecycle->snapshot.visible_type = animation_type;
    lifecycle->snapshot.preparing_generation = 0U;
    lifecycle->snapshot.preparing_ticket = 0U;
    lifecycle->snapshot.preparing_type = -1;
    lifecycle->snapshot.prepare_failure_count = 0U;
    lifecycle->snapshot.completed_cycles = 0U;
    lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_PLAYING;
    lifecycle->last_frame_index = 0U;
    lifecycle->has_last_frame = true;
    append_event(lifecycle, &events, ANIM_PLAYER_EVENT_COMMITTED, ANIM_PLAYER_FAILURE_NONE, generation, ticket,
                 animation_type);

    if (frame_count == 1U) {
        if (loop) {
            lifecycle->snapshot.completed_cycles++;
            append_event(lifecycle, &events, ANIM_PLAYER_EVENT_CYCLE_COMPLETED, ANIM_PLAYER_FAILURE_NONE, generation,
                         ticket, animation_type);
        } else {
            lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_COMPLETED;
            lifecycle->terminal_emitted = true;
            append_event(lifecycle, &events, ANIM_PLAYER_EVENT_COMPLETED, ANIM_PLAYER_FAILURE_NONE, generation, ticket,
                         animation_type);
        }
    }
    return events;
}

anim_player_lifecycle_events_t anim_player_lifecycle_note_frame(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                                uint32_t ticket, uint32_t frame_index,
                                                                uint32_t frame_count, bool loop) {
    anim_player_lifecycle_events_t events = no_events();
    if (lifecycle == NULL || lifecycle->snapshot.visible_generation != generation ||
        lifecycle->snapshot.visible_ticket != ticket || frame_count == 0U ||
        (lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_PLAYING &&
         lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_COMPLETED)) {
        return events;
    }

    lifecycle->last_frame_index = frame_index;
    lifecycle->has_last_frame = true;

    if (loop && frame_index == frame_count - 1U) {
        lifecycle->snapshot.completed_cycles++;
        append_event(lifecycle, &events, ANIM_PLAYER_EVENT_CYCLE_COMPLETED, ANIM_PLAYER_FAILURE_NONE, generation,
                     ticket, lifecycle->snapshot.visible_type);
    } else if (!loop && frame_index == frame_count - 1U && !lifecycle->terminal_emitted) {
        lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_COMPLETED;
        lifecycle->terminal_emitted = true;
        append_event(lifecycle, &events, ANIM_PLAYER_EVENT_COMPLETED, ANIM_PLAYER_FAILURE_NONE, generation, ticket,
                     lifecycle->snapshot.visible_type);
    }
    return events;
}

anim_player_lifecycle_events_t anim_player_lifecycle_note_prepare_failure(anim_player_lifecycle_t *lifecycle,
                                                                          uint32_t generation, uint32_t ticket,
                                                                          anim_player_failure_t reason,
                                                                          uint32_t failure_limit) {
    anim_player_lifecycle_events_t events = no_events();
    if (lifecycle == NULL || lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_PREPARING ||
        lifecycle->snapshot.preparing_generation != generation || lifecycle->snapshot.preparing_ticket != ticket ||
        lifecycle->terminal_emitted) {
        return events;
    }

    lifecycle->snapshot.prepare_failure_count++;
    if (failure_limit == 0U || lifecycle->snapshot.prepare_failure_count >= failure_limit) {
        int32_t failed_type = lifecycle->snapshot.preparing_type;
        lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_FAILED;
        lifecycle->snapshot.preparing_generation = 0U;
        lifecycle->snapshot.preparing_ticket = 0U;
        lifecycle->snapshot.preparing_type = -1;
        lifecycle->terminal_emitted = true;
        append_event(lifecycle, &events, ANIM_PLAYER_EVENT_FAILED, reason, generation, ticket, failed_type);
    }
    return events;
}

anim_player_lifecycle_events_t anim_player_lifecycle_fail_visible(anim_player_lifecycle_t *lifecycle,
                                                                  uint32_t generation, uint32_t ticket,
                                                                  anim_player_failure_t reason) {
    anim_player_lifecycle_events_t events = no_events();
    if (lifecycle == NULL || lifecycle->snapshot.visible_generation != generation ||
        lifecycle->snapshot.visible_ticket != ticket || lifecycle->terminal_emitted ||
        lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_PLAYING) {
        return events;
    }

    lifecycle->snapshot.phase = ANIM_PLAYER_PHASE_FAILED;
    lifecycle->terminal_emitted = true;
    append_event(lifecycle, &events, ANIM_PLAYER_EVENT_FAILED, reason, generation, ticket,
                 lifecycle->snapshot.visible_type);
    return events;
}

void anim_player_lifecycle_note_prepare_progress(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                 uint32_t ticket) {
    if (lifecycle == NULL || lifecycle->snapshot.phase != ANIM_PLAYER_PHASE_PREPARING ||
        lifecycle->snapshot.preparing_generation != generation || lifecycle->snapshot.preparing_ticket != ticket) {
        return;
    }
    lifecycle->snapshot.prepare_failure_count = 0U;
}

bool anim_player_lifecycle_should_retry_prepare(const anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                uint32_t ticket) {
    return lifecycle != NULL && lifecycle->snapshot.phase == ANIM_PLAYER_PHASE_PREPARING &&
           lifecycle->snapshot.preparing_generation == generation && lifecycle->snapshot.preparing_ticket == ticket &&
           !lifecycle->terminal_emitted;
}

void anim_player_lifecycle_worker_started(anim_player_lifecycle_t *lifecycle) {
    if (lifecycle == NULL) {
        return;
    }
    lifecycle->snapshot.worker_running = true;
    lifecycle->snapshot.worker_stop_requested = false;
}

void anim_player_lifecycle_request_worker_stop(anim_player_lifecycle_t *lifecycle) {
    if (lifecycle == NULL || !lifecycle->snapshot.worker_running) {
        return;
    }
    lifecycle->snapshot.worker_stop_requested = true;
}

void anim_player_lifecycle_worker_exited(anim_player_lifecycle_t *lifecycle) {
    if (lifecycle == NULL) {
        return;
    }
    lifecycle->snapshot.worker_running = false;
    lifecycle->snapshot.worker_stop_requested = false;
}

bool anim_player_lifecycle_can_destroy_sync(const anim_player_lifecycle_t *lifecycle) {
    return lifecycle == NULL || !lifecycle->snapshot.worker_running;
}

anim_player_lifecycle_snapshot_t anim_player_lifecycle_snapshot(const anim_player_lifecycle_t *lifecycle) {
    anim_player_lifecycle_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.preparing_type = -1;
    snapshot.visible_type = -1;
    snapshot.last_event.animation_type = -1;
    if (lifecycle != NULL) {
        snapshot = lifecycle->snapshot;
    }
    return snapshot;
}
