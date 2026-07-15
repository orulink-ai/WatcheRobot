#ifndef APP_CENTER_FIRMWARE_SOURCES_H
#define APP_CENTER_FIRMWARE_SOURCES_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *url;
    int source_index;
} app_center_firmware_source_t;

bool app_center_firmware_sources_promote_preferred(app_center_firmware_source_t *sources,
                                                   size_t source_count,
                                                   int preferred_source_index);

#endif
