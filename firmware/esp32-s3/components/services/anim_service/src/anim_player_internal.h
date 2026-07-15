#ifndef ANIM_PLAYER_INTERNAL_H
#define ANIM_PLAYER_INTERNAL_H

#include "animation_service.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY 16U
#define EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL (-2)

typedef struct {
    animation_ticket_t ticket;
    animation_event_type_t type;
    animation_failure_t failure;
    emoji_anim_type_t animation_type;
    uint32_t generation;
    uint16_t completed_cycles;
} anim_player_ticket_event_t;

typedef void (*anim_player_ticket_event_sink_t)(const anim_player_ticket_event_t *event, void *context);

/* Controller/runtime-only API. The request is copied into playback state before this call returns. */
int emoji_anim_start_request_with_ticket(const animation_request_t *request, animation_ticket_t ticket);

/*
 * The sink is never invoked while the player mutex is held. It must remain
 * lightweight and copy the event into the controller/runtime queue.
 */
int emoji_anim_player_set_event_sink(anim_player_ticket_event_sink_t sink, void *context);
void emoji_anim_player_dispatch_events(void);
/* Controller-owned periodic watchdog tick; never advances normal playback. */
void emoji_anim_player_poll_prepare_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif
