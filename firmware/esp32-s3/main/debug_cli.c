#include "debug_cli.h"

#include "behavior_action_parser.h"
#include "behavior_state_service.h"
#include "behavior_types.h"
#include "control_ingress.h"
#include "debug_touch_guard.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mcu_motion_service.h"
#include "sdkconfig.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "DEBUG_CLI"

#define DEBUG_CLI_TASK_STACK 4096
#define DEBUG_CLI_TASK_PRIORITY 3
#define DEBUG_CLI_LINE_MAX 192
#define DEBUG_CLI_ARG_MAX 8
#define DEBUG_CLI_CENTER_X_DEG 90
#define DEBUG_CLI_CENTER_Y_DEG 120
#define DEBUG_CLI_CENTER_DURATION_MS 300
#define DEBUG_CLI_ACTION_FILE_MAX 32768
#define DEBUG_CLI_ACTION_PATH_MAX 128
#define DEBUG_CLI_TOUCH_SUPPRESS_MS 10000U
#define DEBUG_CLI_PRE_ACTION_STOP_SETTLE_MS 150U

static TaskHandle_t s_debug_cli_task = NULL;
static uint16_t s_debug_sequence_id = 1;

static void debug_cli_copy(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool debug_cli_parse_int(const char *value, int *out_value) {
    char *end = NULL;
    long parsed;

    if (value == NULL || value[0] == '\0' || out_value == NULL) {
        return false;
    }

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return false;
    }

    *out_value = (int)parsed;
    return true;
}

static bool debug_cli_is_safe_action_id(const char *action_id) {
    size_t index;

    if (action_id == NULL || action_id[0] == '\0') {
        return false;
    }

    for (index = 0; action_id[index] != '\0'; ++index) {
        const unsigned char ch = (unsigned char)action_id[index];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return false;
        }
    }

    return true;
}

static size_t debug_cli_tokenize(char *line, char **argv, size_t argv_max) {
    size_t argc = 0;
    char *cursor = line;

    if (line == NULL || argv == NULL || argv_max == 0) {
        return 0;
    }

    while (*cursor != '\0' && argc < argv_max) {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        if (*cursor == '"') {
            cursor++;
            argv[argc++] = cursor;
            while (*cursor != '\0' && *cursor != '"') {
                cursor++;
            }
            if (*cursor == '"') {
                *cursor++ = '\0';
            }
        } else {
            argv[argc++] = cursor;
            while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0') {
                *cursor++ = '\0';
            }
        }
    }

    return argc;
}

static void debug_cli_log_result(const char *command, esp_err_t ret) {
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s accepted", command);
    } else {
        ESP_LOGW(TAG, "%s rejected: %s", command, esp_err_to_name(ret));
    }
}

