#ifndef WS_ROUTER_H
#define WS_ROUTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WS_MSG_UNKNOWN = 0,
    WS_MSG_SYS_ACK,
    WS_MSG_SYS_NACK,
    WS_MSG_SYS_PING,
    WS_MSG_SYS_PONG,
    WS_MSG_SYS_SESSION_RESUME,
    WS_MSG_CTRL_SERVO_ANGLE,
    WS_MSG_CTRL_SERVO_PWM_UNLOCK,
    WS_MSG_CTRL_SERVO_PWM_LOCK,
    WS_MSG_CTRL_SERVO_TRAJECTORY_PLAY,
    WS_MSG_CTRL_LIGHT_SET,
    WS_MSG_CTRL_MOTION_JOG,
    WS_MSG_CTRL_MOTION_STOP,
    WS_MSG_CTRL_MICROPHONE_OPEN,
    WS_MSG_CTRL_MICROPHONE_CLOSE,
    WS_MSG_CTRL_ROBOT_STATE_SET,
    WS_MSG_CTRL_SOUND_PLAY,
    WS_MSG_CTRL_CAMERA_VIDEO_CONFIG,
    WS_MSG_CTRL_CAMERA_CAPTURE_IMAGE,
    WS_MSG_CTRL_CAMERA_START_VIDEO,
    WS_MSG_CTRL_CAMERA_STOP_VIDEO,
    WS_MSG_EVT_ASR_RESULT,
    WS_MSG_EVT_AI_STATUS,
    WS_MSG_EVT_AI_THINKING,
    WS_MSG_EVT_AI_REPLY,
    WS_MSG_XFER_OTA_HANDSHAKE,
    WS_MSG_XFER_OTA_CHECKSUM,
    WS_MSG_APP_PACKAGE_TRANSFER_BEGIN,
    WS_MSG_APP_PACKAGE_TRANSFER_COMMIT,
    WS_MSG_APP_PACKAGE_TRANSFER_ABORT,
    WS_MSG_APP_PACKAGE_LIST,
    WS_MSG_APP_PACKAGE_INSTALL,
    WS_MSG_APP_PACKAGE_OPEN,
    WS_MSG_APP_PACKAGE_UNINSTALL,
} ws_msg_type_t;

#define WS_MESSAGE_TYPE_MAX 48
#define WS_COMMAND_ID_MAX 48
#define WS_SERVO_TRAJECTORY_MAX_FRAMES 64
#define WS_TEXT_DATA_MAX 256
#define WS_STATUS_MAX 64
#define WS_KIND_MAX 32
#define WS_LIGHT_MODE_MAX 16
#define WS_LIGHT_EFFECT_MAX 24
#define WS_LIGHT_ZONE_MAX 16
#define WS_RESOURCE_NAME_MAX 96
#define WS_CAPTURE_ACTION_MAX 16
#define WS_TRANSFER_ID_MAX 64
#define WS_SHA256_MAX 80
#define WS_STATE_ID_MAX 32

typedef struct {
    char type[WS_MESSAGE_TYPE_MAX];
    char command_id[WS_COMMAND_ID_MAX];
    char message[WS_TEXT_DATA_MAX];
    char audio_uplink_codec[24];
    char audio_uplink_packetization[48];
    int audio_uplink_sample_rate;
    int audio_uplink_channels;
    int audio_uplink_frame_duration_ms;
    int audio_uplink_version;
    int code;
} ws_sys_ack_t;

typedef struct {
    char type[WS_MESSAGE_TYPE_MAX];
    char command_id[WS_COMMAND_ID_MAX];
    char reason[WS_TEXT_DATA_MAX];
    int code;
} ws_sys_nack_t;

typedef struct {
    bool has_x;
    bool has_y;
    float x_deg;
    float y_deg;
    int duration_ms;
} ws_servo_cmd_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    uint8_t axis_mask;
} ws_servo_pwm_unlock_cmd_t;

typedef struct {
    float x_deg;
    float y_deg;
    int duration_ms;
    uint8_t axis_mask;
    uint8_t motion_profile;
} ws_servo_trajectory_frame_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    int frame_count;
    ws_servo_trajectory_frame_t frames[WS_SERVO_TRAJECTORY_MAX_FRAMES];
} ws_servo_trajectory_cmd_t;

typedef struct {
    bool is_x_axis;
    int direction;
    float speed;
    int velocity_deg_per_sec;
    int timeout_ms;
} ws_motion_jog_cmd_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
} ws_microphone_cmd_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    char state_id[WS_STATE_ID_MAX];
} ws_state_cmd_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    char sound_id[WS_RESOURCE_NAME_MAX];
    int delay_ms;
} ws_sound_cmd_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    char mode[WS_LIGHT_MODE_MAX];
    char effect[WS_LIGHT_EFFECT_MAX];
    char zone[WS_LIGHT_ZONE_MAX];
    int red;
    int green;
    int blue;
    int secondary_red;
    int secondary_green;
    int secondary_blue;
    int brightness;
    int period_ms;
    int repeat_count;
} ws_light_cmd_t;

