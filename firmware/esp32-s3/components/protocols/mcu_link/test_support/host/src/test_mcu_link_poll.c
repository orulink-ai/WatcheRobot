#include "mcu_link.h"
#include "mcu_link_test_support.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    bool ready;
    uint8_t rx_buffer[MCU_FRAME_MAX_WIRE_SIZE * 16u];
    size_t rx_len;
    size_t rx_offset;
    uint8_t tx_buffer[MCU_FRAME_MAX_WIRE_SIZE];
    size_t tx_len;
} fake_uart_state_t;

static fake_uart_state_t s_fake_uart;

static void fake_uart_reset(void) {
    memset(&s_fake_uart, 0, sizeof(s_fake_uart));
    s_fake_uart.ready = true;
}

static void fake_uart_enqueue(const uint8_t *data, size_t data_len) {
    assert(data != NULL);
    assert((s_fake_uart.rx_len + data_len) <= sizeof(s_fake_uart.rx_buffer));
    memcpy(&s_fake_uart.rx_buffer[s_fake_uart.rx_len], data, data_len);
    s_fake_uart.rx_len += data_len;
}

bool mcu_link_uart_is_ready(void) {
    return s_fake_uart.ready;
}

esp_err_t mcu_link_uart_write(const uint8_t *data, size_t data_len, size_t *out_written) {
    if (data == NULL || data_len == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    assert(data_len <= sizeof(s_fake_uart.tx_buffer));
    memcpy(s_fake_uart.tx_buffer, data, data_len);
    s_fake_uart.tx_len = data_len;

    if (out_written != NULL) {
        *out_written = data_len;
    }

    return ESP_OK;
}

esp_err_t mcu_link_uart_read(uint8_t *buffer, size_t buffer_len, uint32_t timeout_ms, size_t *out_read) {
    size_t available;
    size_t read_len;

    (void)timeout_ms;

    if (buffer == NULL || buffer_len == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    available = s_fake_uart.rx_len - s_fake_uart.rx_offset;
    read_len = (available < buffer_len) ? available : buffer_len;
    if (read_len > 0u) {
        memcpy(buffer, &s_fake_uart.rx_buffer[s_fake_uart.rx_offset], read_len);
        s_fake_uart.rx_offset += read_len;
        if (s_fake_uart.rx_offset == s_fake_uart.rx_len) {
            s_fake_uart.rx_offset = 0u;
            s_fake_uart.rx_len = 0u;
        }
    }

    if (out_read != NULL) {
        *out_read = read_len;
    }

    return ESP_OK;
}

esp_err_t mcu_link_uart_get_buffered_bytes(size_t *out_bytes) {
    if (out_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_bytes = s_fake_uart.rx_len - s_fake_uart.rx_offset;
    return ESP_OK;
}

static mcu_link_test_packet_t make_ack_packet(uint32_t seq, uint32_t ref_seq) {
    static const uint16_t status_code = 0u;
    mcu_frame_header_t header;
    uint8_t payload[6];
    mcu_link_test_packet_t packet = {0};

    mcu_frame_header_init(&header, MCU_FRAME_CLASS_SYS, MCU_SYS_MSG_ACK, MCU_FRAME_FLAG_RESPONSE, seq, sizeof(payload));
    payload[0] = (uint8_t)(ref_seq & 0xFFu);
    payload[1] = (uint8_t)((ref_seq >> 8u) & 0xFFu);
    payload[2] = (uint8_t)((ref_seq >> 16u) & 0xFFu);
    payload[3] = (uint8_t)((ref_seq >> 24u) & 0xFFu);
    payload[4] = (uint8_t)(status_code & 0xFFu);
    payload[5] = (uint8_t)((status_code >> 8u) & 0xFFu);

    assert(mcu_link_test_support_make_packet(&header, payload, &packet) == ESP_OK);
    return packet;
}

static mcu_link_test_packet_t make_hello_rsp_packet(uint32_t seq) {
    mcu_frame_header_t header;
    uint8_t payload[9] = {
        0x01u, 0x00u, 0x01u, 0x00u, 0x01u, 0x20u, 0x00u, 0x00u, 0x01u,
    };
    mcu_link_test_packet_t packet = {0};

    mcu_frame_header_init(&header, MCU_FRAME_CLASS_SYS, MCU_SYS_MSG_HELLO_RSP, MCU_FRAME_FLAG_RESPONSE, seq,
                          sizeof(payload));
    assert(mcu_link_test_support_make_packet(&header, payload, &packet) == ESP_OK);
    return packet;
}

static mcu_link_test_packet_t make_motion_done_packet(uint32_t seq, uint32_t ref_seq) {
    mcu_frame_header_t header;
    uint8_t payload[11] = {0};
    mcu_link_test_packet_t packet = {0};

    mcu_frame_header_init(&header, MCU_FRAME_CLASS_MOTION, MCU_MOTION_MSG_MOTION_DONE, MCU_FRAME_FLAG_FINAL, seq,
                          sizeof(payload));
    payload[0] = (uint8_t)(ref_seq & 0xFFu);
    payload[1] = (uint8_t)((ref_seq >> 8u) & 0xFFu);
    payload[2] = (uint8_t)((ref_seq >> 16u) & 0xFFu);
    payload[3] = (uint8_t)((ref_seq >> 24u) & 0xFFu);
    payload[4] = 0x00u;
    payload[5] = 0x84u;
    payload[6] = 0x03u;
    payload[7] = 0xB0u;
    payload[8] = 0x04u;
    payload[9] = 0xB4u;
    payload[10] = 0x00u;

    assert(mcu_link_test_support_make_packet(&header, payload, &packet) == ESP_OK);
    return packet;
}

static mcu_link_test_packet_t make_imu_packet(uint32_t seq) {
    mcu_frame_header_t header;
    uint8_t payload[11] = {0};
    mcu_link_test_packet_t packet = {0};

    mcu_frame_header_init(&header, MCU_FRAME_CLASS_SENSOR, MCU_SENSOR_MSG_IMU_STATE, 0u, seq, sizeof(payload));
    payload[0] = 0x0Au;
    payload[1] = 0x00u;
    payload[2] = 0x9Bu;
    payload[3] = 0x04u;
    payload[4] = 0xB8u;
    payload[5] = 0x0Bu;
    payload[6] = 0xD4u;
    payload[7] = 0x03u;
    payload[8] = 0x78u;
    payload[9] = 0x00u;
    payload[10] = 0x01u;

    assert(mcu_link_test_support_make_packet(&header, payload, &packet) == ESP_OK);
    return packet;
}

static void expect_stats(const mcu_link_t *link, uint32_t crc_error_count) {
    mcu_link_stats_t stats = {0};

    assert(mcu_link_copy_stats(link, &stats) == ESP_OK);
    assert(stats.crc_error_count == crc_error_count);
}

static void test_poll_decodes_single_ack_frame(void) {
    mcu_link_t link = {0};
    mcu_link_event_t event = {0};
    const mcu_link_test_packet_t ack = make_ack_packet(3u, 1u);

    fake_uart_reset();
    fake_uart_enqueue(ack.wire, ack.wire_len);

    assert(mcu_link_init(&link) == ESP_OK);
    assert(mcu_link_begin_handshake(&link) == ESP_OK);
    assert(mcu_link_poll(&link, &event) == ESP_OK);
    assert(event.type == MCU_LINK_RX_EVENT_ACK);
    assert(event.frame.header.seq == 3u);
    assert(event.frame.header.payload_len == 6u);
    assert(event.frame.payload[0] == 0x01u);
    expect_stats(&link, 0u);
}

static void test_poll_keeps_second_frame_from_single_uart_read(void) {
    mcu_link_t link = {0};
    mcu_link_event_t event = {0};
    const mcu_link_test_packet_t ack = make_ack_packet(7u, 5u);
    const mcu_link_test_packet_t hello_rsp = make_hello_rsp_packet(8u);

    fake_uart_reset();
    fake_uart_enqueue(ack.wire, ack.wire_len);
    fake_uart_enqueue(hello_rsp.wire, hello_rsp.wire_len);

    assert(mcu_link_init(&link) == ESP_OK);
    assert(mcu_link_begin_handshake(&link) == ESP_OK);

    assert(mcu_link_poll(&link, &event) == ESP_OK);
    assert(event.type == MCU_LINK_RX_EVENT_ACK);
    assert(event.frame.header.seq == 7u);
    assert(mcu_link_get_state(&link) == MCU_LINK_STATE_HANDSHAKING);

    memset(&event, 0, sizeof(event));
    assert(mcu_link_poll(&link, &event) == ESP_OK);
    assert(event.type == MCU_LINK_RX_EVENT_HELLO_RSP);
    assert(event.frame.header.seq == 8u);
    assert(mcu_link_get_state(&link) == MCU_LINK_STATE_LINK_READY);
    assert(mcu_link_snapshot_supported(&link));
    expect_stats(&link, 0u);
}

static void test_poll_decodes_interleaved_burst_across_uart_chunks(void) {
    mcu_link_t link = {0};
    mcu_link_event_t event = {0};
    size_t i;

    fake_uart_reset();
    assert(mcu_link_init(&link) == ESP_OK);
    assert(mcu_link_begin_handshake(&link) == ESP_OK);

    for (i = 0u; i < 8u; ++i) {
        const mcu_link_test_packet_t ack = make_ack_packet((uint32_t)(10u + (i * 3u)), (uint32_t)(100u + i));
        const mcu_link_test_packet_t motion_done =
            make_motion_done_packet((uint32_t)(11u + (i * 3u)), (uint32_t)(100u + i));
        const mcu_link_test_packet_t imu = make_imu_packet((uint32_t)(12u + (i * 3u)));

        fake_uart_enqueue(ack.wire, ack.wire_len);
        fake_uart_enqueue(motion_done.wire, motion_done.wire_len);
        fake_uart_enqueue(imu.wire, imu.wire_len);
    }

    for (i = 0u; i < 8u; ++i) {
        assert(mcu_link_poll(&link, &event) == ESP_OK);
        assert(event.type == MCU_LINK_RX_EVENT_ACK);
        assert(event.frame.header.seq == (uint32_t)(10u + (i * 3u)));

        memset(&event, 0, sizeof(event));
        assert(mcu_link_poll(&link, &event) == ESP_OK);
        assert(event.type == MCU_LINK_RX_EVENT_MOTION_DONE);
        assert(event.frame.header.seq == (uint32_t)(11u + (i * 3u)));

        memset(&event, 0, sizeof(event));
        assert(mcu_link_poll(&link, &event) == ESP_OK);
        assert(event.type == MCU_LINK_RX_EVENT_IMU_STATE);
        assert(event.frame.header.seq == (uint32_t)(12u + (i * 3u)));
    }

    expect_stats(&link, 0u);
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"single_ack_frame", test_poll_decodes_single_ack_frame},
        {"multi_frame_single_read", test_poll_keeps_second_frame_from_single_uart_read},
        {"interleaved_burst_across_chunks", test_poll_decodes_interleaved_burst_across_uart_chunks},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
