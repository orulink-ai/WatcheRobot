#ifndef BEHAVIOR_MEMORY_H
#define BEHAVIOR_MEMORY_H

#include <stddef.h>

void *behavior_persistent_calloc(size_t count, size_t size);

#ifdef BEHAVIOR_MEMORY_TESTING
void behavior_memory_test_reset(void);
size_t behavior_memory_test_persistent_allocations(void);
size_t behavior_memory_test_allocation_attempts(void);
void behavior_memory_test_fail_on_attempt(size_t attempt);
#endif

#endif /* BEHAVIOR_MEMORY_H */
