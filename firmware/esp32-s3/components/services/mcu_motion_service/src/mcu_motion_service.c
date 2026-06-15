#include "mcu_motion_service.h"

#include "esp_log.h"
#include "mcu_link_bootstrap.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "MCU_MOTION";
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
static const char *OBS_TAG = "MCU_OBS";
#endif

static mcu_motion_request_t s_last_request;
static bool s_has_last_request;
static uint32_t s_last_command_seq;
static bool s_command_inflight;

static uint16_t decode_u16_le(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

static uint32_t decode_u32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) | ((uint32_t)src[3] << 24u);
}

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
static int16_t decode_i16_le(const uint8_t *src) {
    return (int16_t)decode_u16_le(src);
}
#endif

static void encode_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void encode_i16_le(uint8_t *dst, int16_t value) {
    encode_u16_le(dst, (uint16_t)value);
}

static esp_err_t mcu_motion_submit_runtime_frame(const mcu_motion_request_t *request, uint32_t *out_seq) {
    uint8_t payload[9];
    mcu_link_t *link;
    uint32_t seq = 0u;
    size_t wire_len = 0u;
    esp_err_t ret;

    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    link = mcu_link_bootstrap_get_link();
    if (link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_bootstrap_is_ready()) {
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting motion request");
        return ESP_ERR_INVALID_STATE;
    }

    payload[0] = request->axis_mask;
    encode_i16_le(&payload[1], request->x_deg_x10);
    encode_i16_le(&payload[3], request->y_deg_x10);
    encode_u16_le(&payload[5], request->duration_ms);
    payload[7] = request->motion_profile;
    payload[8] = (uint8_t)request->source;

    ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_MOTION, MCU_MOTION_MSG_SERVO_MOVE, MCU_FRAME_FLAG_ACK_REQ, payload,
                              (uint16_t)sizeof(payload), &seq, &wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_MOVE frame: %s", esp_err_to_name(ret));
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued SERVO_MOVE frame seq=%lu wire_len=%u axis_mask=0x%02x duration_ms=%u", (unsigned long)seq,
             (unsigned)wire_len, request->axis_mask, (unsigned)request->duration_ms);
#endif
    s_last_command_seq = seq;
    s_command_inflight = true;
    if (out_seq != NULL) {
        *out_seq = seq;
    }
    return ESP_OK;
}

static esp_err_t mcu_motion_submit_stop_frame(mcu_motion_source_t source) {
    uint8_t payload[2];
    mcu_link_t *link;
    uint32_t seq = 0u;
    size_t wire_len = 0u;
    esp_err_t ret;

    link = mcu_link_bootstrap_get_link();
    if (link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_bootstrap_is_ready()) {
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting stop request");
        return ESP_ERR_INVALID_STATE;
    }

    payload[0] = 1u; /* all_pending: hal_servo_cancel_all() must clear the STM32 queue */
    payload[1] = (uint8_t)source;
    ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_MOTION, MCU_MOTION_MSG_SERVO_STOP, MCU_FRAME_FLAG_ACK_REQ, payload,
                              (uint16_t)sizeof(payload), &seq, &wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_STOP frame: %s", esp_err_to_name(ret));
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued SERVO_STOP frame seq=%lu wire_len=%u scope=all_pending source=%u", (unsigned long)seq,
             (unsigned)wire_len, (unsigned)source);
#endif
    s_last_command_seq = seq;
    s_command_inflight = true;
    return ESP_OK;
}

