/**
 * @file animation_service.h
 * @brief Public, platform-independent animation request and event model.
 */

#ifndef ANIMATION_SERVICE_H
#define ANIMATION_SERVICE_H

#include "animation_registry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t animation_ticket_t;

#define ANIMATION_TICKET_INVALID ((animation_ticket_t)0U)

typedef enum {
    ANIM_PRIORITY_AMBIENT = 0,
    ANIM_PRIORITY_BEHAVIOR,
    ANIM_PRIORITY_INTERACTION,
    ANIM_PRIORITY_SYSTEM,
    ANIM_PRIORITY_COUNT,
} animation_priority_t;

typedef enum {
    ANIM_PREEMPTIBLE = 0,
    ANIM_PROTECTED_AFTER_COMMIT,
    ANIM_PREEMPT_POLICY_COUNT,
} animation_preempt_policy_t;

typedef enum {
    /** Legacy compatibility: repeat_count wins, otherwise use the resource manifest loop flag. */
    ANIM_PLAYBACK_RESOURCE_DEFAULT = 0,
    /** Play exactly one cycle and hold the final frame until replaced. */
    ANIM_PLAYBACK_ONCE,
    /** Play exactly repeat_count cycles; repeat_count must be greater than zero. */
    ANIM_PLAYBACK_REPEAT_COUNT,
    /** Ignore the resource loop flag and continue until cancelled or preempted. */
    ANIM_PLAYBACK_LOOP_UNTIL_REPLACED,
    ANIM_PLAYBACK_MODE_COUNT,
} animation_playback_mode_t;

typedef enum {
    ANIM_SOURCE_UNKNOWN = 0,
    ANIM_SOURCE_BEHAVIOR,
    ANIM_SOURCE_AGENT,
    ANIM_SOURCE_VOICE,
    ANIM_SOURCE_WEBSOCKET,
    ANIM_SOURCE_POWER,
    ANIM_SOURCE_BLUETOOTH,
    ANIM_SOURCE_APP,
    ANIM_SOURCE_SYSTEM,
    ANIM_SOURCE_COUNT,
} animation_source_t;

typedef enum {
    ANIMATION_EVENT_ACCEPTED = 0,
    ANIMATION_EVENT_PREPARING,
    ANIMATION_EVENT_COMMITTED,
    ANIMATION_EVENT_CYCLE_COMPLETED,
    ANIMATION_EVENT_COMPLETED,
    ANIMATION_EVENT_PREEMPTED,
    ANIMATION_EVENT_CANCELLED,
    ANIMATION_EVENT_FAILED,
} animation_event_type_t;

typedef enum {
    ANIMATION_FAILURE_NONE = 0,
    ANIMATION_FAILURE_INVALID_RESOURCE,
    ANIMATION_FAILURE_NO_SURFACE,
    ANIMATION_FAILURE_NO_MEMORY,
    ANIMATION_FAILURE_SD_OPEN_FAILED,
    ANIMATION_FAILURE_SD_READ_FAILED,
    ANIMATION_FAILURE_PACK_CORRUPT,
    ANIMATION_FAILURE_PREPARE_STALLED,
    ANIMATION_FAILURE_RENDER_FAILED,
    ANIMATION_FAILURE_SERVICE_STOPPED,
    ANIMATION_FAILURE_QUEUE_FULL,
    ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL,
} animation_failure_t;

typedef enum {
    ANIMATION_SERVICE_OK = 0,
    ANIMATION_SERVICE_INVALID_ARGUMENT = -1,
    ANIMATION_SERVICE_QUEUE_FULL = -2,
    ANIMATION_SERVICE_TICKET_TABLE_FULL = -3,
    ANIMATION_SERVICE_NOT_FOUND = -4,
    ANIMATION_SERVICE_INVALID_TRANSITION = -5,
    ANIMATION_SERVICE_NOT_INITIALIZED = -6,
    ANIMATION_SERVICE_NO_SURFACE = -7,
    ANIMATION_SERVICE_STOPPED = -8,
    ANIMATION_SERVICE_REENTRANT = -9,
    ANIMATION_SERVICE_PLAYER_ERROR = -10,
} animation_service_result_t;

typedef struct {
    emoji_anim_type_t type;
    animation_priority_t priority;
    animation_preempt_policy_t preempt_policy;
    animation_playback_mode_t playback_mode;
    uint16_t repeat_count;
    uint16_t fade_in_ms;
    animation_source_t source;
    uint32_t owner_epoch;
    uint32_t correlation_id;
} animation_request_t;

typedef struct {
    animation_ticket_t ticket;
    animation_event_type_t type;
    animation_failure_t failure;
    animation_request_t request;
    uint16_t completed_cycles;
} animation_event_t;

typedef struct {
    animation_ticket_t active_ticket;
    animation_ticket_t desired_ticket;
    animation_ticket_t preparing_ticket;
    animation_ticket_t visible_ticket;
    emoji_anim_type_t active_type;
    emoji_anim_type_t desired_type;
    emoji_anim_type_t preparing_type;
    emoji_anim_type_t visible_type;
    size_t queued_count;
    size_t live_ticket_count;
    uint32_t emitted_terminal_count;
    uint32_t accepted_count;
    uint32_t terminal_count;
    uint32_t completed_count;
    uint32_t preempted_count;
    uint32_t cancelled_count;
    uint32_t failed_count;
    uint32_t orphan_player_event_count;
    uint32_t wrong_transition_count;
    uint32_t player_event_overflow_count;
    size_t command_queue_depth_high_watermark;
    size_t player_event_queue_depth_high_watermark;
} animation_snapshot_t;

typedef void (*animation_event_sink_t)(const animation_event_t *event, void *context);

typedef enum {
    ANIMATION_SUSPEND_HOLD_LAST_FRAME = 0,
} animation_suspend_mode_t;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} animation_overlay_region_t;

animation_service_result_t animation_service_init(void);
animation_service_result_t animation_service_deinit(void);
animation_service_result_t animation_submit(const animation_request_t *request, animation_ticket_t *out_ticket);
animation_service_result_t animation_cancel(animation_ticket_t ticket);
animation_service_result_t animation_cancel_owner(animation_source_t source, uint32_t owner_epoch);
animation_service_result_t animation_prefetch_hint(emoji_anim_type_t type);
animation_service_result_t animation_get_snapshot(animation_snapshot_t *out_snapshot);
/**
 * Register the lifecycle event consumer. The sink runs on the Animation
 * Controller task and must only copy the event to another queue. Calling a
 * synchronous animation_service API from the sink returns REENTRANT.
 */
animation_service_result_t animation_set_event_sink(animation_event_sink_t sink, void *context);
/** Bind the LVGL image surface used by the private player. */
animation_service_result_t animation_service_bind_surface(void *surface);
/** Fail every live ticket with SERVICE_STOPPED, then synchronously detach the player surface. */
animation_service_result_t animation_service_unbind_surface(void);
/** Hold the last rendered frame and cancel all live tickets. */
animation_service_result_t animation_service_suspend(animation_suspend_mode_t mode);
/** Resume accepting requests; suspended requests are never automatically restored. */
animation_service_result_t animation_service_resume(void);
animation_service_result_t animation_set_overlay_region(const animation_overlay_region_t *region);
animation_service_result_t animation_clear_overlay_region(void);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_SERVICE_H */
