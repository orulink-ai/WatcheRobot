#ifndef FREERTOS_QUEUE_H
#define FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct {
    unsigned depth;
    unsigned count;
    size_t item_size;
} fake_queue_t;

typedef fake_queue_t *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(unsigned depth, size_t item_size) {
    fake_queue_t *queue = (fake_queue_t *)calloc(1u, sizeof(fake_queue_t));
    if (queue == NULL) {
        return NULL;
    }
    queue->depth = depth;
    queue->item_size = item_size;
    return queue;
}

static inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait) {
    (void)item;
    (void)ticks_to_wait;
    if (queue == NULL || queue->count >= queue->depth) {
        return pdFALSE;
    }
    queue->count++;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait) {
    (void)item;
    (void)ticks_to_wait;
    if (queue == NULL || queue->count == 0u) {
        return pdFALSE;
    }
    queue->count--;
    return pdTRUE;
}

static inline void vQueueDelete(QueueHandle_t queue) {
    free(queue);
}

#endif /* FREERTOS_QUEUE_H */
