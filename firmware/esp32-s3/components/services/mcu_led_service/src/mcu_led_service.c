#include "mcu_led_service.h"

#include "esp_log.h"
#include "mcu_link_bootstrap.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "MCU_LED";
static const char *OBS_TAG = "MCU_OBS";

static mcu_led_request_t s_last_request;
static bool s_has_last_request;
static uint32_t s_last_command_seq;
static bool s_command_inflight;

static uint16_t decode_u16_le(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

static uint32_t decode_u32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) | ((uint32_t)src[3] << 24u);
}

static void encode_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static bool mcu_led_request_is_valid(const mcu_led_request_t *request) {
    if (request == NULL) {
        return false;
    }

    if (request->mode > MCU_LED_MODE_EFFECT) {
        return false;
    }

    if (request->mode == MCU_LED_MODE_EFFECT) {
        if (request->effect_id < MCU_LED_EFFECT_BLINK || request->effect_id > MCU_LED_EFFECT_STATUS_PULSE) {
            return false;
        }

        if (request->period_ms == 0U) {
            return false;
        }
    }

    return true;
}

static esp_err_t mcu_led_submit_runtime_frame(const mcu_led_request_t *request) {
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
        ESP_LOGW(TAG, "MCU link not fully ready; rejecting LED request");
        return ESP_ERR_INVALID_STATE;
    }

    switch (request->mode) {
    case MCU_LED_MODE_OFF:
        ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_LED, MCU_LED_MSG_OFF, MCU_FRAME_FLAG_ACK_REQ, NULL, 0u, &seq,
                                  &wire_len);
        break;
    case MCU_LED_MODE_STATIC: {
        uint8_t payload[6];

        payload[0] = request->primary_red;
        payload[1] = request->primary_green;
        payload[2] = request->primary_blue;
        payload[3] = request->brightness;
        encode_u16_le(&payload[4], request->period_ms);
        ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_LED, MCU_LED_MSG_SET_STATIC, MCU_FRAME_FLAG_ACK_REQ, payload,
                                  (uint16_t)sizeof(payload), &seq, &wire_len);
        break;
    }
    case MCU_LED_MODE_EFFECT: {
        uint8_t payload[12];

        payload[0] = request->effect_id;
        payload[1] = request->primary_red;
        payload[2] = request->primary_green;
        payload[3] = request->primary_blue;
        payload[4] = request->secondary_red;
        payload[5] = request->secondary_green;
        payload[6] = request->secondary_blue;
        payload[7] = request->brightness;
        encode_u16_le(&payload[8], request->period_ms);
        encode_u16_le(&payload[10], request->repeat_count);
        ret = mcu_link_send_frame(link, MCU_FRAME_CLASS_LED, MCU_LED_MSG_SET_EFFECT, MCU_FRAME_FLAG_ACK_REQ, payload,
                                  (uint16_t)sizeof(payload), &seq, &wire_len);
        break;
    }
    default:
        return ESP_ERR_INVALID_STATE;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue LED frame: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Queued LED frame seq=%lu wire_len=%u mode=%u brightness=%u effect=%u", (unsigned long)seq,
             (unsigned)wire_len, (unsigned)request->mode, (unsigned)request->brightness, (unsigned)request->effect_id);
    s_last_command_seq = seq;
    s_command_inflight = true;
    return ESP_OK;
}

esp_err_t mcu_led_service_init(void) {
    memset(&s_last_request, 0, sizeof(s_last_request));
    s_has_last_request = false;
    s_last_command_seq = 0u;
    s_command_inflight = false;
    return ESP_OK;
}

esp_err_t mcu_led_submit(const mcu_led_request_t *request) {
    if (!mcu_led_request_is_valid(request)) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        esp_err_t ret = mcu_led_submit_runtime_frame(request);
        if (ret != ESP_OK) {
            return ret;
        }

        s_last_request = *request;
        s_has_last_request = true;
        return ESP_OK;
    }
}

esp_err_t mcu_led_service_get_last_request(mcu_led_request_t *out_request) {
    if (out_request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_has_last_request) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_request = s_last_request;
    return ESP_OK;
}

esp_err_t mcu_led_service_handle_link_event(const mcu_link_event_t *event) {
    uint32_t ref_seq;

    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (event->type) {
    case MCU_LINK_RX_EVENT_ACK:
        ref_seq = decode_u32_le(event->frame.payload);
        if (s_command_inflight && ref_seq == s_last_command_seq) {
            ESP_LOGI(TAG, "LED ACK ref_seq=%lu status=%u", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[4]));
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_NACK:
        ref_seq = decode_u32_le(event->frame.payload);
        if (s_command_inflight && ref_seq == s_last_command_seq) {
            ESP_LOGW(TAG, "LED NACK ref_seq=%lu reason=0x%04x", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[6]));
            s_command_inflight = false;
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_FAULT:
        ref_seq = decode_u32_le(event->frame.payload);
        if (event->frame.payload[4] == 0x02u &&
            (!s_command_inflight || ref_seq == s_last_command_seq || ref_seq == 0u)) {
            ESP_LOGW(TAG, "LED FAULT ref_seq=%lu fault_code=0x%04x detail=0x%04x", (unsigned long)ref_seq,
                     (unsigned)decode_u16_le(&event->frame.payload[5]),
                     (unsigned)decode_u16_le(&event->frame.payload[7]));
            s_command_inflight = false;
        }
        return ESP_OK;
    case MCU_LINK_RX_EVENT_LED_DONE:
        ref_seq = decode_u32_le(event->frame.payload);
        if (!s_command_inflight || ref_seq == s_last_command_seq) {
            ESP_LOGI(TAG, "LED DONE ref_seq=%lu result=%u", (unsigned long)ref_seq, (unsigned)event->frame.payload[4]);
            ESP_LOGI(OBS_TAG, "evt=led_done ref_seq=%lu msg_class=%u msg_id=%u result=%u", (unsigned long)ref_seq,
                     (unsigned)MCU_FRAME_CLASS_LED, (unsigned)MCU_LED_MSG_DONE, (unsigned)event->frame.payload[4]);
            s_command_inflight = false;
        }
        return ESP_OK;
    default:
        return ESP_ERR_NOT_FOUND;
    }
}
