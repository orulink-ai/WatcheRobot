#include "watcher_sdk_protocol.h"

#include "cJSON.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool copy_bounded_string(char *destination, size_t size, const char *source, bool allow_empty) {
    size_t length;

    if (destination == NULL || size == 0U || source == NULL) {
        return false;
    }
    length = strlen(source);
    if ((!allow_empty && length == 0U) || length >= size) {
        destination[0] = '\0';
        return false;
    }
    memcpy(destination, source, length + 1U);
    return true;
}

static const char *json_string(cJSON *object, const char *key) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : NULL;
}

static bool json_int_range(cJSON *object, const char *key, int minimum, int maximum, int *out_value) {
    cJSON *item = cJSON_GetObjectItem(object, key);
    double value;

    if (!cJSON_IsNumber(item) || out_value == NULL) {
        return false;
    }
    value = item->valuedouble;
    if (value < (double)minimum || value > (double)maximum || value != (double)((int)value)) {
        return false;
    }
    *out_value = (int)value;
    return true;
}

static bool json_optional_int_range(cJSON *object, const char *key, int minimum, int maximum, int default_value,
                                    int *out_value) {
    cJSON *item = cJSON_GetObjectItem(object, key);

    if (item == NULL) {
        *out_value = default_value;
        return true;
    }
    return json_int_range(object, key, minimum, maximum, out_value);
}

