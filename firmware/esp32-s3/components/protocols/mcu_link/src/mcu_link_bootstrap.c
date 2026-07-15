#include "mcu_link_bootstrap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mcu_link_uart.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "MCU_LINK_BOOT";
static const char *OBS_TAG = "MCU_OBS";
static const int64_t HELLO_RETRY_INTERVAL_US = 1000LL * 1000LL;

static mcu_link_t s_link;
static bool s_link_initialized;
static int64_t s_first_hello_req_us;
static int64_t s_last_hello_req_us;

static esp_err_t mcu_link_bootstrap_send_hello_req(void) {
    uint32_t seq = 0u;
    size_t wire_len = 0u;
    mcu_link_state_t previous_state = mcu_link_get_state(&s_link);
    esp_err_t ret;

    ret = mcu_link_send_hello_req(&s_link, &seq, &wire_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU link hello request failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_last_hello_req_us = esp_timer_get_time();
    if (s_first_hello_req_us == 0 ||
        (previous_state != MCU_LINK_STATE_HANDSHAKING && previous_state != MCU_LINK_STATE_DEGRADED &&
         previous_state != MCU_LINK_STATE_RECOVERING)) {
        s_first_hello_req_us = s_last_hello_req_us;
    }
    ESP_LOGI(TAG, "MCU link hello request queued (seq=%lu wire_len=%u state=%d)", (unsigned long)seq,
             (unsigned)wire_len, (int)mcu_link_get_state(&s_link));
    ESP_LOGI(OBS_TAG, "evt=hello_req seq=%lu msg_class=%u msg_id=%u link_state=%d", (unsigned long)seq,
             (unsigned)MCU_FRAME_CLASS_SYS, (unsigned)MCU_SYS_MSG_HELLO_REQ, (int)mcu_link_get_state(&s_link));
    return ESP_OK;
}

static bool mcu_link_bootstrap_hello_retry_due(void) {
    const mcu_link_state_t state = mcu_link_get_state(&s_link);

    if (state != MCU_LINK_STATE_HANDSHAKING && state != MCU_LINK_STATE_DEGRADED && state != MCU_LINK_STATE_RECOVERING) {
        return false;
    }

    if (s_last_hello_req_us == 0) {
        return true;
    }

    return (esp_timer_get_time() - s_last_hello_req_us) >= HELLO_RETRY_INTERVAL_US;
}

static esp_err_t mcu_link_bootstrap_init_uart(void) {
#ifdef CONFIG_WATCHER_MCU_LINK_UART_ENABLE
    const mcu_link_uart_config_t config = {
        .port = (uart_port_t)CONFIG_WATCHER_MCU_LINK_UART_PORT_NUM,
        .tx_io_num = CONFIG_WATCHER_MCU_LINK_UART_TX_GPIO,
        .rx_io_num = CONFIG_WATCHER_MCU_LINK_UART_RX_GPIO,
        .baud_rate = CONFIG_WATCHER_MCU_LINK_UART_BAUD_RATE,
        .rx_buffer_size = CONFIG_WATCHER_MCU_LINK_UART_RX_BUFFER,
        .tx_buffer_size = CONFIG_WATCHER_MCU_LINK_UART_TX_BUFFER,
    };

    return mcu_link_uart_init(&config);
#else
    return ESP_OK;
#endif
}

esp_err_t mcu_link_bootstrap_init(void) {
    esp_err_t ret;

    if (s_link_initialized) {
        return ESP_OK;
    }

    ret = mcu_link_init(&s_link);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU link bootstrap init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mcu_link_bootstrap_init_uart();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU link UART bootstrap init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_link_initialized = true;
    s_first_hello_req_us = 0;
    s_last_hello_req_us = 0;
    ESP_LOGI(TAG, "MCU link bootstrap initialized (uart_ready=%d link_ready=%d ready=%d)",
             mcu_link_uart_is_ready() ? 1 : 0, mcu_link_is_link_ready(&s_link) ? 1 : 0,
             mcu_link_is_ready(&s_link) ? 1 : 0);
    return ESP_OK;
}

mcu_link_t *mcu_link_bootstrap_get_link(void) {
    return s_link_initialized ? &s_link : NULL;
}

bool mcu_link_bootstrap_is_link_ready(void) {
    return s_link_initialized && mcu_link_is_link_ready(&s_link);
}

bool mcu_link_bootstrap_is_ready(void) {
    return s_link_initialized && mcu_link_is_ready(&s_link);
}

bool mcu_link_bootstrap_handshake_timed_out(uint32_t timeout_ms) {
    int64_t elapsed_us;
    mcu_link_state_t state;

    if (!s_link_initialized || s_first_hello_req_us == 0 || timeout_ms == 0) {
        return false;
    }

    state = mcu_link_get_state(&s_link);
    if (state != MCU_LINK_STATE_HANDSHAKING && state != MCU_LINK_STATE_DEGRADED && state != MCU_LINK_STATE_RECOVERING) {
        return false;
    }

    elapsed_us = esp_timer_get_time() - s_first_hello_req_us;
    return elapsed_us >= ((int64_t)timeout_ms * 1000LL);
}

esp_err_t mcu_link_bootstrap_poll(mcu_link_event_t *out_event) {
    mcu_link_event_t local_event;
    if (!s_link_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_uart_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (out_event == NULL) {
        out_event = &local_event;
    }

    memset(out_event, 0, sizeof(*out_event));

    while (mcu_link_poll(&s_link, out_event) == ESP_OK) {
        if (out_event != &local_event) {
            return ESP_OK;
        }
        memset(out_event, 0, sizeof(*out_event));
    }

    if (mcu_link_bootstrap_hello_retry_due()) {
        return mcu_link_bootstrap_send_hello_req();
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t mcu_link_bootstrap_start(void) {
    if (!s_link_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcu_link_uart_is_ready()) {
        ESP_LOGI(TAG, "MCU link transport disabled; handshake not started");
        return ESP_OK;
    }

    return mcu_link_bootstrap_send_hello_req();
}

void mcu_link_bootstrap_stop(void) {
    if (!s_link_initialized) {
        mcu_link_uart_deinit();
        return;
    }

    mcu_link_uart_deinit();
    memset(&s_link, 0, sizeof(s_link));
    s_link_initialized = false;
    s_first_hello_req_us = 0;
    s_last_hello_req_us = 0;
    ESP_LOGI(TAG, "MCU link bootstrap stopped");
}

mcu_link_state_t mcu_link_bootstrap_get_state(void) {
    return s_link_initialized ? mcu_link_get_state(&s_link) : MCU_LINK_STATE_DOWN;
}