static esp_err_t mcu_motion_submit_jog_frame(const mcu_motion_jog_request_t *request, uint32_t *out_seq) {
    uint8_t payload[12];
    mcu_link_t *link;
    uint32_t seq = 0u;
    size_t wire_len = 0u;
    esp_err_t ret;

    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    link = mcu_link_bootstrap_get_link();
    if (link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_bootstrap_is_ready()) {
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting jog request");
        return ESP_ERR_INVALID_STATE;
    }

    payload[0] = request->axis_mask;
    encode_i16_le(&payload[1], request->x_velocity_deg_x10_per_sec);
    encode_i16_le(&payload[3], request->y_velocity_deg_x10_per_sec);
    encode_u16_le(&payload[5], request->timeout_ms);
    payload[7] = (uint8_t)request->source;
    payload[8] = request->x_min_deg;
    payload[9] = request->x_max_deg;
    payload[10] = request->y_min_deg;
    payload[11] = request->y_max_deg;

    ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_MOTION, MCU_MOTION_MSG_SERVO_JOG, MCU_FRAME_FLAG_ACK_REQ, payload,
                              (uint16_t)sizeof(payload), &seq, &wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_JOG frame: %s", esp_err_to_name(ret));
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued SERVO_JOG frame seq=%lu wire_len=%u axis_mask=0x%02x timeout_ms=%u", (unsigned long)seq,
             (unsigned)wire_len, request->axis_mask, (unsigned)request->timeout_ms);
#endif
    s_last_command_seq = seq;
    s_command_inflight = true;
    if (out_seq != NULL) {
        *out_seq = seq;
    }
    return ESP_OK;
}

static esp_err_t mcu_motion_submit_sequence_frame(const mcu_motion_sequence_t *sequence, uint32_t *out_seq) {
    uint8_t payload[2u + (MCU_MOTION_SEQUENCE_MAX_SEGMENTS * 8u)];
    mcu_link_t *link;
    uint32_t seq = 0u;
    size_t wire_len = 0u;
    uint16_t payload_len;
    esp_err_t ret;

    if (sequence == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    link = mcu_link_bootstrap_get_link();
    if (link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_bootstrap_is_ready()) {
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting motion sequence request");
        return ESP_ERR_INVALID_STATE;
    }

    payload[0] = (uint8_t)sequence->source;
    payload[1] = sequence->segment_count;
    for (uint8_t index = 0u; index < sequence->segment_count; index++) {
        uint16_t offset = (uint16_t)(2u + ((uint16_t)index * 8u));
        const mcu_motion_segment_t *segment = &sequence->segments[index];

        payload[offset] = segment->axis_mask;
        encode_i16_le(&payload[offset + 1u], segment->x_deg_x10);
        encode_i16_le(&payload[offset + 3u], segment->y_deg_x10);
        encode_u16_le(&payload[offset + 5u], segment->duration_ms);
        payload[offset + 7u] = segment->motion_profile;
    }
    payload_len = (uint16_t)(2u + ((uint16_t)sequence->segment_count * 8u));

    ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_MOTION, MCU_MOTION_MSG_SERVO_SEQUENCE, MCU_FRAME_FLAG_ACK_REQ,
                              payload, payload_len, &seq, &wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_SEQUENCE frame: %s", esp_err_to_name(ret));
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued SERVO_SEQUENCE frame seq=%lu wire_len=%u segments=%u source=%u", (unsigned long)seq,
             (unsigned)wire_len, (unsigned)sequence->segment_count, (unsigned)sequence->source);
#endif
    s_last_command_seq = seq;
    s_command_inflight = true;
    if (out_seq != NULL) {
        *out_seq = seq;
    }
    return ESP_OK;
}

static esp_err_t mcu_motion_send_chunked_sequence_control_frame(mcu_link_t *link, uint8_t msg_id,
                                                                const uint8_t *payload, uint16_t payload_len,
                                                                uint32_t *out_seq) {
    size_t wire_len = 0u;

    return mcu_link_send_frame(link, MCU_FRAME_CLASS_MOTION, msg_id, MCU_FRAME_FLAG_ACK_REQ, payload, payload_len,
                               out_seq, &wire_len);
}

