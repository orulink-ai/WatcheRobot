#ifndef BEHAVIOR_EXECUTOR_H
#define BEHAVIOR_EXECUTOR_H

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
} behavior_display_command_t;

void behavior_executor_clear_display_command(behavior_display_command_t *command);
void behavior_executor_apply_display(const behavior_display_command_t *command, const char *reason);
esp_err_t behavior_executor_apply_text(const char *text, int font_size, bool alert_text);
esp_err_t behavior_executor_play_sound(const char *sound_id);
esp_err_t behavior_executor_move_motion(const behavior_motion_event_t *event);
esp_err_t behavior_executor_cancel_behavior_motion(void);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_EXECUTOR_H */
