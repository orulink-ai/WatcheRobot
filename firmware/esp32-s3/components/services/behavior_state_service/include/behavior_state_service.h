#ifndef BEHAVIOR_STATE_SERVICE_H
#define BEHAVIOR_STATE_SERVICE_H

#include "animation_service.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char state_id[32];
    animation_event_t event;
} behavior_animation_event_t;

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
/** Play the requested animation and body action once even when the owning state loops. */
esp_err_t behavior_state_set_with_resources_and_action_once(const char *state_id, const char *text, int font_size,
                                                            const char *anim_id, const char *sound_id,
                                                            const char *action_id);
esp_err_t behavior_state_set_text(const char *text, int font_size);
esp_err_t behavior_state_set_text_style(const char *text, int font_size, bool alert_text);
/**
 * Re-submit the current state's effective animation after an explicit player resume.
 * Normal same-state updates remain deduplicated and do not restart the animation.
 */
esp_err_t behavior_state_refresh_animation(void);
/** Cancel the active behavior and restore the catalog default state. */
esp_err_t behavior_state_cancel(void);
const char *behavior_state_get_current(void);
bool behavior_state_is_busy(void);
bool behavior_state_has_action(const char *action_id);
bool behavior_state_has_state(const char *state_id);
bool behavior_state_is_action_active(void);
size_t behavior_state_stack_high_watermark(void);
size_t behavior_state_stack_size(void);
/**
 * Read the next identity-validated animation event emitted for the current
 * Behavior-owned Ticket. The event is copied from a fixed queue and is safe to
 * consume from the application task.
 */
bool behavior_state_poll_animation_event(behavior_animation_event_t *event_out);
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
