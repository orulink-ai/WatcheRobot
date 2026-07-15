/**
 * @file ws_router.c
 * @brief WebSocket message router implementation (protocol v0.1.5)
 */

#include "ws_router.h"
#include "cJSON.h"
#include <string.h>

static ws_router_t g_router = {0};

static const char *get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

static cJSON *get_object(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsObject(item)) {
        return item;
    }
    return NULL;
}

static int get_int(cJSON *obj, const char *key, int default_val) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_val;
}

static int get_int_any(cJSON *obj, const char *key_a, const char *key_b, int default_val) {
    int value;

    value = get_int(obj, key_a, default_val);
    if (value != default_val || key_b == NULL) {
        return value;
    }
    return get_int(obj, key_b, default_val);
}

static bool get_float(cJSON *obj, const char *key, float *out_value) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item) && out_value) {
        *out_value = (float)item->valuedouble;
        return true;
    }
    return false;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (src) {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static void fill_sys_ack(cJSON *root, ws_sys_ack_t *ack) {
    cJSON *data = get_object(root, "data");

    memset(ack, 0, sizeof(*ack));
    ack->code = get_int(root, "code", 0);
    if (data) {
        copy_string(ack->type, sizeof(ack->type), get_string(data, "type"));
        if (ack->type[0] == '\0') {
            copy_string(ack->type, sizeof(ack->type), get_string(data, "ref_type"));
        }
        copy_string(ack->command_id, sizeof(ack->command_id), get_string(data, "command_id"));
        if (ack->command_id[0] == '\0') {
            copy_string(ack->command_id, sizeof(ack->command_id), get_string(data, "ref_id"));
        }
        copy_string(ack->message, sizeof(ack->message), get_string(data, "message"));
        cJSON *negotiated = get_object(data, "negotiated");
        cJSON *audio_uplink = negotiated != NULL ? get_object(negotiated, "audio_uplink") : NULL;
        if (audio_uplink != NULL) {
            copy_string(ack->audio_uplink_codec, sizeof(ack->audio_uplink_codec),
                        get_string(audio_uplink, "codec"));
            copy_string(ack->audio_uplink_packetization, sizeof(ack->audio_uplink_packetization),
                        get_string(audio_uplink, "packetization"));
            ack->audio_uplink_sample_rate = get_int(audio_uplink, "sample_rate", 0);
            ack->audio_uplink_channels = get_int(audio_uplink, "channels", 0);
            ack->audio_uplink_frame_duration_ms = get_int(audio_uplink, "frame_duration_ms", 0);
            ack->audio_uplink_version = get_int(audio_uplink, "version", 0);
        }
    }
}

static void fill_sys_nack(cJSON *root, ws_sys_nack_t *nack) {
    cJSON *data = get_object(root, "data");

    memset(nack, 0, sizeof(*nack));
    nack->code = get_int(root, "code", 1);
    if (data) {
        copy_string(nack->type, sizeof(nack->type), get_string(data, "type"));
        if (nack->type[0] == '\0') {
            copy_string(nack->type, sizeof(nack->type), get_string(data, "ref_type"));
        }
        copy_string(nack->command_id, sizeof(nack->command_id), get_string(data, "command_id"));
        if (nack->command_id[0] == '\0') {
            copy_string(nack->command_id, sizeof(nack->command_id), get_string(data, "ref_id"));
        }
        copy_string(nack->reason, sizeof(nack->reason), get_string(data, "reason"));
        if (nack->reason[0] == '\0') {
            copy_string(nack->reason, sizeof(nack->reason), get_string(data, "message"));
        }
    }
}

static void fill_servo_cmd(cJSON *root, ws_servo_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    cmd->duration_ms = 100;
    if (!data) {
        return;
    }

    cmd->has_x = get_float(data, "x_deg", &cmd->x_deg);
    cmd->has_y = get_float(data, "y_deg", &cmd->y_deg);
    cmd->duration_ms = get_int(data, "duration_ms", 100);
}

static void fill_servo_pwm_unlock_cmd(cJSON *root, ws_servo_pwm_unlock_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");
    cJSON *axes;
    cJSON *axis;

    memset(cmd, 0, sizeof(*cmd));
    if (!data) {
        return;
    }

    copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
    axes = cJSON_GetObjectItem(data, "axes");
    if (cJSON_IsArray(axes)) {
        cJSON_ArrayForEach(axis, axes) {
            const char *value = cJSON_IsString(axis) ? axis->valuestring : NULL;
            if (value == NULL || value[0] == '\0') {
                continue;
            }
            if (value[0] == 'x' || value[0] == 'X') {
                cmd->axis_mask |= 0x01u;
            } else if (value[0] == 'y' || value[0] == 'Y') {
                cmd->axis_mask |= 0x02u;
            }
        }
        return;
    }

    axis = cJSON_GetObjectItem(data, "axis");
    if (cJSON_IsString(axis) && axis->valuestring != NULL) {
        if (axis->valuestring[0] == 'x' || axis->valuestring[0] == 'X') {
            cmd->axis_mask |= 0x01u;
        } else if (axis->valuestring[0] == 'y' || axis->valuestring[0] == 'Y') {
            cmd->axis_mask |= 0x02u;
        }
    }
}

static void fill_servo_trajectory_cmd(cJSON *root, ws_servo_trajectory_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");
    cJSON *frames;
    cJSON *frame;
    int count = 0;

    memset(cmd, 0, sizeof(*cmd));
    if (!data) {
        return;
    }

    copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
    frames = cJSON_GetObjectItem(data, "frames");
    if (!cJSON_IsArray(frames)) {
        return;
    }

    cJSON_ArrayForEach(frame, frames) {
        ws_servo_trajectory_frame_t *out;

        if (!cJSON_IsObject(frame) || count >= WS_SERVO_TRAJECTORY_MAX_FRAMES) {
            continue;
        }

        out = &cmd->frames[count];
        if (!get_float(frame, "x_deg", &out->x_deg) || !get_float(frame, "y_deg", &out->y_deg)) {
            continue;
        }
        out->duration_ms = get_int(frame, "duration_ms", 80);
        out->axis_mask = 0x03u;
        out->motion_profile = 0u;
        cJSON *axes = cJSON_GetObjectItem(frame, "axes");
        if (cJSON_IsArray(axes)) {
            cJSON *axis;
            uint8_t axis_mask = 0u;
            cJSON_ArrayForEach(axis, axes) {
                const char *value = cJSON_IsString(axis) ? axis->valuestring : NULL;
                if (value == NULL || value[0] == '\0') {
                    continue;
                }
                if (value[0] == 'x' || value[0] == 'X') {
                    axis_mask |= 0x01u;
                } else if (value[0] == 'y' || value[0] == 'Y') {
                    axis_mask |= 0x02u;
                }
            }
            if (axis_mask != 0u) {
                out->axis_mask = axis_mask;
            }
        }
        {
            const char *profile = get_string(frame, "profile");
            if (profile != NULL &&
                ((profile[0] == 'e' || profile[0] == 'E') || strcmp(profile, "1") == 0)) {
                out->motion_profile = 1u;
            }
        }
        count++;
    }

    cmd->frame_count = count;
}

static void fill_motion_jog_cmd(cJSON *root, ws_motion_jog_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");
    const char *axis;
    float velocity;

    memset(cmd, 0, sizeof(*cmd));
    cmd->timeout_ms = 250;
    cmd->speed = 1.0f;
    if (!data) {
        return;
    }

    axis = get_string(data, "axis");
    cmd->is_x_axis = axis == NULL || (axis[0] != 'y' && axis[0] != 'Y');
    cmd->direction = get_int(data, "direction", 0);
    if (get_float(data, "speed", &cmd->speed) && cmd->speed < 0.0f) {
        cmd->speed = 0.0f;
    }
    if (cmd->speed > 1.0f) {
        cmd->speed = 1.0f;
    }
    if (get_float(data, "velocity_deg_per_sec", &velocity)) {
        cmd->velocity_deg_per_sec = (int)velocity;
    }
    cmd->timeout_ms = get_int(data, "timeout_ms", 250);
}

static void fill_microphone_cmd(cJSON *root, ws_microphone_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    if (data != NULL) {
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
    }
}

static void fill_state_cmd(cJSON *root, ws_state_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    if (data != NULL) {
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
        copy_string(cmd->state_id, sizeof(cmd->state_id), get_string(data, "state_id"));
    }
}

static void fill_sound_cmd(cJSON *root, ws_sound_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    if (data != NULL) {
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
        copy_string(cmd->sound_id, sizeof(cmd->sound_id), get_string(data, "sound_id"));
        if (cmd->sound_id[0] == '\0') {
            copy_string(cmd->sound_id, sizeof(cmd->sound_id), get_string(data, "sound_file"));
        }
        cmd->delay_ms = get_int(data, "delay_ms", 0);
    }
}

static void fill_light_cmd(cJSON *root, ws_light_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    cmd->brightness = 255;
    cmd->green = 255;
    if (data != NULL) {
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
        copy_string(cmd->mode, sizeof(cmd->mode), get_string(data, "mode"));
        copy_string(cmd->effect, sizeof(cmd->effect), get_string(data, "effect"));
        copy_string(cmd->zone, sizeof(cmd->zone), get_string(data, "zone"));
        cmd->red = get_int(data, "red", 0);
        cmd->green = get_int(data, "green", 255);
        cmd->blue = get_int(data, "blue", 0);
        cmd->secondary_red = get_int(data, "secondary_red", 0);
        cmd->secondary_green = get_int(data, "secondary_green", 0);
        cmd->secondary_blue = get_int(data, "secondary_blue", 0);
        cmd->brightness = get_int(data, "brightness", 255);
        cmd->period_ms = get_int(data, "period_ms", get_int(data, "hold_ms", 0));
        cmd->repeat_count = get_int(data, "repeat_count", 0);
    }
}

static void fill_text_event(cJSON *root, ws_text_event_t *event) {
    cJSON *data = get_object(root, "data");

    memset(event, 0, sizeof(*event));
    if (data) {
        copy_string(event->text, sizeof(event->text), get_string(data, "text"));
    }
}

static void fill_ai_status(cJSON *root, ws_ai_status_t *event) {
    cJSON *data = get_object(root, "data");

    memset(event, 0, sizeof(*event));
    if (data) {
        copy_string(event->status, sizeof(event->status), get_string(data, "status"));
        copy_string(event->message, sizeof(event->message), get_string(data, "message"));
        copy_string(event->image_name, sizeof(event->image_name), get_string(data, "image_name"));
        copy_string(event->action_file, sizeof(event->action_file), get_string(data, "action_file"));
        copy_string(event->sound_file, sizeof(event->sound_file), get_string(data, "sound_file"));
    }
}

static void fill_ai_thinking(cJSON *root, ws_ai_thinking_t *event) {
    cJSON *data = get_object(root, "data");

    memset(event, 0, sizeof(*event));
    if (data) {
        copy_string(event->kind, sizeof(event->kind), get_string(data, "kind"));
        copy_string(event->content, sizeof(event->content), get_string(data, "content"));
    }
}

static void fill_capture_cmd(cJSON *root, const char *action, ws_capture_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    copy_string(cmd->action, sizeof(cmd->action), action);
    if (data) {
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
        cmd->width = get_int(data, "width", 0);
        cmd->height = get_int(data, "height", 0);
        cmd->fps = get_int(data, "fps", 0);
        cmd->quality = get_int(data, "quality", 0);
    }
}

static void fill_transfer_cmd(cJSON *root, const char *message_type, ws_transfer_cmd_t *cmd) {
    cJSON *data = get_object(root, "data");

    memset(cmd, 0, sizeof(*cmd));
    copy_string(cmd->message_type, sizeof(cmd->message_type), message_type);
    if (data) {
        copy_string(cmd->transfer_id, sizeof(cmd->transfer_id), get_string(data, "transfer_id"));
        copy_string(cmd->command_id, sizeof(cmd->command_id), get_string(data, "command_id"));
        if (cmd->transfer_id[0] == '\0') {
            copy_string(cmd->transfer_id, sizeof(cmd->transfer_id), cmd->command_id);
        }
        copy_string(cmd->app_id, sizeof(cmd->app_id), get_string(data, "app_id"));
        if (cmd->app_id[0] == '\0') {
            copy_string(cmd->app_id, sizeof(cmd->app_id), get_string(data, "appId"));
        }
        copy_string(cmd->name, sizeof(cmd->name), get_string(data, "name"));
        copy_string(cmd->version, sizeof(cmd->version), get_string(data, "version"));
        copy_string(cmd->description, sizeof(cmd->description), get_string(data, "description"));
        copy_string(cmd->package_type, sizeof(cmd->package_type), get_string(data, "type"));
        if (cmd->package_type[0] == '\0') {
            copy_string(cmd->package_type, sizeof(cmd->package_type), get_string(data, "package_type"));
        }
        copy_string(cmd->source_url, sizeof(cmd->source_url), get_string(data, "source_url"));
        if (cmd->source_url[0] == '\0') {
            copy_string(cmd->source_url, sizeof(cmd->source_url), get_string(data, "url"));
        }
        if (cmd->source_url[0] == '\0') {
            copy_string(cmd->source_url, sizeof(cmd->source_url), get_string(data, "firmware_url"));
        }
        copy_string(cmd->image_version, sizeof(cmd->image_version), get_string(data, "image_version"));
        if (cmd->image_version[0] == '\0') {
            copy_string(cmd->image_version, sizeof(cmd->image_version), get_string(data, "imageVersion"));
        }
        copy_string(cmd->file_name, sizeof(cmd->file_name), get_string(data, "file_name"));
        copy_string(cmd->sha256, sizeof(cmd->sha256), get_string(data, "sha256"));
        cmd->size_bytes = (size_t)get_int_any(data, "size_bytes", "sizeBytes", 0);
        copy_string(cmd->reason, sizeof(cmd->reason), get_string(data, "reason"));
    }
}

void ws_router_init(ws_router_t *router) {
    if (router) {
        g_router = *router;
    } else {
        memset(&g_router, 0, sizeof(g_router));
    }
}

ws_msg_type_t ws_route_message(const char *json_str) {
    ws_msg_type_t msg_type = WS_MSG_UNKNOWN;
    cJSON *root;
    const char *type;

    if (!json_str) {
        return WS_MSG_UNKNOWN;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        return WS_MSG_UNKNOWN;
    }

    type = get_string(root, "type");
    if (!type) {
        cJSON_Delete(root);
        return WS_MSG_UNKNOWN;
    }

    if (strcmp(type, "sys.ack") == 0) {
        msg_type = WS_MSG_SYS_ACK;
        if (g_router.on_sys_ack) {
            ws_sys_ack_t ack;
            fill_sys_ack(root, &ack);
            g_router.on_sys_ack(&ack);
        }
    } else if (strcmp(type, "sys.nack") == 0) {
        msg_type = WS_MSG_SYS_NACK;
        if (g_router.on_sys_nack) {
            ws_sys_nack_t nack;
            fill_sys_nack(root, &nack);
            g_router.on_sys_nack(&nack);
        }
    } else if (strcmp(type, "sys.ping") == 0) {
        msg_type = WS_MSG_SYS_PING;
        if (g_router.on_sys_ping) {
            g_router.on_sys_ping();
        }
    } else if (strcmp(type, "sys.pong") == 0) {
        msg_type = WS_MSG_SYS_PONG;
        if (g_router.on_sys_pong) {
            g_router.on_sys_pong();
        }
    } else if (strcmp(type, "sys.session.resume") == 0) {
        msg_type = WS_MSG_SYS_SESSION_RESUME;
        if (g_router.on_session_resume) {
            g_router.on_session_resume();
        }
    } else if (strcmp(type, "ctrl.servo.angle") == 0) {
        msg_type = WS_MSG_CTRL_SERVO_ANGLE;
        if (g_router.on_servo) {
            ws_servo_cmd_t cmd;
            fill_servo_cmd(root, &cmd);
            g_router.on_servo(&cmd);
        }
    } else if (strcmp(type, "ctrl.servo.pwm.unlock") == 0) {
        msg_type = WS_MSG_CTRL_SERVO_PWM_UNLOCK;
        if (g_router.on_servo_pwm_unlock) {
            ws_servo_pwm_unlock_cmd_t cmd;
            fill_servo_pwm_unlock_cmd(root, &cmd);
            g_router.on_servo_pwm_unlock(&cmd);
        }
    } else if (strcmp(type, "ctrl.servo.pwm.lock") == 0) {
        msg_type = WS_MSG_CTRL_SERVO_PWM_LOCK;
        if (g_router.on_servo_pwm_lock) {
            ws_servo_pwm_unlock_cmd_t cmd;
            fill_servo_pwm_unlock_cmd(root, &cmd);
            g_router.on_servo_pwm_lock(&cmd);
        }
    } else if (strcmp(type, "ctrl.servo.trajectory.play") == 0) {
        msg_type = WS_MSG_CTRL_SERVO_TRAJECTORY_PLAY;
        if (g_router.on_servo_trajectory_play) {
            ws_servo_trajectory_cmd_t cmd;
            fill_servo_trajectory_cmd(root, &cmd);
            g_router.on_servo_trajectory_play(&cmd);
        }
    } else if (strcmp(type, "ctrl.light.set") == 0) {
        msg_type = WS_MSG_CTRL_LIGHT_SET;
        if (g_router.on_light_set) {
            ws_light_cmd_t cmd;
            fill_light_cmd(root, &cmd);
            g_router.on_light_set(&cmd);
        }
    } else if (strcmp(type, "ctrl.motion.jog") == 0) {
        msg_type = WS_MSG_CTRL_MOTION_JOG;
        if (g_router.on_motion_jog) {
            ws_motion_jog_cmd_t cmd;
            fill_motion_jog_cmd(root, &cmd);
            g_router.on_motion_jog(&cmd);
        }
    } else if (strcmp(type, "ctrl.motion.stop") == 0) {
        msg_type = WS_MSG_CTRL_MOTION_STOP;
        if (g_router.on_motion_stop) {
            g_router.on_motion_stop();
        }
    } else if (strcmp(type, "ctrl.microphone.open") == 0) {
        msg_type = WS_MSG_CTRL_MICROPHONE_OPEN;
        if (g_router.on_microphone_open) {
            ws_microphone_cmd_t cmd;
            fill_microphone_cmd(root, &cmd);
            g_router.on_microphone_open(&cmd);
        }
    } else if (strcmp(type, "ctrl.microphone.close") == 0) {
        msg_type = WS_MSG_CTRL_MICROPHONE_CLOSE;
        if (g_router.on_microphone_close) {
            ws_microphone_cmd_t cmd;
            fill_microphone_cmd(root, &cmd);
            g_router.on_microphone_close(&cmd);
        }
    } else if (strcmp(type, "ctrl.robot.state.set") == 0) {
        msg_type = WS_MSG_CTRL_ROBOT_STATE_SET;
        if (g_router.on_state_set) {
            ws_state_cmd_t cmd;
            fill_state_cmd(root, &cmd);
            g_router.on_state_set(&cmd);
        }
    } else if (strcmp(type, "ctrl.sound.play") == 0) {
        msg_type = WS_MSG_CTRL_SOUND_PLAY;
        if (g_router.on_sound_play) {
            ws_sound_cmd_t cmd;
            fill_sound_cmd(root, &cmd);
            g_router.on_sound_play(&cmd);
        }
    } else if (strcmp(type, "ctrl.camera.video_config") == 0) {
        msg_type = WS_MSG_CTRL_CAMERA_VIDEO_CONFIG;
        if (g_router.on_capture) {
            ws_capture_cmd_t cmd;
            fill_capture_cmd(root, "config", &cmd);
            g_router.on_capture(&cmd);
        }
    } else if (strcmp(type, "ctrl.camera.capture_image") == 0) {
        msg_type = WS_MSG_CTRL_CAMERA_CAPTURE_IMAGE;
        if (g_router.on_capture) {
            ws_capture_cmd_t cmd;
            fill_capture_cmd(root, "single", &cmd);
            g_router.on_capture(&cmd);
        }
    } else if (strcmp(type, "ctrl.camera.start_video") == 0) {
        msg_type = WS_MSG_CTRL_CAMERA_START_VIDEO;
        if (g_router.on_capture) {
            ws_capture_cmd_t cmd;
            fill_capture_cmd(root, "start", &cmd);
            g_router.on_capture(&cmd);
        }
    } else if (strcmp(type, "ctrl.camera.stop_video") == 0) {
        msg_type = WS_MSG_CTRL_CAMERA_STOP_VIDEO;
        if (g_router.on_capture) {
            ws_capture_cmd_t cmd;
            fill_capture_cmd(root, "stop", &cmd);
            g_router.on_capture(&cmd);
        }
    } else if (strcmp(type, "evt.asr.result") == 0) {
        msg_type = WS_MSG_EVT_ASR_RESULT;
        if (g_router.on_asr_result) {
            ws_text_event_t event;
            fill_text_event(root, &event);
            g_router.on_asr_result(&event);
        }
    } else if (strcmp(type, "evt.ai.status") == 0) {
        msg_type = WS_MSG_EVT_AI_STATUS;
        if (g_router.on_ai_status) {
            ws_ai_status_t event;
            fill_ai_status(root, &event);
            g_router.on_ai_status(&event);
        }
    } else if (strcmp(type, "evt.ai.thinking") == 0) {
        msg_type = WS_MSG_EVT_AI_THINKING;
        if (g_router.on_ai_thinking) {
            ws_ai_thinking_t event;
            fill_ai_thinking(root, &event);
            g_router.on_ai_thinking(&event);
        }
    } else if (strcmp(type, "evt.ai.reply") == 0) {
        msg_type = WS_MSG_EVT_AI_REPLY;
        if (g_router.on_ai_reply) {
            ws_text_event_t event;
            fill_text_event(root, &event);
            g_router.on_ai_reply(&event);
        }
    } else if (strcmp(type, "xfer.ota.handshake") == 0) {
        msg_type = WS_MSG_XFER_OTA_HANDSHAKE;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "xfer.ota.checksum") == 0) {
        msg_type = WS_MSG_XFER_OTA_CHECKSUM;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.transfer.begin") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_TRANSFER_BEGIN;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.transfer.commit") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_TRANSFER_COMMIT;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.transfer.abort") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_TRANSFER_ABORT;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.list") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_LIST;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.install") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_INSTALL;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.open") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_OPEN;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    } else if (strcmp(type, "app.package.uninstall") == 0) {
        msg_type = WS_MSG_APP_PACKAGE_UNINSTALL;
        if (g_router.on_transfer) {
            ws_transfer_cmd_t cmd;
            fill_transfer_cmd(root, type, &cmd);
            g_router.on_transfer(&cmd);
        }
    }

    cJSON_Delete(root);
    return msg_type;
}
