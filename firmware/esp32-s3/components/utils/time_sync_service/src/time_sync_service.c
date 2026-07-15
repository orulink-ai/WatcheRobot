#include "time_sync_service.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define TIME_SYNC_VALID_AFTER_EPOCH 1672531200

static const char *TAG = "TIME_SYNC";

static bool s_initialized = false;
static bool s_started = false;
static bool s_time_synced = false;
static time_sync_service_sync_callback_t s_sync_cb = NULL;

static void time_sync_notification_cb(struct timeval *tv)
{
    time_t now;
    struct tm timeinfo = {0};

    (void)tv;

    now = time(NULL);
    s_time_synced = now > TIME_SYNC_VALID_AFTER_EPOCH;
    if (s_time_synced && localtime_r(&now, &timeinfo) != NULL) {
        ESP_LOGI(TAG, "SNTP time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec);
    } else {
        ESP_LOGI(TAG, "SNTP time synchronized");
    }

    if (s_sync_cb != NULL) {
        s_sync_cb();
    }
}

esp_err_t time_sync_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (setenv("TZ", CONFIG_WATCHER_TIME_SYNC_DEFAULT_TZ, 1) != 0) {
        ESP_LOGW(TAG, "Failed to set default TZ=%s", CONFIG_WATCHER_TIME_SYNC_DEFAULT_TZ);
    }
    tzset();

    s_initialized = true;
    ESP_LOGI(TAG, "Time sync initialized tz=%s", CONFIG_WATCHER_TIME_SYNC_DEFAULT_TZ);
    return ESP_OK;
}

void time_sync_service_register_callback(time_sync_service_sync_callback_t cb)
{
    s_sync_cb = cb;
}

esp_err_t time_sync_service_start_on_network(void)
{
    esp_err_t ret;

    ret = time_sync_service_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_started || esp_sntp_enabled()) {
        s_started = true;
        return ESP_OK;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_WATCHER_TIME_SYNC_NTP_SERVER_1);
    esp_sntp_setservername(1, CONFIG_WATCHER_TIME_SYNC_NTP_SERVER_2);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    s_started = true;
    ESP_LOGI(TAG, "SNTP started servers=%s,%s",
             CONFIG_WATCHER_TIME_SYNC_NTP_SERVER_1,
             CONFIG_WATCHER_TIME_SYNC_NTP_SERVER_2);
    return ESP_OK;
}

bool time_sync_service_is_started(void)
{
    return s_started;
}

bool time_sync_service_has_valid_time(void)
{
    time_t now = time(NULL);

    return s_time_synced || now > TIME_SYNC_VALID_AFTER_EPOCH;
}
