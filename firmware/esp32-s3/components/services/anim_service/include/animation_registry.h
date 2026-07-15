/**
 * @file animation_registry.h
 * @brief Generated, stable animation identifiers and lookup helpers.
 */

#ifndef ANIMATION_REGISTRY_H
#define ANIMATION_REGISTRY_H

#include "animation_registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

const animation_registry_entry_t *animation_registry_get(emoji_anim_type_t type);
const char *animation_registry_name(emoji_anim_type_t type);
bool animation_registry_type_from_name(const char *name, emoji_anim_type_t *out_type);
bool animation_registry_default_loop(emoji_anim_type_t type);
animation_encoding_t animation_registry_encoding(emoji_anim_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_REGISTRY_H */
