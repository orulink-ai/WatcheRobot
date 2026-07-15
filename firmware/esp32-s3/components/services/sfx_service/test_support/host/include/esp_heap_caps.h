#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H

#include <stdbool.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT 0x02

#if defined(SFX_ENABLE_TEST_API)
extern bool heap_caps_host_fail_spiram_allocs;
extern int heap_caps_host_internal_alloc_count;

static inline void heap_caps_host_reset(void) {
    heap_caps_host_fail_spiram_allocs = false;
    heap_caps_host_internal_alloc_count = 0;
}
#endif

static inline void *heap_caps_malloc(size_t size, int caps) {
#if defined(SFX_ENABLE_TEST_API)
    if ((caps & MALLOC_CAP_SPIRAM) != 0 && heap_caps_host_fail_spiram_allocs) {
        return NULL;
    }
    if ((caps & MALLOC_CAP_SPIRAM) == 0) {
        heap_caps_host_internal_alloc_count++;
    }
#else
    (void)caps;
#endif
    return malloc(size);
}

static inline void heap_caps_free(void *ptr) {
    free(ptr);
}

#endif /* ESP_HEAP_CAPS_H */
