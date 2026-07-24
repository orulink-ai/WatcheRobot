#ifndef BEHAVIOR_STATE_SERVICE_H
#define BEHAVIOR_STATE_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern char control_ingress_host_last_state_id[64];
extern char control_ingress_host_last_anim_id[64];
extern char control_ingress_host_last_action_id[64];
extern char control_ingress_host_last_sound_id[64];
extern bool control_ingress_host_uses_default_sound;

static inline bool behavior_state_host_has_state(const char *state_id) {
    static const char *const known_states[] = {
        "standby", "listening", "thinking", "processing", "speaking", "custom2", "custom3", "happy", "error",
    };
    size_t i;

    if (state_id == NULL) {
        return false;
    }
    for (i = 0; i < sizeof(known_states) / sizeof(known_states[0]); ++i) {
        if (strcmp(state_id, known_states[i]) == 0) {
            return true;
        }
    }
    return false;
}

static inline esp_err_t behavior_state_interrupt_action(const char *source) {
    (void)source;
    return ESP_ERR_NOT_FOUND;
}

static inline bool behavior_state_has_action(const char *action_id) {
    return behavior_state_host_has_state(action_id);
}

static inline esp_err_t behavior_state_set_with_resources_and_action(const char *state_id, const char *text,
                                                                     int font_size, const char *anim_id,
                                                                     const char *sound_id, const char *action_id) {
    if (!behavior_state_host_has_state(state_id)) {
        return ESP_ERR_NOT_FOUND;
    }
    (void)text;
    (void)font_size;
    snprintf(control_ingress_host_last_state_id, 64, "%s", state_id);
    snprintf(control_ingress_host_last_anim_id, 64, "%s", anim_id != NULL ? anim_id : "");
    snprintf(control_ingress_host_last_action_id, 64, "%s", action_id != NULL ? action_id : "");
    control_ingress_host_uses_default_sound = sound_id == NULL;
    snprintf(control_ingress_host_last_sound_id, 64, "%s", sound_id != NULL ? sound_id : "");
    return ESP_OK;
}

static inline esp_err_t behavior_state_set_text(const char *text, int font_size) {
    (void)text;
    (void)font_size;
    return ESP_OK;
}

static inline esp_err_t behavior_state_set(const char *state_id) {
    (void)state_id;
    return ESP_OK;
}

static inline esp_err_t behavior_state_set_with_resources(const char *state_id, const char *text, int font_size,
                                                          const char *anim_id, const char *sound_id) {
    (void)state_id;
    (void)text;
    (void)font_size;
    (void)anim_id;
    (void)sound_id;
    return ESP_OK;
}

#endif /* BEHAVIOR_STATE_SERVICE_H */