typedef struct {
    char text[WS_TEXT_DATA_MAX];
} ws_text_event_t;

typedef struct {
    char status[WS_STATUS_MAX];
    char message[WS_TEXT_DATA_MAX];
    char image_name[WS_RESOURCE_NAME_MAX];
    char action_file[WS_RESOURCE_NAME_MAX];
    char sound_file[WS_RESOURCE_NAME_MAX];
    char state_domain[16];
    char task_id[64];
    int active_task_count;
    bool has_active_task_count;
    bool foreground_active;
    bool has_foreground_active;
    bool has_action_file;
    bool tts_downlink_complete;
} ws_ai_status_t;

typedef struct {
    char kind[WS_KIND_MAX];
    char content[WS_TEXT_DATA_MAX];
} ws_ai_thinking_t;

typedef struct {
    char command_id[WS_COMMAND_ID_MAX];
    char action[WS_CAPTURE_ACTION_MAX];
    int width;
    int height;
    int fps;
    int quality;
} ws_capture_cmd_t;

typedef struct {
    char message_type[WS_MESSAGE_TYPE_MAX];
    char transfer_id[WS_TRANSFER_ID_MAX];
    char command_id[WS_COMMAND_ID_MAX];
    char app_id[64];
    char name[96];
    char version[32];
    char description[160];
    char package_type[32];
    char source_url[256];
    char image_version[32];
    char file_name[96];
    char sha256[WS_SHA256_MAX];
    size_t size_bytes;
    char reason[WS_TEXT_DATA_MAX];
} ws_transfer_cmd_t;

typedef void (*ws_sys_ack_handler_t)(const ws_sys_ack_t *msg);
typedef void (*ws_sys_nack_handler_t)(const ws_sys_nack_t *msg);
typedef void (*ws_sys_void_handler_t)(void);
typedef void (*ws_servo_handler_t)(const ws_servo_cmd_t *cmd);
typedef void (*ws_servo_pwm_unlock_handler_t)(const ws_servo_pwm_unlock_cmd_t *cmd);
typedef void (*ws_servo_pwm_lock_handler_t)(const ws_servo_pwm_unlock_cmd_t *cmd);
typedef void (*ws_servo_trajectory_handler_t)(const ws_servo_trajectory_cmd_t *cmd);
typedef void (*ws_motion_jog_handler_t)(const ws_motion_jog_cmd_t *cmd);
typedef void (*ws_motion_stop_handler_t)(void);
typedef void (*ws_microphone_handler_t)(const ws_microphone_cmd_t *cmd);
typedef void (*ws_state_handler_t)(const ws_state_cmd_t *cmd);
typedef void (*ws_sound_handler_t)(const ws_sound_cmd_t *cmd);
typedef void (*ws_light_handler_t)(const ws_light_cmd_t *cmd);
typedef void (*ws_text_event_handler_t)(const ws_text_event_t *event);
typedef void (*ws_ai_status_handler_t)(const ws_ai_status_t *event);
typedef void (*ws_ai_thinking_handler_t)(const ws_ai_thinking_t *event);
typedef void (*ws_capture_handler_t)(const ws_capture_cmd_t *cmd);
typedef void (*ws_transfer_handler_t)(const ws_transfer_cmd_t *cmd);

typedef struct {
    ws_sys_ack_handler_t on_sys_ack;
    ws_sys_nack_handler_t on_sys_nack;
    ws_sys_void_handler_t on_sys_ping;
    ws_sys_void_handler_t on_sys_pong;
    ws_sys_void_handler_t on_session_resume;
    ws_servo_handler_t on_servo;
    ws_servo_pwm_unlock_handler_t on_servo_pwm_unlock;
    ws_servo_pwm_lock_handler_t on_servo_pwm_lock;
    ws_servo_trajectory_handler_t on_servo_trajectory_play;
    ws_motion_jog_handler_t on_motion_jog;
    ws_motion_stop_handler_t on_motion_stop;
    ws_microphone_handler_t on_microphone_open;
    ws_microphone_handler_t on_microphone_close;
    ws_state_handler_t on_state_set;
    ws_sound_handler_t on_sound_play;
    ws_light_handler_t on_light_set;
    ws_capture_handler_t on_capture;
    ws_text_event_handler_t on_asr_result;
    ws_ai_status_handler_t on_ai_status;
    ws_ai_thinking_handler_t on_ai_thinking;
    ws_text_event_handler_t on_ai_reply;
    ws_transfer_handler_t on_transfer;
} ws_router_t;

void ws_router_init(ws_router_t *router);
ws_msg_type_t ws_route_message(const char *json_str);

#endif /* WS_ROUTER_H */
