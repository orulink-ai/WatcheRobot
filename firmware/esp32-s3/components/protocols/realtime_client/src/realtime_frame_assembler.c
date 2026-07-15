#include "realtime_frame_assembler.h"

#include <string.h>

void realtime_frame_assembler_init(realtime_frame_assembler_t *assembler, char *buffer, size_t capacity) {
    if (assembler == NULL) {
        return;
    }
    assembler->buffer = buffer;
    assembler->capacity = capacity;
    realtime_frame_assembler_reset(assembler);
}

void realtime_frame_assembler_reset(realtime_frame_assembler_t *assembler) {
    if (assembler == NULL) {
        return;
    }
    assembler->message_len = 0;
    assembler->frame_expected_len = 0;
    assembler->frame_received_len = 0;
    assembler->active = false;
    if (assembler->buffer != NULL && assembler->capacity > 0) {
        assembler->buffer[0] = '\0';
    }
}

esp_err_t realtime_frame_assembler_append(realtime_frame_assembler_t *assembler, const char *data, size_t data_len,
                                          size_t frame_payload_len, size_t frame_payload_offset, bool frame_fin,
                                          bool frame_starts_text, bool frame_continues_text, const char **complete_text) {
    size_t end_frame_offset;
    bool frame_complete;

    if (complete_text != NULL) {
        *complete_text = NULL;
    }
    if (assembler == NULL || assembler->buffer == NULL || assembler->capacity == 0 || data == NULL ||
        (data_len > 0 && frame_payload_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    end_frame_offset = frame_payload_offset + data_len;
    if (end_frame_offset > frame_payload_len || end_frame_offset < frame_payload_offset) {
        realtime_frame_assembler_reset(assembler);
        return ESP_ERR_INVALID_ARG;
    }

    if (frame_payload_offset == 0) {
        if (frame_starts_text) {
            assembler->message_len = 0;
            assembler->active = true;
            assembler->buffer[0] = '\0';
        } else if (frame_continues_text) {
            if (!assembler->active) {
                realtime_frame_assembler_reset(assembler);
                return ESP_ERR_INVALID_STATE;
            }
        } else {
            realtime_frame_assembler_reset(assembler);
            return ESP_ERR_INVALID_ARG;
        }
        assembler->frame_expected_len = frame_payload_len;
        assembler->frame_received_len = 0;
    } else if (!assembler->active || assembler->frame_expected_len != frame_payload_len) {
        realtime_frame_assembler_reset(assembler);
        return ESP_ERR_INVALID_STATE;
    } else if (frame_payload_offset != assembler->frame_received_len) {
        realtime_frame_assembler_reset(assembler);
        return ESP_ERR_INVALID_STATE;
    }

    if (assembler->message_len + data_len + 1U > assembler->capacity) {
        realtime_frame_assembler_reset(assembler);
        return ESP_ERR_NO_MEM;
    }

    memcpy(assembler->buffer + assembler->message_len, data, data_len);
    assembler->message_len += data_len;
    assembler->frame_received_len = end_frame_offset;
    frame_complete = end_frame_offset == frame_payload_len;
    if (frame_complete && frame_fin) {
        assembler->buffer[assembler->message_len] = '\0';
        if (complete_text != NULL) {
            *complete_text = assembler->buffer;
        }
        assembler->active = false;
        assembler->message_len = 0;
        assembler->frame_expected_len = 0;
        assembler->frame_received_len = 0;
    } else if (frame_complete) {
        assembler->frame_expected_len = 0;
        assembler->frame_received_len = 0;
    }
    return ESP_OK;
}
