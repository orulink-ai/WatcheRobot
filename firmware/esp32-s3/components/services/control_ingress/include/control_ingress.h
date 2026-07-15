#ifndef CONTROL_INGRESS_H
#define CONTROL_INGRESS_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROL_MOTION_SOURCE_UNKNOWN = 0,
    CONTROL_MOTION_SOURCE_BEHAVIOR = 1,
    CONTROL_MOTION_SOURCE_BLE = 2,
    CONTROL_MOTION_SOURCE_WS = 3,
    CONTROL_MOTION_SOURCE_RECOVERY = 4,
    CONTROL_MOTION_SOURCE_STRESS = 5,
    CONTROL_MOTION_SOURCE_RELAY_W1 = 6,
    CONTROL_MOTION_SOURCE_SDK = 7,
} control_motion_source_t;

typedef struct {
    bool has_x;
    bool has_y;
    int x_deg;
    int y_deg;
    int duration_ms;
    control_motion_source_t source;
} control_servo_request_t;

typedef struct {
    bool is_x_axis;
    int velocity_deg_per_sec;
    int timeout_ms;
    control_motion_source_t source;
} control_jog_request_t;

typedef struct {
    bool has_x;
    bool has_y;
    int x_velocity_deg_per_sec;
    int y_velocity_deg_per_sec;
    int timeout_ms;
    control_motion_source_t source;
} control_jog_vector_request_t;

typedef struct {
    char status[64];
    char message[256];
    char image_name[64];
    char action_file[64];
    char sound_file[64];
    bool defer_ui_until_tts_complete;
} control_ai_status_request_t;

typedef struct {
    char state_id[32];
} control_state_set_request_t;

typedef struct {
    char state_id[32];
    char text[256];
    int font_size;
} control_state_text_request_t;

esp_err_t control_ingress_init(void);
esp_err_t control_ingress_submit_servo_with_seq(const control_servo_request_t *req, uint32_t *out_seq);
esp_err_t control_ingress_submit_servo(const control_servo_request_t *req);
esp_err_t control_ingress_submit_jog_with_seq(const control_jog_request_t *req, uint32_t *out_seq);
esp_err_t control_ingress_submit_jog(const control_jog_request_t *req);
esp_err_t control_ingress_submit_jog_vector_with_seq(const control_jog_vector_request_t *req, uint32_t *out_seq);
esp_err_t control_ingress_submit_jog_vector(const control_jog_vector_request_t *req);
esp_err_t control_ingress_stop_manual(control_motion_source_t source);
void control_ingress_suppress_manual_touch_for_ms(uint32_t duration_ms);
bool control_ingress_is_manual_touch_suppressed(void);
uint32_t control_ingress_manual_touch_remaining_ms(void);
esp_err_t control_ingress_submit_ai_status(const control_ai_status_request_t *req);
esp_err_t control_ingress_flush_deferred_ai_status_after_tts(void);
esp_err_t control_ingress_submit_state_set(const control_state_set_request_t *req);
esp_err_t control_ingress_submit_state_text(const control_state_text_request_t *req);
size_t control_ingress_state_stack_high_watermark(void);
size_t control_ingress_state_stack_size(void);

typedef struct {
    esp_err_t (*interrupt_action)(const char *source);
    esp_err_t (*move_sync)(int x_deg, int y_deg, int duration_ms, control_motion_source_t source, uint32_t *out_seq);
    esp_err_t (*move_axis)(bool is_x_axis, int angle_deg, int duration_ms, control_motion_source_t source,
                           uint32_t *out_seq);
    esp_err_t (*move_sync_direct)(int x_deg, int y_deg, control_motion_source_t source, uint32_t *out_seq);
    esp_err_t (*move_axis_direct)(bool is_x_axis, int angle_deg, control_motion_source_t source, uint32_t *out_seq);
    esp_err_t (*jog_axis)(bool is_x_axis, int velocity_deg_per_sec, int timeout_ms, control_motion_source_t source,
                          uint32_t *out_seq);
    esp_err_t (*jog_vector)(int x_velocity_deg_per_sec, int y_velocity_deg_per_sec, int timeout_ms,
                            control_motion_source_t source, uint32_t *out_seq);
    esp_err_t (*stop)(control_motion_source_t source);
} control_ingress_ops_t;

#if defined(CONTROL_INGRESS_ENABLE_TEST_API)
void control_ingress_set_ops_for_test(const control_ingress_ops_t *ops);
void control_ingress_reset_ops_for_test(void);
void control_ingress_normalize_resource_name_for_test(const char *raw, char *out, size_t out_size);
const char *control_ingress_ai_status_text_for_test(const control_ai_status_request_t *req);
void control_ingress_reset_deferred_ai_status_for_test(void);
void control_ingress_reset_state_queue_for_test(void);
bool control_ingress_has_deferred_ai_status_for_test(void);
const char *control_ingress_deferred_ai_status_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_INGRESS_H */
