#ifndef BEHAVIOR_ACTION_PARSER_H
#define BEHAVIOR_ACTION_PARSER_H

#include "behavior_types.h"
#include "esp_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t behavior_action_parse_json(const char *action_id, const char *json, size_t json_len,
                                     behavior_action_def_t *out_action);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_ACTION_PARSER_H */
