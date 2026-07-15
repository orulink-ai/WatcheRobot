#ifndef BEHAVIOR_CATALOG_LOADER_H
#define BEHAVIOR_CATALOG_LOADER_H

#include "behavior_catalog_parser.h"

#include <stddef.h>

typedef struct {
    const char *path;
    const char *label;
} behavior_catalog_candidate_t;

esp_err_t behavior_catalog_load_first_valid(const behavior_catalog_candidate_t *candidates, size_t candidate_count,
                                             size_t max_bytes, behavior_anim_validator_t anim_validator,
                                             void *anim_validator_ctx, behavior_catalog_t *out_catalog,
                                             size_t *out_selected_index);

#endif /* BEHAVIOR_CATALOG_LOADER_H */
