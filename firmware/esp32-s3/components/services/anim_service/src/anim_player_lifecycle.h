#ifndef ANIM_PLAYER_LIFECYCLE_H
#define ANIM_PLAYER_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ANIM_PLAYER_PHASE_IDLE = 0,
    ANIM_PLAYER_PHASE_PREPARING,
    ANIM_PLAYER_PHASE_PLAYING,
    ANIM_PLAYER_PHASE_COMPLETED,
    ANIM_PLAYER_PHASE_FAILED,
} anim_player_phase_t;

typedef enum {
    ANIM_PLAYER_EVENT_NONE = 0,
    ANIM_PLAYER_EVENT_PREPARING,
    ANIM_PLAYER_EVENT_COMMITTED,
    ANIM_PLAYER_EVENT_CYCLE_COMPLETED,
    ANIM_PLAYER_EVENT_COMPLETED,
    ANIM_PLAYER_EVENT_FAILED,
} anim_player_event_type_t;

typedef enum {
    ANIM_PLAYER_FAILURE_NONE = 0,
    ANIM_PLAYER_FAILURE_STREAM_OPEN,
    ANIM_PLAYER_FAILURE_FRAME_READ,
    ANIM_PLAYER_FAILURE_PREPARE_STALLED,
    ANIM_PLAYER_FAILURE_NO_MEMORY,
    ANIM_PLAYER_FAILURE_INVALID_RESOURCE,
    ANIM_PLAYER_FAILURE_PACK_CORRUPT,
    ANIM_PLAYER_FAILURE_RENDER,
    ANIM_PLAYER_FAILURE_SERVICE_STOPPED,
} anim_player_failure_t;

typedef struct {
    anim_player_event_type_t type;
    anim_player_failure_t failure;
    uint32_t generation;
    uint32_t ticket;
    int32_t animation_type;
    uint16_t completed_cycles;
} anim_player_lifecycle_event_t;

#define ANIM_PLAYER_LIFECYCLE_MAX_EVENTS 2U

typedef struct {
    anim_player_lifecycle_event_t items[ANIM_PLAYER_LIFECYCLE_MAX_EVENTS];
    size_t count;
} anim_player_lifecycle_events_t;

typedef struct {
    anim_player_phase_t phase;
    uint32_t preparing_generation;
    uint32_t visible_generation;
    uint32_t preparing_ticket;
    uint32_t visible_ticket;
    int32_t preparing_type;
    int32_t visible_type;
    uint32_t prepare_failure_count;
    uint32_t completed_cycles;
    bool worker_running;
    bool worker_stop_requested;
    anim_player_lifecycle_event_t last_event;
    uint32_t event_sequence;
} anim_player_lifecycle_snapshot_t;

typedef struct {
    anim_player_lifecycle_snapshot_t snapshot;
    uint32_t last_frame_index;
    bool has_last_frame;
    bool terminal_emitted;
} anim_player_lifecycle_t;

void anim_player_lifecycle_init(anim_player_lifecycle_t *lifecycle);
anim_player_lifecycle_events_t anim_player_lifecycle_begin_prepare(anim_player_lifecycle_t *lifecycle,
                                                                   uint32_t generation, uint32_t ticket,
                                                                   int32_t animation_type);
anim_player_lifecycle_events_t anim_player_lifecycle_commit(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                            uint32_t ticket, int32_t animation_type,
                                                            uint32_t frame_count, bool loop);
anim_player_lifecycle_events_t anim_player_lifecycle_note_frame(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                                uint32_t ticket, uint32_t frame_index,
                                                                uint32_t frame_count, bool loop);
anim_player_lifecycle_events_t anim_player_lifecycle_note_prepare_failure(anim_player_lifecycle_t *lifecycle,
                                                                          uint32_t generation, uint32_t ticket,
                                                                          anim_player_failure_t reason,
                                                                          uint32_t failure_limit);
anim_player_lifecycle_events_t anim_player_lifecycle_fail_visible(anim_player_lifecycle_t *lifecycle,
                                                                  uint32_t generation, uint32_t ticket,
                                                                  anim_player_failure_t reason);
void anim_player_lifecycle_note_prepare_progress(anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                 uint32_t ticket);
bool anim_player_lifecycle_should_retry_prepare(const anim_player_lifecycle_t *lifecycle, uint32_t generation,
                                                uint32_t ticket);
void anim_player_lifecycle_worker_started(anim_player_lifecycle_t *lifecycle);
void anim_player_lifecycle_request_worker_stop(anim_player_lifecycle_t *lifecycle);
void anim_player_lifecycle_worker_exited(anim_player_lifecycle_t *lifecycle);
bool anim_player_lifecycle_can_destroy_sync(const anim_player_lifecycle_t *lifecycle);
anim_player_lifecycle_snapshot_t anim_player_lifecycle_snapshot(const anim_player_lifecycle_t *lifecycle);

#endif
