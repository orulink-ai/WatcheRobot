#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void mem_monitor_init(void);
void mem_monitor_snapshot(const char *stage);
bool mem_monitor_check_integrity(const char *stage);

#ifdef __cplusplus
}
#endif
