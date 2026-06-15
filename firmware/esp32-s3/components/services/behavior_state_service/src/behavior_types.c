#include "behavior_types.h"

#include <stdlib.h>
#include <string.h>

void behavior_free_action_catalog(behavior_action_catalog_t *catalog) {
    int i;

    if (catalog == NULL) {
        return;
    }

    if (catalog->actions != NULL) {
        for (i = 0; i < catalog->action_count; ++i) {
            free(catalog->actions[i].motion);
        }
    }

    free(catalog->actions);
    memset(catalog, 0, sizeof(*catalog));
}

void behavior_free_catalog(behavior_catalog_t *catalog) {
    int i;

    if (catalog == NULL) {
        return;
    }

    if (catalog->states != NULL) {
        for (i = 0; i < catalog->state_count; ++i) {
            free(catalog->states[i].motion);
            free(catalog->states[i].expression);
            free(catalog->states[i].sound);
        }
    }

    free(catalog->states);
    memset(catalog, 0, sizeof(*catalog));
}
