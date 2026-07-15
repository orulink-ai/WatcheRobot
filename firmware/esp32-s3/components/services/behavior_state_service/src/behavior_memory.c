#include "behavior_memory.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#ifdef BEHAVIOR_MEMORY_TESTING
static size_t s_persistent_allocations;
static size_t s_allocation_attempts;
static size_t s_fail_on_attempt;
#endif

void *behavior_persistent_calloc(size_t count, size_t size) {
    void *memory = NULL;

    if (count == 0 || size == 0 || count > SIZE_MAX / size) {
        return NULL;
    }
#ifdef BEHAVIOR_MEMORY_TESTING
    ++s_allocation_attempts;
    if (s_fail_on_attempt != 0 && s_allocation_attempts == s_fail_on_attempt) {
        return NULL;
    }
#endif
#ifdef ESP_PLATFORM
    memory = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    memory = calloc(count, size);
#endif
#ifdef BEHAVIOR_MEMORY_TESTING
    if (memory != NULL) {
        ++s_persistent_allocations;
    }
#endif
    return memory;
}

#ifdef BEHAVIOR_MEMORY_TESTING
void behavior_memory_test_reset(void) {
    s_persistent_allocations = 0;
    s_allocation_attempts = 0;
    s_fail_on_attempt = 0;
}

size_t behavior_memory_test_persistent_allocations(void) {
    return s_persistent_allocations;
}

size_t behavior_memory_test_allocation_attempts(void) {
    return s_allocation_attempts;
}

void behavior_memory_test_fail_on_attempt(size_t attempt) {
    s_fail_on_attempt = attempt;
}
#endif
