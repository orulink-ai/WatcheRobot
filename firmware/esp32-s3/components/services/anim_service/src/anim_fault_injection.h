#ifndef ANIM_FAULT_INJECTION_H
#define ANIM_FAULT_INJECTION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ANIM_FAULT_SD_OPEN = 0,
    ANIM_FAULT_SD_HEADER_READ,
    ANIM_FAULT_SD_TOC_READ,
    ANIM_FAULT_SD_FRAME_READ,
    ANIM_FAULT_SD_SHORT_READ,
    ANIM_FAULT_PSRAM_FRAME_BUFFER_ALLOC,
    ANIM_FAULT_PSRAM_STREAM_SCRATCH_ALLOC,
    ANIM_FAULT_PSRAM_DIRECT_STRIP_ALLOC,
    ANIM_FAULT_RENDER,
    ANIM_FAULT_COUNT,
} anim_fault_point_t;

#ifdef ANIM_SERVICE_ENABLE_TEST_HOOKS
void anim_fault_injection_reset(void);
void anim_fault_injection_fail_on_call(anim_fault_point_t point, uint32_t call_number);
bool anim_fault_injection_should_fail(anim_fault_point_t point);
#else
static inline bool anim_fault_injection_should_fail(anim_fault_point_t point) {
    (void)point;
    return false;
}
#endif

#endif
