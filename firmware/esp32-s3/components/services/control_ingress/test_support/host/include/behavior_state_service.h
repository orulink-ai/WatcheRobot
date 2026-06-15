#ifndef BEHAVIOR_STATE_SERVICE_H
#define BEHAVIOR_STATE_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

static inline esp_err_t behavior_state_interrupt_action(const char *source) {
    (void)source;
    return ESP_ERR_NOT_FOUND;
}

static inline bool behavior_state_has_action(const char *action_id) {
    (void)action_id;
    return false;
}

static inline esp_err_t behavior_state_set_with_resources_and_action(const char *state_id, const char *text,
                                                                     int font_size, const char *anim_id,
                                                                     const char *sound_id, const char *action_id) {
    (void)state_id;
    (void)text;
    (void)font_size;
    (void)anim_id;
    (void)sound_id;
    (void)action_id;
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
