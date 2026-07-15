#ifndef FREERTOS_FREERTOS_H
#define FREERTOS_FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMUX_INITIALIZER_UNLOCKED 0

static inline void portENTER_CRITICAL(portMUX_TYPE *mux) {
    (void)mux;
}

static inline void portEXIT_CRITICAL(portMUX_TYPE *mux) {
    (void)mux;
}

#endif /* FREERTOS_FREERTOS_H */
