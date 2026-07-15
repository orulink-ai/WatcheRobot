#ifndef BEHAVIOR_EXECUTOR_H
#define BEHAVIOR_EXECUTOR_H

#include "animation_service.h"
#include "behavior_types.h"
#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pending;
    bool has_text;
    bool has_anim;
    char text[BEHAVIOR_TEXT_LEN];
    char anim[BEHAVIOR_STATE_ID_LEN];
    char state_id[BEHAVIOR_STATE_ID_LEN];
    int font_size;
    bool alert_text;
    animation_playback_mode_t playback_mode;
    uint16_t repeat_count;
    uint16_t fade_in_ms;
    uint32_t owner_epoch;
    uint32_t correlation_id;
} behavior_display_command_t;

typedef struct {
    bool animation_attempted;
    animation_service_result_t animation_result;
    animation_ticket_t animation_ticket;
    emoji_anim_type_t animation_type;
    uint32_t owner_epoch;
} behavior_display_execution_result_t;

void behavior_executor_clear_display_command(behavior_display_command_t *command);
void behavior_executor_apply_display(const behavior_display_command_t *command, const char *reason,
                                     behavior_display_execution_result_t *out_result);
esp_err_t behavior_executor_apply_text(const char *text, int font_size, bool alert_text);
esp_err_t behavior_executor_play_sound(const char *sound_id);
esp_err_t behavior_executor_apply_light(const behavior_light_event_t *event);
esp_err_t behavior_executor_move_motion(const behavior_motion_event_t *event);
esp_err_t behavior_executor_play_motion_sequence(const behavior_motion_event_t *events, int event_count);
esp_err_t behavior_executor_cancel_behavior_motion(void);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_EXECUTOR_H */