static void debug_cli_handle_status(void) {
    ESP_LOGI(TAG, "evt=debug_status behavior=%s busy=%d action_active=%d heap_free=%lu internal_free=%lu",
             behavior_state_get_current(), behavior_state_is_busy() ? 1 : 0,
             behavior_state_is_action_active() ? 1 : 0, (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void debug_cli_prepare_motion_debug_window(void) {
    debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    (void)control_ingress_stop_manual(CONTROL_MOTION_SOURCE_WS);
    vTaskDelay(pdMS_TO_TICKS(DEBUG_CLI_PRE_ACTION_STOP_SETTLE_MS));
}

static void debug_cli_handle_behavior(size_t argc, char **argv) {
    control_state_set_request_t request = {0};
    esp_err_t ret;

    if (argc < 2) {
        ESP_LOGW(TAG, "debug.behavior requires state_id");
        return;
    }

    debug_cli_copy(request.state_id, sizeof(request.state_id), argv[1]);
    ret = control_ingress_submit_state_set(&request);
    debug_cli_log_result("debug.behavior", ret);
}

static void debug_cli_handle_action(size_t argc, char **argv) {
    const char *action_id;
    const char *state_id;
    esp_err_t ret;

    if (argc < 2) {
        ESP_LOGW(TAG, "debug.action requires action_id [state_id]");
        return;
    }

    action_id = argv[1];
    state_id = argc >= 3 ? argv[2] : action_id;
    debug_cli_prepare_motion_debug_window();
    ret = behavior_state_set_with_resources_and_action(state_id, NULL, 0, NULL, "", action_id);
    if (ret == ESP_ERR_NOT_FOUND && strcmp(state_id, "standby") != 0) {
        ret = behavior_state_set_with_resources_and_action("standby", NULL, 0, NULL, "", action_id);
    }
    debug_cli_log_result("debug.action", ret);
}

static void debug_cli_handle_ai_status(size_t argc, char **argv) {
    control_ai_status_request_t request = {0};
    esp_err_t ret;

    if (argc < 2) {
        ESP_LOGW(TAG, "debug.ai_status requires status [message] [action] [sound]");
        return;
    }

    debug_cli_copy(request.status, sizeof(request.status), argv[1]);
    debug_cli_copy(request.message, sizeof(request.message), argc >= 3 ? argv[2] : "");
    debug_cli_copy(request.action_file, sizeof(request.action_file), argc >= 4 ? argv[3] : "");
    debug_cli_copy(request.sound_file, sizeof(request.sound_file), argc >= 5 ? argv[4] : "");
    ret = control_ingress_submit_ai_status(&request);
    debug_cli_log_result("debug.ai_status", ret);
}

static void debug_cli_handle_motion_move(size_t argc, char **argv) {
    control_servo_request_t request = {
        .has_x = true,
        .has_y = true,
        .source = CONTROL_MOTION_SOURCE_WS,
    };
    esp_err_t ret;

    if (argc < 4 || !debug_cli_parse_int(argv[1], &request.x_deg) || !debug_cli_parse_int(argv[2], &request.y_deg) ||
        !debug_cli_parse_int(argv[3], &request.duration_ms)) {
        ESP_LOGW(TAG, "debug.motion.move requires x y duration_ms");
        return;
    }

    ret = control_ingress_submit_servo(&request);
    if (ret == ESP_OK) {
        debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    }
    debug_cli_log_result("debug.motion.move", ret);
}

static void debug_cli_handle_motion_center(void) {
    control_servo_request_t request = {
        .has_x = true,
        .has_y = true,
        .x_deg = DEBUG_CLI_CENTER_X_DEG,
        .y_deg = DEBUG_CLI_CENTER_Y_DEG,
        .duration_ms = DEBUG_CLI_CENTER_DURATION_MS,
        .source = CONTROL_MOTION_SOURCE_WS,
    };
    esp_err_t ret = control_ingress_submit_servo(&request);

    if (ret == ESP_OK) {
        debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    }
    debug_cli_log_result("debug.motion.center", ret);
}

static void debug_cli_handle_motion_jog(size_t argc, char **argv) {
    control_jog_request_t request = {
        .source = CONTROL_MOTION_SOURCE_WS,
    };
    esp_err_t ret;

    if (argc < 4 || (strcmp(argv[1], "x") != 0 && strcmp(argv[1], "y") != 0) ||
        !debug_cli_parse_int(argv[2], &request.velocity_deg_per_sec) ||
        !debug_cli_parse_int(argv[3], &request.timeout_ms)) {
        ESP_LOGW(TAG, "debug.motion.jog requires x|y velocity_deg_per_sec timeout_ms");
        return;
    }

    request.is_x_axis = strcmp(argv[1], "x") == 0;
    ret = control_ingress_submit_jog(&request);
    if (ret == ESP_OK) {
        debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    }
    debug_cli_log_result("debug.motion.jog", ret);
}

static void debug_cli_handle_motion_stop(void) {
    debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    esp_err_t ret = control_ingress_stop_manual(CONTROL_MOTION_SOURCE_WS);

    debug_cli_log_result("debug.motion.stop", ret);
}

static esp_err_t debug_cli_read_action_json(const char *action_id, char **out_json, size_t *out_len) {
    char path[DEBUG_CLI_ACTION_PATH_MAX];
    FILE *file = NULL;
    char *json = NULL;
    long file_len;
    size_t read_len;

    if (!debug_cli_is_safe_action_id(action_id) || out_json == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(path, sizeof(path), "/spiffs/actions/%s.json", action_id);
    file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    file_len = ftell(file);
    if (file_len <= 0 || file_len > DEBUG_CLI_ACTION_FILE_MAX) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(file);

    json = (char *)calloc((size_t)file_len + 1U, 1U);
    if (json == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    read_len = fread(json, 1U, (size_t)file_len, file);
    fclose(file);
    if (read_len != (size_t)file_len) {
        free(json);
        return ESP_FAIL;
    }

    *out_json = json;
    *out_len = read_len;
    return ESP_OK;
}

static uint8_t debug_cli_profile_to_mcu(uint8_t behavior_profile) {
    return behavior_profile == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT ? MCU_MOTION_PROFILE_EASE_IN_OUT
                                                                   : MCU_MOTION_PROFILE_LINEAR;
}

static esp_err_t debug_cli_append_sequence_segment(mcu_motion_segment_t *segments, uint8_t *segment_count,
                                                   uint8_t max_segments, int x_deg, int y_deg, uint32_t duration_ms,
                                                   uint8_t motion_profile) {
    mcu_motion_segment_t *segment;

    if (segments == NULL || segment_count == NULL || duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*segment_count >= max_segments) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (duration_ms > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    segment = &segments[*segment_count];
    segment->axis_mask = MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y;
    segment->x_deg_x10 = (int16_t)(x_deg * 10);
    segment->y_deg_x10 = (int16_t)(y_deg * 10);
    segment->duration_ms = (uint16_t)duration_ms;
    segment->motion_profile = motion_profile;
    (*segment_count)++;
    return ESP_OK;
}

static esp_err_t debug_cli_build_sequence_from_action(const behavior_action_def_t *action,
                                                      mcu_motion_segment_t *segments, uint8_t *segment_count,
                                                      uint8_t max_segments) {
    uint32_t cursor_ms = 0;
    int current_x = BEHAVIOR_ACTION_DEFAULT_X_DEG;
    int current_y = BEHAVIOR_ACTION_DEFAULT_Y_DEG;

    if (action == NULL || segments == NULL || segment_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *segment_count = 0;
    for (int index = 0; index < action->motion_count; ++index) {
        const behavior_motion_event_t *event = &action->motion[index];
        esp_err_t ret;

        if (event->at_ms > cursor_ms) {
            ret = debug_cli_append_sequence_segment(segments, segment_count, max_segments, current_x, current_y,
                                                    event->at_ms - cursor_ms, MCU_MOTION_PROFILE_LINEAR);
            if (ret != ESP_OK) {
                return ret;
            }
            cursor_ms = event->at_ms;
        }

        ret = debug_cli_append_sequence_segment(segments, segment_count, max_segments, event->x_deg, event->y_deg,
                                                (uint32_t)event->duration_ms,
                                                debug_cli_profile_to_mcu(event->motion_profile));
        if (ret != ESP_OK) {
            return ret;
        }
        cursor_ms = event->at_ms + (uint32_t)event->duration_ms;
        current_x = event->x_deg;
        current_y = event->y_deg;
    }

    return *segment_count > 0U ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t debug_cli_submit_sequence_segments(const mcu_motion_segment_t *segments, uint8_t segment_count) {
    if (segments == NULL || segment_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (segment_count <= MCU_MOTION_SEQUENCE_MAX_SEGMENTS) {
        mcu_motion_sequence_t sequence = {
            .source = MCU_MOTION_SOURCE_BEHAVIOR,
            .segment_count = segment_count,
        };
        memcpy(sequence.segments, segments, (size_t)segment_count * sizeof(sequence.segments[0]));
        return mcu_motion_submit_sequence(&sequence);
    }

    if (segment_count <= MCU_MOTION_CHUNKED_SEQUENCE_MAX_SEGMENTS) {
        mcu_motion_chunked_sequence_t sequence = {
            .sequence_id = s_debug_sequence_id++,
            .source = MCU_MOTION_SOURCE_BEHAVIOR,
            .segment_count = segment_count,
        };
        if (s_debug_sequence_id == 0U) {
            s_debug_sequence_id = 1U;
        }
        memcpy(sequence.segments, segments, (size_t)segment_count * sizeof(sequence.segments[0]));
        return mcu_motion_submit_chunked_sequence(&sequence);
    }

    return ESP_ERR_INVALID_SIZE;
}

static void debug_cli_handle_motion_sequence(size_t argc, char **argv) {
    char *json = NULL;
    size_t json_len = 0;
    behavior_action_def_t action = {0};
    mcu_motion_segment_t segments[MCU_MOTION_CHUNKED_SEQUENCE_MAX_SEGMENTS] = {0};
    uint8_t segment_count = 0;
    esp_err_t ret;

    if (argc < 2) {
        ESP_LOGW(TAG, "debug.motion.sequence requires action_id");
        return;
    }

    debug_cli_prepare_motion_debug_window();
    ret = debug_cli_read_action_json(argv[1], &json, &json_len);
    if (ret == ESP_OK) {
        ret = behavior_action_parse_json(argv[1], json, json_len, &action);
    }
    if (ret == ESP_OK) {
        ret = debug_cli_build_sequence_from_action(&action, segments, &segment_count,
                                                   MCU_MOTION_CHUNKED_SEQUENCE_MAX_SEGMENTS);
    }
    if (ret == ESP_OK) {
        ret = debug_cli_submit_sequence_segments(segments, segment_count);
    }

    if (ret == ESP_OK) {
        debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
        ESP_LOGI(TAG, "debug.motion.sequence accepted action=%s segments=%u", argv[1], (unsigned)segment_count);
    } else {
        ESP_LOGW(TAG, "debug.motion.sequence rejected action=%s err=%s", argv[1], esp_err_to_name(ret));
    }

    free(action.motion);
    free(json);
}

static void debug_cli_handle_line(char *line) {
    char *argv[DEBUG_CLI_ARG_MAX] = {0};
    size_t argc = debug_cli_tokenize(line, argv, DEBUG_CLI_ARG_MAX);

    if (argc == 0) {
        return;
    }

    if (strncmp(argv[0], "debug.", strlen("debug.")) == 0) {
        debug_touch_guard_suppress_for_ms(DEBUG_CLI_TOUCH_SUPPRESS_MS);
    }

    if (strcmp(argv[0], "debug.status") == 0 || strcmp(argv[0], "debug.mcu") == 0 ||
        strcmp(argv[0], "debug.heap") == 0) {
        debug_cli_handle_status();
    } else if (strcmp(argv[0], "debug.behavior") == 0) {
        debug_cli_handle_behavior(argc, argv);
    } else if (strcmp(argv[0], "debug.action") == 0) {
        debug_cli_handle_action(argc, argv);
    } else if (strcmp(argv[0], "debug.ai_status") == 0) {
        debug_cli_handle_ai_status(argc, argv);
    } else if (strcmp(argv[0], "debug.motion.move") == 0) {
        debug_cli_handle_motion_move(argc, argv);
    } else if (strcmp(argv[0], "debug.motion.center") == 0) {
        debug_cli_handle_motion_center();
    } else if (strcmp(argv[0], "debug.motion.jog") == 0) {
        debug_cli_handle_motion_jog(argc, argv);
    } else if (strcmp(argv[0], "debug.motion.stop") == 0) {
        debug_cli_handle_motion_stop();
    } else if (strcmp(argv[0], "debug.motion.sequence") == 0) {
        debug_cli_handle_motion_sequence(argc, argv);
    } else {
        ESP_LOGW(TAG, "unknown debug command: %s", argv[0]);
    }
}

static void debug_cli_task(void *arg) {
    char line[DEBUG_CLI_LINE_MAX];

    (void)arg;

    ESP_LOGI(TAG, "debug CLI ready");
    while (true) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        debug_cli_handle_line(line);
    }
}

static void debug_cli_configure_console(void) {
#if CONFIG_ESP_CONSOLE_UART
    esp_err_t ret;

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    ret = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 512, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "console UART driver install failed: %s", esp_err_to_name(ret));
    }
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
}

esp_err_t debug_cli_start(void) {
    if (s_debug_cli_task != NULL) {
        return ESP_OK;
    }

    debug_cli_configure_console();
    if (xTaskCreate(debug_cli_task, "debug_cli", DEBUG_CLI_TASK_STACK, NULL, DEBUG_CLI_TASK_PRIORITY,
                    &s_debug_cli_task) != pdPASS) {
        s_debug_cli_task = NULL;
        ESP_LOGE(TAG, "debug CLI task create failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
