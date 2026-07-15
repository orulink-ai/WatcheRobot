#include "anim_player_event_queue.h"

#include <string.h>

void anim_player_event_queue_init(anim_player_event_queue_t *queue) {
    if (queue == NULL) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->last_overflow_ticket = ANIMATION_TICKET_INVALID;
}

bool anim_player_event_queue_push_batch(anim_player_event_queue_t *queue, const anim_player_ticket_event_t *events,
                                        size_t event_count) {
    if (queue == NULL || (event_count > 0U && events == NULL)) {
        return false;
    }
    if (event_count == 0U) {
        return true;
    }
    if (queue->overflow_fault_pending && events[0].ticket == queue->overflow_fault.ticket) {
        queue->overflow_count++;
        queue->last_overflow_ticket = events[0].ticket;
        return false;
    }
    if (event_count > anim_player_event_queue_free(queue)) {
        queue->overflow_count++;
        queue->last_overflow_ticket = events[0].ticket;
        if (!queue->overflow_fault_pending) {
            queue->overflow_fault = events[0];
            queue->overflow_fault.type = ANIMATION_EVENT_FAILED;
            queue->overflow_fault.failure = ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL;
            queue->overflow_fault_pending = true;
        }
        return false;
    }

    for (size_t index = 0U; index < event_count; ++index) {
        size_t tail = (queue->head + queue->count) % EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY;
        queue->items[tail] = events[index];
        queue->count++;
    }
    return true;
}

bool anim_player_event_queue_pop(anim_player_event_queue_t *queue, anim_player_ticket_event_t *out_event) {
    if (queue == NULL || out_event == NULL) {
        return false;
    }

    if (queue->count == 0U) {
        if (!queue->overflow_fault_pending) {
            return false;
        }
        *out_event = queue->overflow_fault;
        memset(&queue->overflow_fault, 0, sizeof(queue->overflow_fault));
        queue->overflow_fault_pending = false;
        return true;
    }

    *out_event = queue->items[queue->head];
    memset(&queue->items[queue->head], 0, sizeof(queue->items[queue->head]));
    queue->head = (queue->head + 1U) % EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY;
    queue->count--;
    return true;
}

size_t anim_player_event_queue_count(const anim_player_event_queue_t *queue) {
    return queue == NULL ? 0U : queue->count;
}

size_t anim_player_event_queue_free(const anim_player_event_queue_t *queue) {
    return queue == NULL ? 0U : EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY - queue->count;
}

bool anim_player_event_queue_has_overflow_fault(const anim_player_event_queue_t *queue) {
    return queue != NULL && queue->overflow_fault_pending;
}
