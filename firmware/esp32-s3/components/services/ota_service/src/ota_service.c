/**
 * @file ota_service.c
 * @brief Firmware OTA service.
 */

#include "ota_service.h"

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/sha256.h"

#define TAG "OTA_SVC"
#define OTA_HTTP_TIMEOUT_MS 15000
#define OTA_HTTP_CONNECT_TIMEOUT_MS 8000
#define OTA_HTTP_READ_TIMEOUT_MS 15000
#define OTA_READ_CHUNK_BYTES 4096
#define OTA_HTTP_MAX_REDIRECTS 5

static SemaphoreHandle_t s_active_client_lock = NULL;
static esp_http_client_handle_t s_active_client = NULL;

static void ota_digest_to_hex(const unsigned char *digest, size_t digest_len, char *out_hex, size_t out_hex_size) {
    static const char hex[] = "0123456789abcdef";

    if (digest == NULL || out_hex == NULL || out_hex_size < digest_len * 2U + 1U) {
        return;
    }
    for (size_t index = 0; index < digest_len; ++index) {
        out_hex[index * 2U] = hex[(digest[index] >> 4) & 0x0F];
        out_hex[index * 2U + 1U] = hex[digest[index] & 0x0F];
    }
    out_hex[digest_len * 2U] = '\0';
}

static int ota_hex_char_to_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool ota_sha256_hex_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL || strlen(left) != 64U || strlen(right) != 64U) {
        return false;
    }
    for (size_t index = 0; index < 64U; ++index) {
        int a = ota_hex_char_to_value(left[index]);
        int b = ota_hex_char_to_value(right[index]);
        if (a < 0 || b < 0 || a != b) {
            return false;
        }
    }
    return true;
}

static bool ota_has_text(const char *value) {
    return value != NULL && value[0] != '\0';
}

static void ota_service_ensure_active_client_lock(void) {
    if (s_active_client_lock == NULL) {
        s_active_client_lock = xSemaphoreCreateMutex();
        if (s_active_client_lock == NULL) {
            ESP_LOGW(TAG, "Failed to create OTA active client lock");
        }
    }
}

static void ota_service_set_active_client(esp_http_client_handle_t client) {
    ota_service_ensure_active_client_lock();
    if (s_active_client_lock != NULL) {
        xSemaphoreTake(s_active_client_lock, portMAX_DELAY);
    }
    s_active_client = client;
    if (s_active_client_lock != NULL) {
        xSemaphoreGive(s_active_client_lock);
    }
}

static void ota_service_clear_active_client(esp_http_client_handle_t client) {
    ota_service_ensure_active_client_lock();
    if (s_active_client_lock != NULL) {
        xSemaphoreTake(s_active_client_lock, portMAX_DELAY);
    }
    if (s_active_client == client) {
        s_active_client = NULL;
    }
    if (s_active_client_lock != NULL) {
        xSemaphoreGive(s_active_client_lock);
    }
}

static bool ota_service_cancel_requested(ota_service_cancel_cb_t cancel_cb, void *user_ctx) {
    return cancel_cb != NULL && cancel_cb(user_ctx);
}

