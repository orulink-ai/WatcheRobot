#ifndef BEHAVIOR_CATALOG_PARSER_H
#define BEHAVIOR_CATALOG_PARSER_H

#include "behavior_types.h"
#include "esp_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t behavior_catalog_parse_json(const char *json, size_t json_len, behavior_anim_validator_t anim_validator,
                                      void *anim_validator_ctx, behavior_catalog_t *out_catalog);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_CATALOG_PARSER_H */
