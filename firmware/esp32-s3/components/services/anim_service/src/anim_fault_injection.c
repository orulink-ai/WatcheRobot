#include "anim_fault_injection.h"

#ifdef ANIM_SERVICE_ENABLE_TEST_HOOKS
#include <string.h>

static uint32_t g_fail_on_call[ANIM_FAULT_COUNT];
static uint32_t g_call_count[ANIM_FAULT_COUNT];

void anim_fault_injection_reset(void) {
    memset(g_fail_on_call, 0, sizeof(g_fail_on_call));
    memset(g_call_count, 0, sizeof(g_call_count));
}

void anim_fault_injection_fail_on_call(anim_fault_point_t point, uint32_t call_number) {
    if (point < ANIM_FAULT_COUNT) {
        g_fail_on_call[point] = call_number;
        g_call_count[point] = 0U;
    }
}

bool anim_fault_injection_should_fail(anim_fault_point_t point) {
    if (point >= ANIM_FAULT_COUNT) {
        return false;
    }
    g_call_count[point]++;
    return g_fail_on_call[point] != 0U && g_call_count[point] == g_fail_on_call[point];
}
#else
typedef int anim_fault_injection_disabled_translation_unit_t;
#endif
