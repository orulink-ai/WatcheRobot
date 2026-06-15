#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

static inline BaseType_t xTaskCreate(void (*task_fn)(void *), const char *name, unsigned stack_depth,
                                     void *arg, unsigned priority, TaskHandle_t *out_handle) {
    (void)task_fn;
    (void)name;
    (void)stack_depth;
    (void)arg;
    (void)priority;
    if (out_handle != NULL) {
        *out_handle = (TaskHandle_t)1;
    }
    return pdPASS;
}

#endif /* FREERTOS_TASK_H */
