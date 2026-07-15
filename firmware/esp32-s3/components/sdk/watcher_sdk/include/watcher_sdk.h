#ifndef WATCHER_SDK_H
#define WATCHER_SDK_H

#include "watcher_sdk_core.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHER_SDK_PROTOCOL_VERSION "1.0"
#define WATCHER_SDK_APP_ID_MAX 32U

typedef uint32_t watcher_sdk_session_id_t;

#define WATCHER_SDK_SESSION_INVALID ((watcher_sdk_session_id_t)0U)

typedef struct watcher_sdk_context watcher_sdk_context_t;

typedef struct {
    const char *app_id;
} watcher_sdk_config_t;

typedef struct {
    int pan_deg;
    int tilt_deg;
    uint32_t duration_ms;
    bool ease_in_out;
} watcher_motion_target_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness_percent;
    const char *zone;
} watcher_light_color_t;

typedef struct {
    const char *effect;
    watcher_light_color_t color;
    uint16_t period_ms;
    uint16_t repeat_count;
} watcher_light_effect_options_t;

typedef void (*watcher_microphone_frame_cb_t)(const uint8_t *pcm, size_t size, uint32_t timestamp_ms,
                                              void *user_context);
typedef void (*watcher_microphone_end_cb_t)(void *user_context);

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t sample_width_bytes;
    watcher_microphone_frame_cb_t on_frame;
    watcher_microphone_end_cb_t on_end;
    void *user_context;
} watcher_microphone_config_t;

typedef void (*watcher_camera_image_cb_t)(const uint8_t *jpeg, size_t size, uint32_t timestamp_ms,
                                          void *user_context);

typedef struct {
    int width;
    int height;
    int quality;
    watcher_camera_image_cb_t on_image;
    void *user_context;
} watcher_camera_capture_config_t;

watcher_sdk_result_t watcher_sdk_open(const watcher_sdk_config_t *config, watcher_sdk_context_t **out_context);
void watcher_sdk_close(watcher_sdk_context_t *context);
void watcher_sdk_tick(watcher_sdk_context_t *context);
bool watcher_sdk_poll_event(watcher_sdk_context_t *context, watcher_sdk_event_t *out_event);
watcher_sdk_job_state_t watcher_job_get_state(watcher_sdk_context_t *context, watcher_sdk_job_id_t job_id);
watcher_sdk_result_t watcher_job_cancel(watcher_sdk_context_t *context, watcher_sdk_job_id_t job_id);

watcher_sdk_result_t watcher_behavior_play(watcher_sdk_context_t *context, const char *behavior_id,
                                           uint16_t repeat_count, watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_behavior_stop(watcher_sdk_context_t *context);

watcher_sdk_result_t watcher_animation_play(watcher_sdk_context_t *context, const char *animation_id,
                                            watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_animation_stop(watcher_sdk_context_t *context);

watcher_sdk_result_t watcher_motion_move_to(watcher_sdk_context_t *context, const watcher_motion_target_t *target,
                                            watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_motion_set_target(watcher_sdk_context_t *context, bool has_pan, int pan_deg,
                                               bool has_tilt, int tilt_deg);
watcher_sdk_result_t watcher_motion_play_action(watcher_sdk_context_t *context, const char *action_id,
                                                watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_motion_stop(watcher_sdk_context_t *context);

watcher_sdk_result_t watcher_audio_play(watcher_sdk_context_t *context, const char *sound_id,
                                        watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_audio_stop(watcher_sdk_context_t *context);

watcher_sdk_result_t watcher_light_set_color(watcher_sdk_context_t *context, const watcher_light_color_t *color);
watcher_sdk_result_t watcher_light_play_effect(watcher_sdk_context_t *context,
                                               const watcher_light_effect_options_t *options,
                                               watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_light_off(watcher_sdk_context_t *context);

watcher_sdk_result_t watcher_microphone_open(watcher_sdk_context_t *context,
                                             const watcher_microphone_config_t *config,
                                             watcher_sdk_session_id_t *out_session_id);
watcher_sdk_result_t watcher_microphone_close(watcher_sdk_context_t *context,
                                              watcher_sdk_session_id_t session_id);

watcher_sdk_result_t watcher_camera_capture(watcher_sdk_context_t *context,
                                            const watcher_camera_capture_config_t *config,
                                            watcher_sdk_session_id_t *out_session_id);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_H */