static esp_err_t ota_service_return_canceled(ota_service_status_cb_t status_cb, void *user_ctx) {
    if (status_cb != NULL) {
        status_cb("OTA canceled", user_ctx);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ota_service_cancel_active_request(const char *reason) {
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    ota_service_ensure_active_client_lock();
    if (s_active_client_lock != NULL) {
        xSemaphoreTake(s_active_client_lock, portMAX_DELAY);
    }
    esp_http_client_handle_t client = s_active_client;
    if (client != NULL) {
        ESP_LOGI(TAG, "Canceling active OTA HTTP request (%s)",
                 reason != NULL && reason[0] != '\0' ? reason : "no reason");
        ret = esp_http_client_cancel_request(client);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to cancel active OTA HTTP request: %s", esp_err_to_name(ret));
        }
    }
    if (s_active_client_lock != NULL) {
        xSemaphoreGive(s_active_client_lock);
    }
    return ret;
}

static bool ota_http_status_is_redirect(int status_code) {
    return status_code == HttpStatus_MovedPermanently || status_code == HttpStatus_Found ||
           status_code == HttpStatus_SeeOther || status_code == HttpStatus_TemporaryRedirect ||
           status_code == HttpStatus_PermanentRedirect;
}

static esp_err_t ota_http_open_follow_redirects(esp_http_client_handle_t client, const char *url,
                                                int64_t *out_content_length, int *out_status_code) {
    if (client == NULL || out_content_length == NULL || out_status_code == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int redirect_count = 0; redirect_count <= OTA_HTTP_MAX_REDIRECTS; ++redirect_count) {
        esp_err_t ret = esp_http_client_open(client, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA HTTP open failed: %s", esp_err_to_name(ret));
            return ret;
        }

        int64_t content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "OTA HTTP header fetch failed: status=%d ret=%lld url=%s", status_code,
                     (long long)content_length, url);
            esp_http_client_close(client);
            return ESP_ERR_HTTP_FETCH_HEADER;
        }

        if (status_code >= 200 && status_code < 300) {
            *out_content_length = content_length;
            *out_status_code = status_code;
            if (redirect_count > 0) {
                ESP_LOGI(TAG, "OTA HTTP redirect resolved: redirects=%d status=%d content_length=%lld",
                         redirect_count, status_code, (long long)content_length);
            }
            return ESP_OK;
        }

        if (!ota_http_status_is_redirect(status_code)) {
            *out_content_length = content_length;
            *out_status_code = status_code;
            ESP_LOGE(TAG, "OTA HTTP status=%d url=%s", status_code, url);
            return ESP_FAIL;
        }

        if (redirect_count >= OTA_HTTP_MAX_REDIRECTS) {
            ESP_LOGE(TAG, "OTA HTTP redirect limit reached: status=%d max=%d url=%s", status_code,
                     OTA_HTTP_MAX_REDIRECTS, url);
            esp_http_client_close(client);
            return ESP_ERR_HTTP_MAX_REDIRECT;
        }

        ret = esp_http_client_set_redirection(client);
        esp_http_client_close(client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA HTTP redirect setup failed: status=%d ret=%s url=%s", status_code,
                     esp_err_to_name(ret), url);
            return ret;
        }
        ESP_LOGI(TAG, "OTA HTTP redirect followed: status=%d step=%d", status_code, redirect_count + 1);
    }

    return ESP_ERR_HTTP_MAX_REDIRECT;
}

esp_err_t ota_service_init(void) {
    ESP_LOGI(TAG, "ota_service_init");
    return ESP_OK;
}

esp_err_t ota_service_start(const char *url, const char *expected_version) {
    return ota_service_start_with_progress(url, expected_version, NULL, NULL);
}

esp_err_t ota_service_start_with_progress(const char *url, const char *expected_version,
                                          ota_service_progress_cb_t progress_cb, void *user_ctx) {
    return ota_service_start_with_callbacks(url, expected_version, progress_cb, NULL, user_ctx);
}

