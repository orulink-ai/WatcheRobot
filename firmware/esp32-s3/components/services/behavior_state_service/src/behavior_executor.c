#include "behavior_executor.h"

#include "display_ui.h"
#include "esp_log.h"
#include "hal_servo.h"
#include "sfx_service.h"

#include <string.h>

#define TAG "BEHAVIOR_EXECUTOR"

void behavior_executor_clear_display_command(behavior_display_command_t *command) {
    if (command == NULL) {
        return;
    }

    memset(command, 0, sizeof(*command));
}

void behavior_executor_apply_display(const behavior_display_command_t *command, const char *reason) {
    const char *text = NULL;
    const char *anim = NULL;
    const char *state_id = NULL;
    display_text_style_t text_style = DISPLAY_TEXT_STYLE_NORMAL;

    if (command == NULL || !command->pending) {
        return;
    }

    text = command->has_text ? command->text : NULL;
    anim = command->has_anim ? command->anim : NULL;
    state_id = command->state_id[0] != '\0' ? command->state_id : "<unset>";
    text_style = command->alert_text ? DISPLAY_TEXT_STYLE_ALERT : DISPLAY_TEXT_STYLE_NORMAL;
    if (display_update_with_style(text, anim, command->font_size, text_style, NULL) != 0) {
        ESP_LOGW(TAG, "Display update failed for state '%s' during %s", state_id,
                 reason != NULL ? reason : "behavior refresh");
    }
}

esp_err_t behavior_executor_apply_text(const char *text, int font_size, bool alert_text) {
    display_text_style_t text_style = alert_text ? DISPLAY_TEXT_STYLE_ALERT : DISPLAY_TEXT_STYLE_NORMAL;

    return display_update_with_style(text, NULL, font_size, text_style, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t behavior_executor_play_sound(const char *sound_id) {
    esp_err_t ret;

    if (sound_id == NULL || sound_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ret = sfx_service_play(sound_id);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Skip sound '%s': audio_busy_tts", sound_id);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        /* Missing optional local SFX should not drown out motion debugging. */
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to play sound '%s': %s", sound_id, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t behavior_executor_move_motion(const behavior_motion_event_t *event) {
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    /* Stress mode owns the motion lane. Keep consuming behavior timelines so
     * display/audio state stays alive, but never emit behavior servo traffic. */
    return ESP_OK;
#else
    hal_servo_motion_profile_t profile = event->motion_profile == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT
                                             ? HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT
                                             : HAL_SERVO_MOTION_PROFILE_LINEAR;
    esp_err_t ret = hal_servo_move_sync_with_profile_and_seq(event->x_deg, event->y_deg, event->duration_ms,
                                                             HAL_SERVO_MOTION_SOURCE_BEHAVIOR, profile, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Servo motion failed: x=%d y=%d duration=%d profile=%u", event->x_deg, event->y_deg,
                 event->duration_ms, (unsigned)event->motion_profile);
    }
    return ret;
#endif
}

esp_err_t behavior_executor_cancel_behavior_motion(void) {
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    return ESP_OK;
#else
    esp_err_t ret = hal_servo_cancel_all_with_source(HAL_SERVO_MOTION_SOURCE_BEHAVIOR);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to cancel behavior servo motions");
    }
    return ret;
#endif
}
