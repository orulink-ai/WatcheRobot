#include "app_center_firmware_sources.h"

#include <string.h>

bool app_center_firmware_sources_promote_preferred(app_center_firmware_source_t *sources,
                                                   size_t source_count,
                                                   int preferred_source_index) {
    if (sources == NULL || source_count < 2U || preferred_source_index < 0) {
        return false;
    }

    for (size_t index = 1U; index < source_count; ++index) {
        if (sources[index].source_index != preferred_source_index) {
            continue;
        }

        app_center_firmware_source_t preferred = sources[index];
        memmove(&sources[1], &sources[0], index * sizeof(sources[0]));
        sources[0] = preferred;
        return true;
    }

    return false;
}