esp_err_t ota_service_start_with_callbacks(const char *url, const char *expected_version,
                                           ota_service_progress_cb_t progress_cb,
                                           ota_service_status_cb_t status_cb, void *user_ctx) {
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Starting OTA url=%s expected=%s running=%s next=%s", url,
             expected_version != NULL ? expected_version : "",
             running != NULL ? running->label : "(unknown)", next != NULL ? next->label : "(unknown)");

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .max_redirection_count = OTA_HTTP_MAX_REDIRECTS,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    if (status_cb != NULL) {
        status_cb("Connecting OTA...", user_ctx);
    }
    esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int last_progress = -1;
    while (true) {
        ret = esp_https_ota_perform(ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int image_size = esp_https_ota_get_image_size(ota_handle);
        int read_size = esp_https_ota_get_image_len_read(ota_handle);
        if (image_size > 0) {
            int progress = (read_size * 100) / image_size;
            if (progress != last_progress) {
                last_progress = progress;
                if (progress_cb != NULL) {
                    progress_cb(progress, user_ctx);
                }
            }
        }
    }

    if (ret != ESP_OK) {
        esp_https_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (progress_cb != NULL) {
        progress_cb(100, user_ctx);
    }
    if (status_cb != NULL) {
        status_cb("Finalizing OTA...", user_ctx);
    }
    ESP_LOGI(TAG, "OTA download complete; finalizing image");

    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (status_cb != NULL) {
        status_cb("OTA complete. Rebooting...", user_ctx);
    }

    ESP_LOGI(TAG, "OTA succeeded; rebooting into updated slot");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_service_install_with_sha256_cancelable(const char *url, const char *expected_version,
                                                     const char *expected_sha256,
                                                     ota_service_progress_cb_t progress_cb,
                                                     ota_service_status_cb_t status_cb,
                                                     ota_service_cancel_cb_t cancel_cb, void *user_ctx) {
    esp_http_client_handle_t client = NULL;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *running = NULL;
    const esp_partition_t *next = NULL;
    uint8_t *buffer = NULL;
    mbedtls_sha256_context sha_ctx;
    bool sha_started = false;
    bool ota_started = false;
    int64_t content_length = 0;
    int64_t downloaded = 0;
    int last_progress = -1;
    unsigned char digest[32] = {0};
    char actual_sha256[65] = {0};
    esp_err_t ret = ESP_OK;

    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }
    if (ota_has_text(expected_sha256) && strlen(expected_sha256) != 64U) {
        ESP_LOGE(TAG, "Expected OTA SHA-256 must be 64 hex characters");
        return ESP_ERR_INVALID_ARG;
    }

    running = esp_ota_get_running_partition();
    next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        ESP_LOGE(TAG, "No OTA update partition available");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Installing OTA url=%s expected=%s sha=%s running=%s next=%s", url,
             expected_version != NULL ? expected_version : "",
             expected_sha256 != NULL ? expected_sha256 : "",
             running != NULL ? running->label : "(unknown)", next->label);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = OTA_HTTP_CONNECT_TIMEOUT_MS,
        .keep_alive_enable = true,
        .max_redirection_count = OTA_HTTP_MAX_REDIRECTS,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    buffer = (uint8_t *)malloc(OTA_READ_CHUNK_BYTES);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    ota_service_set_active_client(client);

    if (status_cb != NULL) {
        status_cb("Connecting OTA...", user_ctx);
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }
    int status_code = 0;
    ret = ota_http_open_follow_redirects(client, url, &content_length, &status_code);
    if (ret != ESP_OK) {
        if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
            ret = ota_service_return_canceled(status_cb, user_ctx);
        }
        goto cleanup;
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }
    (void)esp_http_client_set_timeout_ms(client, OTA_HTTP_READ_TIMEOUT_MS);

    if (status_cb != NULL) {
        status_cb("Writing OTA image...", user_ctx);
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }
    ret = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed for %s: %s", next->label, esp_err_to_name(ret));
        goto cleanup;
    }
    ota_started = true;

    mbedtls_sha256_init(&sha_ctx);
    sha_started = true;
    if (mbedtls_sha256_starts(&sha_ctx, 0) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    while (true) {
        if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
            ret = ota_service_return_canceled(status_cb, user_ctx);
            goto cleanup;
        }
        int read_len = esp_http_client_read(client, (char *)buffer, OTA_READ_CHUNK_BYTES);
        if (read_len < 0) {
            ret = ota_service_cancel_requested(cancel_cb, user_ctx)
                      ? ota_service_return_canceled(status_cb, user_ctx)
                      : ESP_FAIL;
            goto cleanup;
        }
        if (read_len == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
                ret = ota_service_return_canceled(status_cb, user_ctx);
                goto cleanup;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ret = esp_ota_write(update_handle, buffer, (size_t)read_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        if (mbedtls_sha256_update(&sha_ctx, buffer, (size_t)read_len) != 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }

        downloaded += read_len;
        if (content_length > 0) {
            int progress = (int)((downloaded * 100) / content_length);
            if (progress > 100) {
                progress = 100;
            }
            if (progress != last_progress) {
                last_progress = progress;
                if (progress_cb != NULL) {
                    progress_cb(progress, user_ctx);
                }
            }
        }
        if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
            ret = ota_service_return_canceled(status_cb, user_ctx);
            goto cleanup;
        }
    }

    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }
    if (downloaded <= 0) {
        if (status_cb != NULL) {
            status_cb("No OTA data received", user_ctx);
        }
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    if (content_length > 0 && downloaded != content_length) {
        ESP_LOGE(TAG, "OTA size mismatch: downloaded=%lld content_length=%lld", (long long)downloaded,
                 (long long)content_length);
        if (status_cb != NULL) {
            status_cb("Download size mismatch", user_ctx);
        }
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    if (mbedtls_sha256_finish(&sha_ctx, digest) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    mbedtls_sha256_free(&sha_ctx);
    sha_started = false;
    ota_digest_to_hex(digest, sizeof(digest), actual_sha256, sizeof(actual_sha256));

    if (ota_has_text(expected_sha256) && !ota_sha256_hex_equal(actual_sha256, expected_sha256)) {
        ESP_LOGE(TAG, "OTA SHA-256 mismatch: actual=%s expected=%s", actual_sha256, expected_sha256);
        if (status_cb != NULL) {
            status_cb("SHA-256 mismatch. Device unchanged.", user_ctx);
        }
        ret = ESP_ERR_INVALID_CRC;
        goto cleanup;
    }

    if (progress_cb != NULL) {
        progress_cb(100, user_ctx);
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }
    if (status_cb != NULL) {
        status_cb("Finalizing OTA...", user_ctx);
    }

    ret = esp_ota_end(update_handle);
    ota_started = false;
    update_handle = 0;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(ret));
        if (status_cb != NULL) {
            status_cb("Firmware image is invalid", user_ctx);
        }
        goto cleanup;
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }

    if (ota_has_text(expected_version)) {
        esp_app_desc_t app_desc = {0};
        ret = esp_ota_get_partition_description(next, &app_desc);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA app description read failed: %s", esp_err_to_name(ret));
            if (status_cb != NULL) {
                status_cb("Firmware metadata unreadable", user_ctx);
            }
            goto cleanup;
        }
        if (strcmp(app_desc.version, expected_version) != 0) {
            char status[96] = {0};
            ESP_LOGE(TAG, "OTA version mismatch: actual=%s expected=%s", app_desc.version, expected_version);
            if (status_cb != NULL) {
                snprintf(status, sizeof(status), "Version mismatch: expected %.24s, got %.24s", expected_version,
                         app_desc.version);
                status_cb(status, user_ctx);
            }
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
    }
    if (ota_service_cancel_requested(cancel_cb, user_ctx)) {
        ret = ota_service_return_canceled(status_cb, user_ctx);
        goto cleanup;
    }

    ret = esp_ota_set_boot_partition(next);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set OTA boot partition %s: %s", next->label, esp_err_to_name(ret));
        if (status_cb != NULL) {
            status_cb("OTA boot setup failed", user_ctx);
        }
        goto cleanup;
    }

    if (status_cb != NULL) {
        status_cb("OTA complete. Ready to reboot...", user_ctx);
    }
    ESP_LOGI(TAG, "OTA image installed: bytes=%lld sha256=%s next=%s", (long long)downloaded, actual_sha256,
             next->label);

