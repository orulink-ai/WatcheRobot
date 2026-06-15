#ifndef BEHAVIOR_TYPES_H
#define BEHAVIOR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEHAVIOR_VERSION_LEN 16
#define BEHAVIOR_STATE_ID_LEN 32
#define BEHAVIOR_TEXT_LEN 128
#define BEHAVIOR_SOUND_ID_LEN 32

#define BEHAVIOR_ACTION_DEFAULT_X_DEG 90
#define BEHAVIOR_ACTION_DEFAULT_Y_DEG 120
#define BEHAVIOR_ACTION_SINGLE_KEYFRAME_DURATION_MS 100
#define BEHAVIOR_SERVO_LOGICAL_MIN_DEG 0
#define BEHAVIOR_SERVO_LOGICAL_MAX_DEG 180
#define BEHAVIOR_SERVO_X_MIN_DEG BEHAVIOR_SERVO_LOGICAL_MIN_DEG
#define BEHAVIOR_SERVO_X_MAX_DEG BEHAVIOR_SERVO_LOGICAL_MAX_DEG
#define BEHAVIOR_SERVO_Y_MIN_DEG 100
#define BEHAVIOR_SERVO_Y_MAX_DEG 140

typedef enum {
    BEHAVIOR_MOTION_PROFILE_LINEAR = 0,
    BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT = 1,
} behavior_motion_profile_t;

typedef struct {
    uint32_t at_ms;
    int x_deg;
    int y_deg;
    int duration_ms;
    uint8_t motion_profile;
} behavior_motion_event_t;

typedef struct {
    uint32_t at_ms;
    char anim[BEHAVIOR_STATE_ID_LEN];
    char text[BEHAVIOR_TEXT_LEN];
    int font_size;
} behavior_expression_event_t;

typedef struct {
    uint32_t at_ms;
    char sound_id[BEHAVIOR_SOUND_ID_LEN];
} behavior_sound_event_t;

typedef struct {
    char id[BEHAVIOR_STATE_ID_LEN];
    bool loop;
    bool hold_until_replaced;
    uint32_t timeline_end_ms;
    behavior_motion_event_t *motion;
    int motion_count;
    behavior_expression_event_t *expression;
    int expression_count;
    behavior_sound_event_t *sound;
    int sound_count;
} behavior_state_def_t;

typedef struct {
    char version[BEHAVIOR_VERSION_LEN];
    char default_state[BEHAVIOR_STATE_ID_LEN];
    behavior_state_def_t *states;
    int state_count;
} behavior_catalog_t;

typedef struct {
    char id[BEHAVIOR_STATE_ID_LEN];
    uint32_t total_duration_ms;
    behavior_motion_event_t *motion;
    int motion_count;
} behavior_action_def_t;

typedef struct {
    behavior_action_def_t *actions;
    int action_count;
} behavior_action_catalog_t;

typedef bool (*behavior_anim_validator_t)(const char *anim_id, void *ctx);

void behavior_free_catalog(behavior_catalog_t *catalog);
void behavior_free_action_catalog(behavior_action_catalog_t *catalog);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_TYPES_H */
