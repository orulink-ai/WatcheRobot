#include "behavior_executor.h"

#include "behavior_animation_policy.h"
#include "behavior_motion_sequence.h"
#include "display_ui.h"
#include "esp_log.h"
#include "hal_servo.h"
#include "mcu_led_service.h"
#include "sfx_service.h"

#include <string.h>

#define TAG "BEHAVIOR_EXECUTOR"

void behavior_executor_clear_display_command(behavior_display_command_t *command) {
    if (command == NULL) {
        return;
    }

    memset(command, 0, sizeof(*command));
}

void behavior_executor_apply_display(const behavior_display_command_t *command, const char *reason,
                                     behavior_display_execution_result_t *out_result) {
    const char *text = NULL;
    const char *anim = NULL;
    const char *state_id = NULL;
    display_text_style_t text_style = DISPLAY_TEXT_STYLE_NORMAL;
    emoji_anim_type_t animation_type = EMOJI_ANIM_NONE;
    animation_service_result_t animation_result = ANIMATION_SERVICE_OK;
    animation_ticket_t animation_ticket = ANIMATION_TICKET_INVALID;
    behavior_animation_policy_t animation_policy;

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
        out_result->animation_result = ANIMATION_SERVICE_OK;
        out_result->animation_ticket = ANIMATION_TICKET_INVALID;
        out_result->animation_type = EMOJI_ANIM_NONE;
    }

    if (command == NULL || !command->pending) {
        return;
    }

    text = command->has_text ? command->text : NULL;
    anim = command->has_anim ? command->anim : NULL;
    state_id = command->state_id[0] != '\0' ? command->state_id : "<unset>";
    text_style = command->alert_text ? DISPLAY_TEXT_STYLE_ALERT : DISPLAY_TEXT_STYLE_NORMAL;

    if (text != NULL && display_update_with_style(text, command->font_size, text_style, NULL) != 0) {
        ESP_LOGW(TAG, "Display text update failed for state '%s' during %s", state_id,
                 reason != NULL ? reason : "behavior refresh");
    }

    if (anim == NULL) {
        return;
    }
    if (out_result != NULL) {
        out_result->animation_attempted = true;
        out_result->owner_epoch = command->owner_epoch;
    }
    if (!animation_registry_type_from_name(anim, &animation_type)) {
        animation_result = ANIMATION_SERVICE_INVALID_ARGUMENT;
    } else {
        animation_request_t request;
        memset(&request, 0, sizeof(request));
        animation_policy = behavior_animation_policy_resolve(state_id, animation_type);
        request.type = animation_type;
        request.priority = animation_policy.priority;
        request.preempt_policy = animation_policy.preempt_policy;
        request.playback_mode = command->playback_mode;
        request.repeat_count = command->repeat_count;
        request.fade_in_ms = command->fade_in_ms;
        request.source = ANIM_SOURCE_BEHAVIOR;
        request.owner_epoch = command->owner_epoch;
        request.correlation_id = command->correlation_id;
        animation_result = animation_submit(&request, &animation_ticket);
    }
    if (out_result != NULL) {
        out_result->animation_result = animation_result;
        out_result->animation_ticket = animation_ticket;
        out_result->animation_type = animation_type;
    }
    if (animation_result != ANIMATION_SERVICE_OK) {
        ESP_LOGW(TAG, "Animation submit failed state='%s' anim='%s' owner=%lu result=%d", state_id, anim,
                 (unsigned long)command->owner_epoch, (int)animation_result);
    }
}

esp_err_t behavior_executor_apply_text(const char *text, int font_size, bool alert_text) {
    display_text_style_t text_style = alert_text ? DISPLAY_TEXT_STYLE_ALERT : DISPLAY_TEXT_STYLE_NORMAL;

    return display_update_with_style(text, font_size, text_style, NULL) == 0 ? ESP_OK : ESP_FAIL;
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

esp_err_t behavior_executor_apply_light(const behavior_light_event_t *event) {
    mcu_led_request_t request = {0};

    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    request.mode = MCU_LED_MODE_STATIC;
    if (strcmp(event->effect, "off") == 0) {
        request.mode = MCU_LED_MODE_OFF;
    } else if (event->effect[0] != '\0') {
        request.mode = MCU_LED_MODE_EFFECT;
        if (strcmp(event->effect, "blink") == 0) {
            request.effect_id = MCU_LED_EFFECT_BLINK;
        } else if (strcmp(event->effect, "breathing") == 0) {
            request.effect_id = MCU_LED_EFFECT_BREATHING;
        } else if (strcmp(event->effect, "rainbow") == 0) {
            request.effect_id = MCU_LED_EFFECT_RAINBOW;
        } else if (strcmp(event->effect, "status_pulse") == 0) {
            request.effect_id = MCU_LED_EFFECT_STATUS_PULSE;
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (strcmp(event->zone, "side") == 0) {
        request.zone = MCU_LED_ZONE_SIDE;
    } else if (strcmp(event->zone, "bottom") == 0) {
        request.zone = MCU_LED_ZONE_BOTTOM;
    } else {
        request.zone = MCU_LED_ZONE_ALL;
    }
    request.primary_red = event->red;
    request.primary_green = event->green;
    request.primary_blue = event->blue;
    request.brightness = (uint8_t)(((uint16_t)event->brightness * UINT8_MAX + 50U) / 100U);
    request.period_ms = event->period_ms;
    request.repeat_count = event->repeat_count;
    return mcu_led_submit(&request);
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

esp_err_t behavior_executor_play_motion_sequence(const behavior_motion_event_t *events, int event_count) {
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    (void)events;
    (void)event_count;
    return ESP_OK;
#else
    esp_err_t ret = behavior_motion_sequence_play(events, event_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Behavior motion sequence failed: event_count=%d err=%s", event_count, esp_err_to_name(ret));
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