cleanup:
    if (sha_started) {
        mbedtls_sha256_free(&sha_ctx);
    }
    if (ota_started) {
        esp_ota_abort(update_handle);
    }
    if (client != NULL) {
        ota_service_clear_active_client(client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(buffer);
    return ret;
}

esp_err_t ota_service_install_with_sha256(const char *url, const char *expected_version,
                                          const char *expected_sha256,
                                          ota_service_progress_cb_t progress_cb,
                                          ota_service_status_cb_t status_cb, void *user_ctx) {
    return ota_service_install_with_sha256_cancelable(url, expected_version, expected_sha256, progress_cb, status_cb,
                                                      NULL, user_ctx);
}

esp_err_t ota_service_start_with_sha256(const char *url, const char *expected_version,
                                        const char *expected_sha256,
                                        ota_service_progress_cb_t progress_cb,
                                        ota_service_status_cb_t status_cb, void *user_ctx) {
    esp_err_t ret = ota_service_install_with_sha256(url, expected_version, expected_sha256, progress_cb, status_cb,
                                                    user_ctx);
    if (ret != ESP_OK) {
        return ret;
    }
    if (status_cb != NULL) {
        status_cb("OTA complete. Rebooting...", user_ctx);
    }
    ESP_LOGI(TAG, "OTA succeeded; rebooting into updated slot");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

void ota_service_mark_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        ESP_LOGI(TAG, "OTA partition valid: %s (offset 0x%lx)", running->label, running->address);
    }

    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret != ESP_OK && ret != ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to mark OTA app valid: %s", esp_err_to_name(ret));
    }
}

const char *ota_service_get_fw_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc ? app_desc->version : "0.0.0";
}
