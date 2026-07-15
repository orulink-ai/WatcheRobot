#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

extern unsigned control_ingress_host_last_task_stack_depth;

static inline BaseType_t xTaskCreate(void (*task_fn)(void *), const char *name, unsigned stack_depth,
                                     void *arg, unsigned priority, TaskHandle_t *out_handle) {
    (void)task_fn;
    (void)name;
    control_ingress_host_last_task_stack_depth = stack_depth;
    (void)arg;
    (void)priority;
    if (out_handle != NULL) {
        *out_handle = (TaskHandle_t)1;
    }
    return pdPASS;
}

static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    (void)task;
    return 4096u;
}

#endif /* FREERTOS_TASK_H */
