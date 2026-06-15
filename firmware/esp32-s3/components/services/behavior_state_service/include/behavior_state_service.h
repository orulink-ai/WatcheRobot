#ifndef BEHAVIOR_STATE_SERVICE_H
#define BEHAVIOR_STATE_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t behavior_state_init(void);
esp_err_t behavior_state_load(void);
esp_err_t behavior_state_set(const char *state_id);
esp_err_t behavior_state_set_with_text(const char *state_id, const char *text, int font_size);
esp_err_t behavior_state_set_with_text_style(const char *state_id, const char *text, int font_size, bool alert_text);
/* Resource setter override semantics:
 * anim_id: NULL uses the state's default animation; "" explicitly keeps the current animation/text-only display.
 * sound_id: NULL uses the state's default sound timeline; "" explicitly suppresses local SFX for this request. */
esp_err_t behavior_state_set_with_resources(const char *state_id, const char *text, int font_size, const char *anim_id,
                                            const char *sound_id);
esp_err_t behavior_state_set_with_resources_and_action(const char *state_id, const char *text, int font_size,
                                                       const char *anim_id, const char *sound_id,
                                                       const char *action_id);
esp_err_t behavior_state_set_text(const char *text, int font_size);
esp_err_t behavior_state_set_text_style(const char *text, int font_size, bool alert_text);
const char *behavior_state_get_current(void);
bool behavior_state_is_busy(void);
bool behavior_state_has_action(const char *action_id);
bool behavior_state_is_action_active(void);
/**
 * @brief Interrupt the currently active action motion track.
 *
 * Stops the behavior-layer action loop and asks the servo HAL to discard any
 * queued motion segments that still belong to the interrupted action. The
 * state itself can remain active; this only targets action motion playback.
 *
 * @param source Optional short source string for diagnostics/logging
 * @return ESP_OK if an action was interrupted, ESP_ERR_NOT_FOUND if idle
 */
esp_err_t behavior_state_interrupt_action(const char *source);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_STATE_SERVICE_H */
