#ifndef WATCHER_SDK_PROTOCOL_H
#define WATCHER_SDK_PROTOCOL_H

#include "watcher_sdk_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHER_SDK_PROTOCOL_COMMAND_ID_MAX 48U
#define WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX 64U
#define WATCHER_SDK_PROTOCOL_PAIRING_CODE_MAX 7U
#define WATCHER_SDK_PROTOCOL_VERSION_MAX 16U
#define WATCHER_SDK_PROTOCOL_CLIENT_NAME_MAX 48U
#define WATCHER_SDK_PROTOCOL_ZONE_MAX 16U
#define WATCHER_SDK_PROTOCOL_SHA256_MAX 65U
#define WATCHER_SDK_PROTOCOL_AUDIO_STREAM_MAX_BYTES (4U * 1024U * 1024U)

typedef enum {
    WATCHER_SDK_PROTOCOL_OK = 0,
    WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT = -1,
    WATCHER_SDK_PROTOCOL_UNSUPPORTED = -2,
    WATCHER_SDK_PROTOCOL_NO_MEMORY = -3,
    WATCHER_SDK_PROTOCOL_OUTPUT_TOO_SMALL = -4,
} watcher_sdk_protocol_result_t;

typedef enum {
    WATCHER_SDK_PROTOCOL_UNKNOWN = 0,
    WATCHER_SDK_PROTOCOL_AUTHENTICATE,
    WATCHER_SDK_PROTOCOL_JOB_CANCEL,
    WATCHER_SDK_PROTOCOL_BEHAVIOR_PLAY,
    WATCHER_SDK_PROTOCOL_BEHAVIOR_STOP,
    WATCHER_SDK_PROTOCOL_ANIMATION_PLAY,
    WATCHER_SDK_PROTOCOL_ANIMATION_STOP,
    WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO,
    WATCHER_SDK_PROTOCOL_MOTION_SET_TARGET,
    WATCHER_SDK_PROTOCOL_MOTION_ACTION_PLAY,
    WATCHER_SDK_PROTOCOL_MOTION_STOP,
    WATCHER_SDK_PROTOCOL_AUDIO_PLAY,
    WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN,
    WATCHER_SDK_PROTOCOL_AUDIO_STOP,
    WATCHER_SDK_PROTOCOL_LIGHT_SET,
    WATCHER_SDK_PROTOCOL_LIGHT_EFFECT_PLAY,
    WATCHER_SDK_PROTOCOL_LIGHT_OFF,
    WATCHER_SDK_PROTOCOL_MICROPHONE_OPEN,
    WATCHER_SDK_PROTOCOL_MICROPHONE_CLOSE,
    WATCHER_SDK_PROTOCOL_CAMERA_CAPTURE,
} watcher_sdk_protocol_command_type_t;

typedef struct {
    watcher_sdk_protocol_command_type_t type;
    char message_type[WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX];
    char command_id[WATCHER_SDK_PROTOCOL_COMMAND_ID_MAX];
    union {
        struct {
            char pairing_code[WATCHER_SDK_PROTOCOL_PAIRING_CODE_MAX];
            char protocol_version[WATCHER_SDK_PROTOCOL_VERSION_MAX];
            char client_name[WATCHER_SDK_PROTOCOL_CLIENT_NAME_MAX];
        } authenticate;
        struct {
            watcher_sdk_job_id_t operation_id;
        } job_cancel;
        struct {
            char behavior_id[WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX];
            uint16_t repeat_count;
        } behavior_play;
        struct {
            char resource_id[WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX];
        } resource;
        struct {
            uint16_t stream_id;
            uint32_t total_bytes;
            char audio_sha256[WATCHER_SDK_PROTOCOL_SHA256_MAX];
        } audio_stream;
        struct {
            bool has_pan;
            bool has_tilt;
            int pan_deg;
            int tilt_deg;
            uint32_t duration_ms;
            bool ease_in_out;
        } motion;
        struct {
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t brightness_percent;
            char zone[WATCHER_SDK_PROTOCOL_ZONE_MAX];
            char effect[WATCHER_SDK_PROTOCOL_RESOURCE_ID_MAX];
            uint16_t period_ms;
            uint16_t repeat_count;
        } light;
        struct {
            uint32_t sample_rate_hz;
        } microphone;
        struct {
            uint32_t session_id;
        } session;
        struct {
            int width;
            int height;
            int quality;
        } camera;
    } data;
} watcher_sdk_protocol_command_t;

watcher_sdk_protocol_result_t watcher_sdk_protocol_parse(const char *json, size_t json_len,
                                                         watcher_sdk_protocol_command_t *out_command);
bool watcher_sdk_protocol_supports_type(const char *message_type);
watcher_sdk_protocol_result_t watcher_sdk_protocol_build_ack(const char *message_type, const char *command_id,
                                                             watcher_sdk_job_id_t operation_id, char *out_json,
                                                             size_t out_size);
watcher_sdk_protocol_result_t watcher_sdk_protocol_build_nack(const char *message_type, const char *command_id,
                                                              const char *reason, char *out_json, size_t out_size);
watcher_sdk_protocol_result_t watcher_sdk_protocol_build_session_ack(const char *message_type,
                                                                     const char *command_id, uint32_t session_id,
                                                                     char *out_json, size_t out_size);
watcher_sdk_protocol_result_t watcher_sdk_protocol_build_ready(const char *device_id, const char *firmware_version,
                                                               char *out_json, size_t out_size);
watcher_sdk_protocol_result_t watcher_sdk_protocol_build_operation_event(const watcher_sdk_event_t *event,
                                                                         char *out_json, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_PROTOCOL_H */
