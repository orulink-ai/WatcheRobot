#ifndef FREERTOS_SEMPHR_H
#define FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"

#include <stdlib.h>

typedef struct {
    int unused;
} host_semaphore_t;

typedef host_semaphore_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)calloc(1U, sizeof(host_semaphore_t));
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait) {
    (void)semaphore;
    (void)ticks_to_wait;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    free(semaphore);
}

#endif /* FREERTOS_SEMPHR_H */
