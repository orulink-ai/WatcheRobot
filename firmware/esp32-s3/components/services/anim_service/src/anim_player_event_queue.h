#ifndef ANIM_PLAYER_EVENT_QUEUE_H
#define ANIM_PLAYER_EVENT_QUEUE_H

#include "anim_player_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    anim_player_ticket_event_t items[EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY];
    size_t head;
    size_t count;
    uint32_t overflow_count;
    animation_ticket_t last_overflow_ticket;
    bool overflow_fault_pending;
    anim_player_ticket_event_t overflow_fault;
} anim_player_event_queue_t;

void anim_player_event_queue_init(anim_player_event_queue_t *queue);
bool anim_player_event_queue_push_batch(anim_player_event_queue_t *queue, const anim_player_ticket_event_t *events,
                                        size_t event_count);
bool anim_player_event_queue_pop(anim_player_event_queue_t *queue, anim_player_ticket_event_t *out_event);
size_t anim_player_event_queue_count(const anim_player_event_queue_t *queue);
size_t anim_player_event_queue_free(const anim_player_event_queue_t *queue);
bool anim_player_event_queue_has_overflow_fault(const anim_player_event_queue_t *queue);

#endif
