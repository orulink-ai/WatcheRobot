#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

#include <stddef.h>

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

static inline BaseType_t xTaskCreate(TaskFunction_t task_fn, const char *name, uint32_t stack_depth, void *params,
                                     UBaseType_t priority, TaskHandle_t *created_task) {
    (void)task_fn;
    (void)name;
    (void)stack_depth;
    (void)params;
    (void)priority;
    if (created_task != NULL) {
        *created_task = (TaskHandle_t)1;
    }
    return pdPASS;
}

static inline void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

static inline void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    (void)task;
    return 2048U;
}

#endif /* FREERTOS_TASK_H */