static esp_err_t mcu_motion_submit_chunked_sequence_frames(const mcu_motion_chunked_sequence_t *sequence,
                                                          uint32_t *out_end_seq) {
    uint8_t payload[4u + (MCU_MOTION_SEQUENCE_CHUNK_MAX_SEGMENTS * 8u)];
    mcu_link_t *link;
    uint8_t next_index = 0u;
    uint32_t end_seq = 0u;
    esp_err_t ret;

    if (sequence == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    link = mcu_link_bootstrap_get_link();
    if (link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_bootstrap_is_ready()) {
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting chunked motion sequence request");
        return ESP_ERR_INVALID_STATE;
    }

    encode_u16_le(&payload[0], sequence->sequence_id);
    payload[2] = (uint8_t)sequence->source;
    payload[3] = sequence->segment_count;
    ret = mcu_motion_send_chunked_sequence_control_frame(link, MCU_MOTION_MSG_SERVO_SEQUENCE_BEGIN, payload, 4u, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_SEQUENCE_BEGIN frame: %s", esp_err_to_name(ret));
        return ret;
    }

    while (next_index < sequence->segment_count) {
        uint8_t chunk_count = (uint8_t)(sequence->segment_count - next_index);
        uint16_t payload_len;

        if (chunk_count > MCU_MOTION_SEQUENCE_CHUNK_MAX_SEGMENTS) {
            chunk_count = MCU_MOTION_SEQUENCE_CHUNK_MAX_SEGMENTS;
        }

        encode_u16_le(&payload[0], sequence->sequence_id);
        payload[2] = next_index;
        payload[3] = chunk_count;
        for (uint8_t index = 0u; index < chunk_count; index++) {
            uint16_t offset = (uint16_t)(4u + ((uint16_t)index * 8u));
            const mcu_motion_segment_t *segment = &sequence->segments[next_index + index];

            payload[offset] = segment->axis_mask;
            encode_i16_le(&payload[offset + 1u], segment->x_deg_x10);
            encode_i16_le(&payload[offset + 3u], segment->y_deg_x10);
            encode_u16_le(&payload[offset + 5u], segment->duration_ms);
            payload[offset + 7u] = segment->motion_profile;
        }
        payload_len = (uint16_t)(4u + ((uint16_t)chunk_count * 8u));
        ret = mcu_motion_send_chunked_sequence_control_frame(link, MCU_MOTION_MSG_SERVO_SEQUENCE_CHUNK, payload,
                                                             payload_len, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to queue SERVO_SEQUENCE_CHUNK frame: %s", esp_err_to_name(ret));
            return ret;
        }
        next_index = (uint8_t)(next_index + chunk_count);
    }

    encode_u16_le(&payload[0], sequence->sequence_id);
    ret = mcu_motion_send_chunked_sequence_control_frame(link, MCU_MOTION_MSG_SERVO_SEQUENCE_END, payload, 2u,
                                                         &end_seq);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue SERVO_SEQUENCE_END frame: %s", esp_err_to_name(ret));
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ESP_LOGI(TAG, "Queued chunked SERVO_SEQUENCE seq=%lu id=%u segments=%u source=%u", (unsigned long)end_seq,
             (unsigned)sequence->sequence_id, (unsigned)sequence->segment_count, (unsigned)sequence->source);
#endif
    s_last_command_seq = end_seq;
    s_command_inflight = true;
    if (out_end_seq != NULL) {
        *out_end_seq = end_seq;
    }
    return ESP_OK;
}

static bool mcu_motion_request_is_valid(const mcu_motion_request_t *request) {
    if (request == NULL) {
        return false;
    }

    if (request->axis_mask == 0U) {
        return false;
    }

    if ((request->axis_mask & ~(MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y)) != 0U) {
        return false;
    }

    if (request->duration_ms == 0U) {
        return false;
    }

    if (request->motion_profile != MCU_MOTION_PROFILE_LINEAR &&
        request->motion_profile != MCU_MOTION_PROFILE_EASE_IN_OUT) {
        return false;
    }

    if (request->source < MCU_MOTION_SOURCE_UNKNOWN || request->source > MCU_MOTION_SOURCE_RECOVERY) {
        return false;
    }

    return true;
}

static bool mcu_motion_segment_is_valid(const mcu_motion_segment_t *segment) {
    if (segment == NULL) {
        return false;
    }
    if (segment->axis_mask == 0U || (segment->axis_mask & ~(MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y)) != 0U) {
        return false;
    }
    if (segment->duration_ms == 0U) {
        return false;
    }
    if (segment->motion_profile != MCU_MOTION_PROFILE_LINEAR &&
        segment->motion_profile != MCU_MOTION_PROFILE_EASE_IN_OUT) {
        return false;
    }
    if ((segment->axis_mask & MCU_MOTION_AXIS_X) != 0U &&
        (segment->x_deg_x10 < 0 || segment->x_deg_x10 > 1800)) {
        return false;
    }
    if ((segment->axis_mask & MCU_MOTION_AXIS_Y) != 0U &&
        (segment->y_deg_x10 < 0 || segment->y_deg_x10 > 1800)) {
        return false;
    }
    return true;
}

static bool mcu_motion_sequence_is_valid(const mcu_motion_sequence_t *sequence) {
    if (sequence == NULL) {
        return false;
    }
    if (sequence->source < MCU_MOTION_SOURCE_UNKNOWN || sequence->source > MCU_MOTION_SOURCE_RECOVERY) {
        return false;
    }
    if (sequence->segment_count == 0U || sequence->segment_count > MCU_MOTION_SEQUENCE_MAX_SEGMENTS) {
        return false;
    }
    for (uint8_t index = 0u; index < sequence->segment_count; index++) {
        if (!mcu_motion_segment_is_valid(&sequence->segments[index])) {
            return false;
        }
    }
    return true;
}

static bool mcu_motion_chunked_sequence_is_valid(const mcu_motion_chunked_sequence_t *sequence) {
    if (sequence == NULL) {
        return false;
    }
    if (sequence->source < MCU_MOTION_SOURCE_UNKNOWN || sequence->source > MCU_MOTION_SOURCE_RECOVERY) {
        return false;
    }
    if (sequence->segment_count == 0U || sequence->segment_count > MCU_MOTION_CHUNKED_SEQUENCE_MAX_SEGMENTS) {
        return false;
    }
    for (uint8_t index = 0u; index < sequence->segment_count; index++) {
        if (!mcu_motion_segment_is_valid(&sequence->segments[index])) {
            return false;
        }
    }
    return true;
}

static bool mcu_motion_jog_request_is_valid(const mcu_motion_jog_request_t *request) {
    if (request == NULL) {
        return false;
    }
    if (request->axis_mask == 0U || (request->axis_mask & ~(MCU_MOTION_AXIS_X | MCU_MOTION_AXIS_Y)) != 0U) {
        return false;
    }
    if (request->timeout_ms == 0U) {
        return false;
    }
    if (request->x_min_deg > request->x_max_deg || request->y_min_deg > request->y_max_deg) {
        return false;
    }
    if ((request->axis_mask & MCU_MOTION_AXIS_X) != 0U && request->x_velocity_deg_x10_per_sec == 0) {
        return false;
    }
    if ((request->axis_mask & MCU_MOTION_AXIS_Y) != 0U && request->y_velocity_deg_x10_per_sec == 0) {
        return false;
    }
    if (request->source < MCU_MOTION_SOURCE_UNKNOWN || request->source > MCU_MOTION_SOURCE_RECOVERY) {
        return false;
    }
    return true;
}

esp_err_t mcu_motion_service_init(void) {
    memset(&s_last_request, 0, sizeof(s_last_request));
    s_has_last_request = false;
    s_last_command_seq = 0u;
    s_command_inflight = false;
    return ESP_OK;
}

esp_err_t mcu_motion_submit(const mcu_motion_request_t *request) {
    return mcu_motion_submit_with_seq(request, NULL);
}

esp_err_t mcu_motion_jog(const mcu_motion_jog_request_t *request) {
    return mcu_motion_jog_with_seq(request, NULL);
}

esp_err_t mcu_motion_submit_sequence(const mcu_motion_sequence_t *sequence) {
    return mcu_motion_submit_sequence_with_seq(sequence, NULL);
}

esp_err_t mcu_motion_submit_chunked_sequence(const mcu_motion_chunked_sequence_t *sequence) {
    return mcu_motion_submit_chunked_sequence_with_seq(sequence, NULL);
}

esp_err_t mcu_motion_submit_with_seq(const mcu_motion_request_t *request, uint32_t *out_seq) {
    if (!mcu_motion_request_is_valid(request)) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        esp_err_t ret = mcu_motion_submit_runtime_frame(request, out_seq);
        if (ret != ESP_OK) {
            return ret;
        }

        s_last_request = *request;
        s_has_last_request = true;
        return ESP_OK;
    }
}

