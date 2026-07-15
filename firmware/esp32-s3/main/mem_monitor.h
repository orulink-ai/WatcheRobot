#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mem_monitor_context_cb_t)(char *app_buf, size_t app_buf_size, char *resource_buf,
                                         size_t resource_buf_size);

void mem_monitor_init(void);
void mem_monitor_set_context_callback(mem_monitor_context_cb_t callback);
void mem_monitor_snapshot(const char *stage);
void mem_monitor_dump_task_owners(const char *stage);
bool mem_monitor_check_integrity(const char *stage);

#ifdef __cplusplus
}
#endif
