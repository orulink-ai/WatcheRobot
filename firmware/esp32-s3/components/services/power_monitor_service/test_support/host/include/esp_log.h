#ifndef POWER_MONITOR_HOST_TEST_ESP_LOG_H
#define POWER_MONITOR_HOST_TEST_ESP_LOG_H

#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)0)

static inline const char *esp_err_to_name(int err) {
    (void)err;
    return "host";
}

#endif /* POWER_MONITOR_HOST_TEST_ESP_LOG_H */
