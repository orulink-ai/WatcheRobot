/**
 * @file ota_service.h
 * @brief Firmware OTA service (Phase 5)
 *
 * HTTP OTA download + WebSocket notification trigger.
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "esp_err.h"

#include <stdbool.h>

typedef void (*ota_service_progress_cb_t)(int percent, void *user_ctx);
typedef void (*ota_service_status_cb_t)(const char *status, void *user_ctx);
typedef bool (*ota_service_cancel_cb_t)(void *user_ctx);

esp_err_t ota_service_init(void);
esp_err_t ota_service_start(const char *url, const char *expected_version);
esp_err_t ota_service_start_with_progress(const char *url, const char *expected_version,
                                          ota_service_progress_cb_t progress_cb, void *user_ctx);
esp_err_t ota_service_start_with_callbacks(const char *url, const char *expected_version,
                                           ota_service_progress_cb_t progress_cb,
                                           ota_service_status_cb_t status_cb, void *user_ctx);
esp_err_t ota_service_install_with_sha256(const char *url, const char *expected_version,
                                          const char *expected_sha256,
                                          ota_service_progress_cb_t progress_cb,
                                          ota_service_status_cb_t status_cb, void *user_ctx);
esp_err_t ota_service_install_with_sha256_cancelable(const char *url, const char *expected_version,
                                                     const char *expected_sha256,
                                                     ota_service_progress_cb_t progress_cb,
                                                     ota_service_status_cb_t status_cb,
                                                     ota_service_cancel_cb_t cancel_cb, void *user_ctx);
esp_err_t ota_service_start_with_sha256(const char *url, const char *expected_version,
                                        const char *expected_sha256,
                                        ota_service_progress_cb_t progress_cb,
                                        ota_service_status_cb_t status_cb, void *user_ctx);
esp_err_t ota_service_cancel_active_request(const char *reason);
void ota_service_mark_valid(void);
const char *ota_service_get_fw_version(void);

#endif /* OTA_SERVICE_H */