static bool six_digit_code(const char *value) {
    size_t index;

    if (value == NULL || strlen(value) != 6U) {
        return false;
    }
    for (index = 0U; index < 6U; ++index) {
        if (!isdigit((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

static bool parse_hex_color(const char *color, uint8_t *red, uint8_t *green, uint8_t *blue) {
    unsigned int values[6];
    size_t index;

    if (color == NULL || strlen(color) != 7U || color[0] != '#') {
        return false;
    }
    for (index = 0U; index < 6U; ++index) {
        int ch = color[index + 1U];
        if (!isxdigit(ch)) {
            return false;
        }
        values[index] = (unsigned int)(isdigit(ch) ? ch - '0' : 10 + tolower(ch) - 'a');
    }
    *red = (uint8_t)((values[0] << 4U) | values[1]);
    *green = (uint8_t)((values[2] << 4U) | values[3]);
    *blue = (uint8_t)((values[4] << 4U) | values[5]);
    return true;
}

static watcher_sdk_protocol_command_type_t command_type_from_name(const char *type) {
    static const struct {
        const char *name;
        watcher_sdk_protocol_command_type_t type;
    } mappings[] = {
        {"sys.sdk.authenticate", WATCHER_SDK_PROTOCOL_AUTHENTICATE},
        {"ctrl.job.cancel", WATCHER_SDK_PROTOCOL_JOB_CANCEL},
        {"ctrl.behavior.play", WATCHER_SDK_PROTOCOL_BEHAVIOR_PLAY},
        {"ctrl.behavior.stop", WATCHER_SDK_PROTOCOL_BEHAVIOR_STOP},
        {"ctrl.animation.play", WATCHER_SDK_PROTOCOL_ANIMATION_PLAY},
        {"ctrl.animation.stop", WATCHER_SDK_PROTOCOL_ANIMATION_STOP},
        {"ctrl.motion.move_to", WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO},
        {"ctrl.motion.set_target", WATCHER_SDK_PROTOCOL_MOTION_SET_TARGET},
        {"ctrl.motion.action.play", WATCHER_SDK_PROTOCOL_MOTION_ACTION_PLAY},
        {"ctrl.motion.stop", WATCHER_SDK_PROTOCOL_MOTION_STOP},
        {"ctrl.audio.play", WATCHER_SDK_PROTOCOL_AUDIO_PLAY},
        {"ctrl.audio.stream.begin", WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN},
        {"ctrl.audio.stop", WATCHER_SDK_PROTOCOL_AUDIO_STOP},
        {"ctrl.light.set", WATCHER_SDK_PROTOCOL_LIGHT_SET},
        {"ctrl.light.effect.play", WATCHER_SDK_PROTOCOL_LIGHT_EFFECT_PLAY},
        {"ctrl.light.off", WATCHER_SDK_PROTOCOL_LIGHT_OFF},
        {"ctrl.microphone.open", WATCHER_SDK_PROTOCOL_MICROPHONE_OPEN},
        {"ctrl.microphone.close", WATCHER_SDK_PROTOCOL_MICROPHONE_CLOSE},
        {"ctrl.camera.capture", WATCHER_SDK_PROTOCOL_CAMERA_CAPTURE},
    };
    size_t index;

    if (type == NULL) {
        return WATCHER_SDK_PROTOCOL_UNKNOWN;
    }
    for (index = 0U; index < sizeof(mappings) / sizeof(mappings[0]); ++index) {
        if (strcmp(type, mappings[index].name) == 0) {
            return mappings[index].type;
        }
    }
    return WATCHER_SDK_PROTOCOL_UNKNOWN;
}

bool watcher_sdk_protocol_supports_type(const char *message_type) {
    return command_type_from_name(message_type) != WATCHER_SDK_PROTOCOL_UNKNOWN;
}

static bool command_needs_only_id(watcher_sdk_protocol_command_type_t type) {
    return type == WATCHER_SDK_PROTOCOL_BEHAVIOR_STOP || type == WATCHER_SDK_PROTOCOL_ANIMATION_STOP ||
           type == WATCHER_SDK_PROTOCOL_MOTION_STOP || type == WATCHER_SDK_PROTOCOL_AUDIO_STOP ||
           type == WATCHER_SDK_PROTOCOL_LIGHT_OFF;
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_parse(const char *json, size_t json_len,
                                                         watcher_sdk_protocol_command_t *out_command) {
    cJSON *root;
    cJSON *data;
    const char *type_name;
    const char *command_id;
    const char *resource_id;
    int number;

    if (json == NULL || json_len == 0U || out_command == NULL) {
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    memset(out_command, 0, sizeof(*out_command));
    root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    type_name = json_string(root, "type");
    data = cJSON_GetObjectItem(root, "data");
    if (type_name == NULL || !cJSON_IsObject(data) ||
        !copy_bounded_string(out_command->message_type, sizeof(out_command->message_type), type_name, false)) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    command_id = json_string(data, "command_id");
    if (!copy_bounded_string(out_command->command_id, sizeof(out_command->command_id), command_id, false)) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    out_command->type = command_type_from_name(type_name);
    if (out_command->type == WATCHER_SDK_PROTOCOL_UNKNOWN) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_UNSUPPORTED;
    }

    if (command_needs_only_id(out_command->type)) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_OK;
    }

    switch (out_command->type) {
    case WATCHER_SDK_PROTOCOL_AUTHENTICATE: {
        const char *pairing_code = json_string(data, "pairing_code");
        const char *protocol_version = json_string(data, "protocol_version");
        const char *client_name = json_string(data, "client_name");
        if (!six_digit_code(pairing_code) ||
            !copy_bounded_string(out_command->data.authenticate.pairing_code,
                                 sizeof(out_command->data.authenticate.pairing_code), pairing_code, false) ||
            !copy_bounded_string(out_command->data.authenticate.protocol_version,
                                 sizeof(out_command->data.authenticate.protocol_version), protocol_version, false) ||
            !copy_bounded_string(out_command->data.authenticate.client_name,
                                 sizeof(out_command->data.authenticate.client_name), client_name, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    }
    case WATCHER_SDK_PROTOCOL_JOB_CANCEL:
        if (!json_int_range(data, "operation_id", 1, INT_MAX, &number)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.job_cancel.operation_id = (watcher_sdk_job_id_t)number;
        break;
    case WATCHER_SDK_PROTOCOL_BEHAVIOR_PLAY:
        resource_id = json_string(data, "behavior_id");
        if (!copy_bounded_string(out_command->data.behavior_play.behavior_id,
                                 sizeof(out_command->data.behavior_play.behavior_id), resource_id, false) ||
            !json_optional_int_range(data, "repeat", 1, UINT16_MAX, 1, &number)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.behavior_play.repeat_count = (uint16_t)number;
        break;
    case WATCHER_SDK_PROTOCOL_ANIMATION_PLAY:
        resource_id = json_string(data, "animation_id");
        if (!copy_bounded_string(out_command->data.resource.resource_id, sizeof(out_command->data.resource.resource_id),
                                 resource_id, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    case WATCHER_SDK_PROTOCOL_MOTION_ACTION_PLAY:
        resource_id = json_string(data, "action_id");
        if (!copy_bounded_string(out_command->data.resource.resource_id, sizeof(out_command->data.resource.resource_id),
                                 resource_id, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    case WATCHER_SDK_PROTOCOL_AUDIO_PLAY:
        resource_id = json_string(data, "sound_id");
        if (!copy_bounded_string(out_command->data.resource.resource_id, sizeof(out_command->data.resource.resource_id),
                                 resource_id, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    case WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN: {
        const char *audio_sha256 = json_string(data, "audio_sha256");
        int sample_rate_hz;
        int channels;
        int sample_width_bytes;
        size_t index;

        if (!json_int_range(data, "stream_id", 1, UINT16_MAX, &number)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.audio_stream.stream_id = (uint16_t)number;
        if (!json_int_range(data, "total_bytes", 2, WATCHER_SDK_PROTOCOL_AUDIO_STREAM_MAX_BYTES, &number) ||
            (number % 2) != 0 || !json_int_range(data, "sample_rate_hz", 24000, 24000, &sample_rate_hz) ||
            !json_int_range(data, "channels", 1, 1, &channels) ||
            !json_int_range(data, "sample_width_bytes", 2, 2, &sample_width_bytes) || audio_sha256 == NULL ||
            strlen(audio_sha256) != 64U) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        for (index = 0U; index < 64U; ++index) {
            if (!isxdigit((unsigned char)audio_sha256[index])) {
                cJSON_Delete(root);
                return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
            }
        }
        out_command->data.audio_stream.total_bytes = (uint32_t)number;
        if (!copy_bounded_string(out_command->data.audio_stream.audio_sha256,
                                 sizeof(out_command->data.audio_stream.audio_sha256), audio_sha256, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    }
    case WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO:
    case WATCHER_SDK_PROTOCOL_MOTION_SET_TARGET: {
        cJSON *pan = cJSON_GetObjectItem(data, "pan_deg");
        cJSON *tilt = cJSON_GetObjectItem(data, "tilt_deg");
        const char *profile = json_string(data, "profile");

        out_command->data.motion.has_pan = pan != NULL;
        out_command->data.motion.has_tilt = tilt != NULL;
        if ((out_command->data.motion.has_pan &&
             !json_int_range(data, "pan_deg", 0, 180, &out_command->data.motion.pan_deg)) ||
            (out_command->data.motion.has_tilt &&
             !json_int_range(data, "tilt_deg", 0, 180, &out_command->data.motion.tilt_deg))) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        if ((!out_command->data.motion.has_pan && !out_command->data.motion.has_tilt) ||
            (out_command->type == WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO &&
             (!out_command->data.motion.has_pan || !out_command->data.motion.has_tilt))) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        if (out_command->type == WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO) {
            if (!json_int_range(data, "duration_ms", 1, UINT16_MAX, &number)) {
                cJSON_Delete(root);
                return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
            }
            out_command->data.motion.duration_ms = (uint32_t)number;
        }
        if (profile != NULL && strcmp(profile, "linear") != 0 && strcmp(profile, "ease_in_out") != 0) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.motion.ease_in_out = profile != NULL && strcmp(profile, "ease_in_out") == 0;
        break;
    }
    case WATCHER_SDK_PROTOCOL_LIGHT_SET:
    case WATCHER_SDK_PROTOCOL_LIGHT_EFFECT_PLAY: {
        cJSON *brightness = cJSON_GetObjectItem(data, "brightness");
        const char *color = json_string(data, "color");
        const char *zone = json_string(data, "zone");
        if (!parse_hex_color(color, &out_command->data.light.red, &out_command->data.light.green,
                             &out_command->data.light.blue)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.light.brightness_percent = 100U;
        if (brightness != NULL) {
            if (!cJSON_IsNumber(brightness) || brightness->valuedouble < 0.0 || brightness->valuedouble > 1.0) {
                cJSON_Delete(root);
                return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
            }
            out_command->data.light.brightness_percent = (uint8_t)(brightness->valuedouble * 100.0 + 0.5);
        }
        if (zone == NULL) {
            zone = "all";
        }
        if ((strcmp(zone, "all") != 0 && strcmp(zone, "side") != 0 && strcmp(zone, "bottom") != 0) ||
            !copy_bounded_string(out_command->data.light.zone, sizeof(out_command->data.light.zone), zone, false)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        if (out_command->type == WATCHER_SDK_PROTOCOL_LIGHT_EFFECT_PLAY) {
            if (!copy_bounded_string(out_command->data.light.effect, sizeof(out_command->data.light.effect),
                                     json_string(data, "effect"), false) ||
                !json_optional_int_range(data, "period_ms", 0, UINT16_MAX, 0, &number)) {
                cJSON_Delete(root);
                return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
            }
            out_command->data.light.period_ms = (uint16_t)number;
            if (!json_optional_int_range(data, "repeat", 0, UINT16_MAX, 0, &number)) {
                cJSON_Delete(root);
                return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
            }
            out_command->data.light.repeat_count = (uint16_t)number;
        }
        break;
    }
    case WATCHER_SDK_PROTOCOL_MICROPHONE_OPEN:
        if (!json_optional_int_range(data, "sample_rate_hz", 16000, 16000, 16000, &number)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.microphone.sample_rate_hz = (uint32_t)number;
        break;
    case WATCHER_SDK_PROTOCOL_MICROPHONE_CLOSE:
        if (!json_int_range(data, "session_id", 1, INT_MAX, &number)) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        out_command->data.session.session_id = (uint32_t)number;
        break;
    case WATCHER_SDK_PROTOCOL_CAMERA_CAPTURE:
        if (!json_optional_int_range(data, "width", 0, 4096, 0, &out_command->data.camera.width) ||
            !json_optional_int_range(data, "height", 0, 4096, 0, &out_command->data.camera.height) ||
            !json_optional_int_range(data, "quality", 0, 100, 0, &out_command->data.camera.quality) ||
            ((out_command->data.camera.width == 0) != (out_command->data.camera.height == 0))) {
            cJSON_Delete(root);
            return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
        }
        break;
    case WATCHER_SDK_PROTOCOL_UNKNOWN:
    case WATCHER_SDK_PROTOCOL_BEHAVIOR_STOP:
    case WATCHER_SDK_PROTOCOL_ANIMATION_STOP:
    case WATCHER_SDK_PROTOCOL_MOTION_STOP:
    case WATCHER_SDK_PROTOCOL_AUDIO_STOP:
    case WATCHER_SDK_PROTOCOL_LIGHT_OFF:
    default:
        break;
    }

    cJSON_Delete(root);
    return WATCHER_SDK_PROTOCOL_OK;
}

static watcher_sdk_protocol_result_t print_json(cJSON *root, char *out_json, size_t out_size) {
    char *serialized;
    size_t length;

    if (root == NULL || out_json == NULL || out_size == 0U) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == NULL) {
        return WATCHER_SDK_PROTOCOL_NO_MEMORY;
    }
    length = strlen(serialized);
    if (length >= out_size) {
        cJSON_free(serialized);
        return WATCHER_SDK_PROTOCOL_OUTPUT_TOO_SMALL;
    }
    memcpy(out_json, serialized, length + 1U);
    cJSON_free(serialized);
    return WATCHER_SDK_PROTOCOL_OK;
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_ack(const char *message_type, const char *command_id,
                                                             watcher_sdk_job_id_t operation_id, char *out_json,
                                                             size_t out_size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *data;

    if (root == NULL || message_type == NULL || command_id == NULL) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    cJSON_AddStringToObject(root, "type", "sys.ack");
    cJSON_AddNumberToObject(root, "code", 0);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "type", message_type);
    cJSON_AddStringToObject(data, "command_id", command_id);
    if (operation_id != WATCHER_SDK_JOB_INVALID) {
        cJSON_AddNumberToObject(data, "operation_id", operation_id);
    }
    return print_json(root, out_json, out_size);
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_nack(const char *message_type, const char *command_id,
                                                              const char *reason, char *out_json, size_t out_size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *data;

    if (root == NULL || message_type == NULL || command_id == NULL || reason == NULL) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    cJSON_AddStringToObject(root, "type", "sys.nack");
    cJSON_AddNumberToObject(root, "code", 1);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "type", message_type);
    cJSON_AddStringToObject(data, "command_id", command_id);
    cJSON_AddStringToObject(data, "reason", reason);
    return print_json(root, out_json, out_size);
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_session_ack(const char *message_type, const char *command_id,
                                                                     uint32_t session_id, char *out_json,
                                                                     size_t out_size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *data;

    if (root == NULL || message_type == NULL || command_id == NULL || session_id == 0U) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    cJSON_AddStringToObject(root, "type", "sys.ack");
    cJSON_AddNumberToObject(root, "code", 0);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "type", message_type);
    cJSON_AddStringToObject(data, "command_id", command_id);
    cJSON_AddNumberToObject(data, "session_id", session_id);
    return print_json(root, out_json, out_size);
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_ready(const char *device_id, const char *firmware_version,
                                                               char *out_json, size_t out_size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *data;
    cJSON *capabilities;
    static const char *names[] = {"behavior",         "animation",
                                  "motion",           "audio",
                                  "audio.stream",     "light",
                                  "microphone",       "camera.capture",
                                  "input.back_touch", "input.screen_touch",
                                  "input.roller"};
    size_t index;

    if (root == NULL || device_id == NULL) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    cJSON_AddStringToObject(root, "type", "evt.sdk.ready");
    cJSON_AddNumberToObject(root, "code", 0);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "protocol_version", "1.0");
    cJSON_AddStringToObject(data, "device_id", device_id);
    cJSON_AddStringToObject(data, "firmware_version", firmware_version != NULL ? firmware_version : "unknown");
    capabilities = cJSON_AddArrayToObject(data, "capabilities");
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        cJSON_AddItemToArray(capabilities, cJSON_CreateString(names[index]));
    }
    return print_json(root, out_json, out_size);
}

static const char *job_state_name(watcher_sdk_job_state_t state) {
    switch (state) {
    case WATCHER_SDK_JOB_STARTING:
        return "starting";
    case WATCHER_SDK_JOB_RUNNING:
        return "running";
    case WATCHER_SDK_JOB_COMPLETED:
        return "completed";
    case WATCHER_SDK_JOB_FAILED:
        return "failed";
    case WATCHER_SDK_JOB_CANCELLED:
        return "cancelled";
    case WATCHER_SDK_JOB_UNKNOWN:
    default:
        return "unknown";
    }
}

static const char *domain_name(watcher_sdk_domain_t domain) {
    static const char *names[] = {"none", "behavior", "animation", "motion", "audio", "light", "microphone", "camera"};
    return domain >= WATCHER_SDK_DOMAIN_NONE && domain < WATCHER_SDK_DOMAIN_COUNT ? names[domain] : "none";
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_operation_event(const watcher_sdk_event_t *event,
                                                                         char *out_json, size_t out_size) {
    cJSON *root = cJSON_CreateObject();
    cJSON *data;

    if (root == NULL || event == NULL || event->job_id == WATCHER_SDK_JOB_INVALID) {
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    cJSON_AddStringToObject(root, "type", "evt.sdk.operation");
    cJSON_AddNumberToObject(root, "code", 0);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddNumberToObject(data, "operation_id", event->job_id);
    cJSON_AddStringToObject(data, "domain", domain_name(event->domain));
    cJSON_AddStringToObject(data, "state", job_state_name(event->state));
    if (event->error_code != 0) {
        cJSON_AddNumberToObject(data, "error_code", event->error_code);
    }
    return print_json(root, out_json, out_size);
}

static const char *input_source_name(watcher_sdk_input_source_t source) {
    switch (source) {
    case WATCHER_SDK_INPUT_BACK_TOUCH:
        return "back_touch";
    case WATCHER_SDK_INPUT_SCREEN_TOUCH:
        return "screen_touch";
    case WATCHER_SDK_INPUT_ROLLER:
        return "roller";
    case WATCHER_SDK_INPUT_UNKNOWN:
    default:
        return NULL;
    }
}

static const char *input_action_name(watcher_sdk_input_action_t action) {
    switch (action) {
    case WATCHER_SDK_INPUT_ACTION_PRESS:
        return "press";
    case WATCHER_SDK_INPUT_ACTION_RELEASE:
        return "release";
    case WATCHER_SDK_INPUT_ACTION_LONG_PRESS:
        return "long_press";
    case WATCHER_SDK_INPUT_ACTION_TAP:
        return "tap";
    case WATCHER_SDK_INPUT_ACTION_ROTATE:
        return "rotate";
    case WATCHER_SDK_INPUT_ACTION_UNKNOWN:
    default:
        return NULL;
    }
}

static bool input_event_is_valid(const watcher_sdk_input_event_t *event) {
    if (event == NULL) {
        return false;
    }
    switch (event->source) {
    case WATCHER_SDK_INPUT_BACK_TOUCH:
        return event->action == WATCHER_SDK_INPUT_ACTION_PRESS || event->action == WATCHER_SDK_INPUT_ACTION_RELEASE ||
               event->action == WATCHER_SDK_INPUT_ACTION_LONG_PRESS;
    case WATCHER_SDK_INPUT_SCREEN_TOUCH:
        return event->action == WATCHER_SDK_INPUT_ACTION_TAP && event->x >= 0 && event->y >= 0;
    case WATCHER_SDK_INPUT_ROLLER:
        return event->action == WATCHER_SDK_INPUT_ACTION_ROTATE && event->delta != 0;
    case WATCHER_SDK_INPUT_UNKNOWN:
    default:
        return false;
    }
}

watcher_sdk_protocol_result_t watcher_sdk_protocol_build_input_event(const watcher_sdk_input_event_t *event,
                                                                     char *out_json, size_t out_size) {
    cJSON *root;
    cJSON *data;
    const char *source;
    const char *action;

    if (!input_event_is_valid(event)) {
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    source = input_source_name(event->source);
    action = input_action_name(event->action);
    if (source == NULL || action == NULL) {
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return WATCHER_SDK_PROTOCOL_NO_MEMORY;
    }
    cJSON_AddStringToObject(root, "type", "evt.sdk.input");
    cJSON_AddNumberToObject(root, "code", 0);
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "source", source);
    cJSON_AddStringToObject(data, "action", action);
    cJSON_AddNumberToObject(data, "timestamp_ms", event->timestamp_ms);
    switch (event->source) {
    case WATCHER_SDK_INPUT_BACK_TOUCH:
        cJSON_AddNumberToObject(data, "touch_id", event->touch_id);
        break;
    case WATCHER_SDK_INPUT_SCREEN_TOUCH:
        cJSON_AddNumberToObject(data, "x", event->x);
        cJSON_AddNumberToObject(data, "y", event->y);
        break;
    case WATCHER_SDK_INPUT_ROLLER:
        cJSON_AddNumberToObject(data, "delta", event->delta);
        break;
    case WATCHER_SDK_INPUT_UNKNOWN:
    default:
        cJSON_Delete(root);
        return WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT;
    }
    return print_json(root, out_json, out_size);
}