esp_err_t mcu_motion_jog_with_seq(const mcu_motion_jog_request_t *request, uint32_t *out_seq) {
    if (!mcu_motion_jog_request_is_valid(request)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcu_motion_submit_jog_frame(request, out_seq);
}

esp_err_t mcu_motion_submit_sequence_with_seq(const mcu_motion_sequence_t *sequence, uint32_t *out_seq) {
    if (!mcu_motion_sequence_is_valid(sequence)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcu_motion_submit_sequence_frame(sequence, out_seq);
}

esp_err_t mcu_motion_submit_chunked_sequence_with_seq(const mcu_motion_chunked_sequence_t *sequence,
                                                      uint32_t *out_end_seq) {
    if (!mcu_motion_chunked_sequence_is_valid(sequence)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcu_motion_submit_chunked_sequence_frames(sequence, out_end_seq);
}

esp_err_t mcu_motion_service_get_last_request(mcu_motion_request_t *out_request) {
    if (out_request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_has_last_request) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_request = s_last_request;
    return ESP_OK;
}

esp_err_t mcu_motion_stop(mcu_motion_source_t source) {
    if (source < MCU_MOTION_SOURCE_UNKNOWN || source > MCU_MOTION_SOURCE_RECOVERY) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcu_motion_submit_stop_frame(source);
}

esp_err_t mcu_motion_service_handle_link_event(const mcu_link_event_t *event) {
    uint32_t ref_seq;

    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (event->type) {
    case MCU_LINK_RX_EVENT_ACK:
        ref_seq = decode_u32_le(event->frame.payload);
        if (s_command_inflight && ref_seq == s_last_command_seq) {
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
            ESP_LOGI(TAG, "Motion ACK ref_seq=%lu status=%u", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[4]));
#endif
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_NACK:
        ref_seq = decode_u32_le(event->frame.payload);
        if (s_command_inflight && ref_seq == s_last_command_seq) {
            ESP_LOGW(TAG, "Motion NACK ref_seq=%lu reason=0x%04x", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[6]));
            s_command_inflight = false;
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_FAULT:
        ref_seq = decode_u32_le(event->frame.payload);
        if (event->frame.payload[4] == 0x01u &&
            (!s_command_inflight || ref_seq == s_last_command_seq || ref_seq == 0u)) {
            ESP_LOGW(TAG, "Motion FAULT ref_seq=%lu fault_code=0x%04x detail=0x%04x", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[5]),
                     (unsigned)decode_u16_le(&event->frame.payload[7]));
            s_command_inflight = false;
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_MOTION_DONE:
        ref_seq = decode_u32_le(event->frame.payload);
        if (!s_command_inflight || ref_seq == s_last_command_seq) {
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
            ESP_LOGI(TAG, "Motion DONE ref_seq=%lu result=%u final=(%d,%d) exec_ms=%u", (unsigned long)ref_seq,
                     (unsigned)event->frame.payload[4], (int)decode_i16_le(&event->frame.payload[5]),
                     (int)decode_i16_le(&event->frame.payload[7]), (unsigned)decode_u16_le(&event->frame.payload[9]));
            ESP_LOGI(OBS_TAG,
                     "evt=motion_done ref_seq=%lu msg_class=%u msg_id=%u result=%u final_x=%d final_y=%d "
                     "exec_ms=%u",
                     (unsigned long)ref_seq, (unsigned)MCU_FRAME_CLASS_MOTION, (unsigned)MCU_MOTION_MSG_MOTION_DONE,
                     (unsigned)event->frame.payload[4], (int)decode_i16_le(&event->frame.payload[5]),
                     (int)decode_i16_le(&event->frame.payload[7]), (unsigned)decode_u16_le(&event->frame.payload[9]));
#endif
            s_command_inflight = false;
        }
        return ESP_OK;
    default:
        return ESP_ERR_NOT_FOUND;
    }
}
