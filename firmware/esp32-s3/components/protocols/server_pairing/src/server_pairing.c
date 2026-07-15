#include "server_pairing.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "nvs.h"

#define TAG "SERVER_PAIR"
#define NVS_NAMESPACE "server_pair"

static void copy_text(char *out, size_t out_len, const char *value)
{
    if (out == NULL || out_len == 0U) {
        return;
    }
    snprintf(out, out_len, "%s", value != NULL ? value : "");
}

static esp_err_t nvs_get_text(nvs_handle_t handle, const char *key, char *out, size_t out_len)
{
    esp_err_t ret;
    size_t len = out_len;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    ret = nvs_get_str(handle, key, out, &len);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t nvs_set_text(nvs_handle_t handle, const char *key, const char *value)
{
    return nvs_set_str(handle, key, value != NULL ? value : "");
}

esp_err_t server_pairing_load(server_pairing_config_t *out)
{
    nvs_handle_t handle;
    esp_err_t ret;
    int32_t ws_port = 0;
    int32_t discovery_port = 0;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_text(handle, "server_id", out->server_id, sizeof(out->server_id));
    if (ret == ESP_OK) {
        ret = nvs_get_text(handle, "server_name", out->server_name, sizeof(out->server_name));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_text(handle, "pairing_id", out->pairing_id, sizeof(out->pairing_id));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_text(handle, "secret", out->pairing_secret, sizeof(out->pairing_secret));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_text(handle, "proto", out->protocol_version, sizeof(out->protocol_version));
    }
    if (ret == ESP_OK) {
        ret = nvs_get_i32(handle, "ws_port", &ws_port);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_get_i32(handle, "disc_port", &discovery_port);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    out->ws_port = ws_port > 0 ? (uint16_t)ws_port : 0;
    out->discovery_port = discovery_port > 0 ? (uint16_t)discovery_port : 0;
    if (out->protocol_version[0] == '\0') {
        copy_text(out->protocol_version, sizeof(out->protocol_version), SERVER_PAIRING_PROTOCOL_VERSION);
    }
    out->configured = out->server_id[0] != '\0' &&
                      out->pairing_id[0] != '\0' &&
                      strlen(out->pairing_secret) == 64U;
    return ESP_OK;
}

esp_err_t server_pairing_save(const server_pairing_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (config == NULL ||
        config->server_id[0] == '\0' ||
        config->pairing_id[0] == '\0' ||
        strlen(config->pairing_secret) != 64U) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_text(handle, "server_id", config->server_id);
    if (ret == ESP_OK) {
        ret = nvs_set_text(handle, "server_name", config->server_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_text(handle, "pairing_id", config->pairing_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_text(handle, "secret", config->pairing_secret);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_text(handle, "proto", config->protocol_version[0] != '\0'
                                               ? config->protocol_version
                                               : SERVER_PAIRING_PROTOCOL_VERSION);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "ws_port", config->ws_port);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "disc_port", config->discovery_port);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t server_pairing_clear(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

bool server_pairing_is_configured(void)
{
    server_pairing_config_t config;

    return server_pairing_load(&config) == ESP_OK && config.configured;
}

esp_err_t server_pairing_make_nonce(char *out, size_t out_len)
{
    if (out == NULL || out_len < SERVER_PAIRING_NONCE_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(out,
             out_len,
             "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(),
             (unsigned long)esp_random(),
             (unsigned long)esp_random(),
             (unsigned long)esp_random());
    return ESP_OK;
}

esp_err_t server_pairing_build_discover_json(char *out,
                                             size_t out_len,
                                             const char *device_id,
                                             const char *mac,
                                             const char *nonce)
{
    server_pairing_config_t config;
    esp_err_t ret;
    int written;

    if (out == NULL || out_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = server_pairing_load(&config);
    if (ret != ESP_OK || !config.configured) {
        written = snprintf(out,
                           out_len,
                           "{\"cmd\":\"DISCOVER\",\"device_id\":\"%s\",\"mac\":\"%s\"}",
                           device_id != NULL ? device_id : "",
                           mac != NULL ? mac : "");
    } else {
        written = snprintf(out,
                           out_len,
                           "{\"cmd\":\"DISCOVER\",\"protocol_version\":\"%s\","
                           "\"target_server_id\":\"%s\",\"pairing_id\":\"%s\","
                           "\"nonce\":\"%s\",\"device_id\":\"%s\",\"mac\":\"%s\"}",
                           config.protocol_version[0] != '\0' ? config.protocol_version : SERVER_PAIRING_PROTOCOL_VERSION,
                           config.server_id,
                           config.pairing_id,
                           nonce != NULL ? nonce : "",
                           device_id != NULL ? device_id : "",
                           mac != NULL ? mac : "");
    }

    return (written > 0 && (size_t)written < out_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t i;

    if (hex == NULL || out == NULL || strlen(hex) != out_len * 2U) {
        return false;
    }
    for (i = 0; i < out_len; ++i) {
        unsigned int byte = 0;
        if (sscanf(&hex[i * 2U], "%2x", &byte) != 1) {
            return false;
        }
        out[i] = (uint8_t)byte;
    }
    return true;
}

static bool hmac_sha256_hex(const char *secret_hex, const char *text, char *out, size_t out_len)
{
    uint8_t secret[32];
    uint8_t digest[32];
    const mbedtls_md_info_t *info;
    size_t i;
    size_t offset = 0;

    if (out == NULL || out_len < 65U || text == NULL || !hex_to_bytes(secret_hex, secret, sizeof(secret))) {
        return false;
    }
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return false;
    }
    if (mbedtls_md_hmac(info,
                        secret,
                        sizeof(secret),
                        (const unsigned char *)text,
                        strlen(text),
                        digest) != 0) {
        return false;
    }
    for (i = 0; i < sizeof(digest); ++i) {
        offset += (size_t)snprintf(out + offset, out_len - offset, "%02x", digest[i]);
    }
    out[64] = '\0';
    return true;
}

static const char *json_text(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(root, key);

    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
}

bool server_pairing_verify_announce_json(const char *json_text_payload, const char *expected_nonce)
{
    server_pairing_config_t config;
    cJSON *root = NULL;
    const char *cmd;
    const char *protocol_version;
    const char *server_id;
    const char *pairing_id;
    const char *ip;
    const char *nonce;
    const char *auth;
    cJSON *port_item;
    char canonical[256];
    char expected_auth[65];
    bool ok = false;
    int port = 0;

    if (server_pairing_load(&config) != ESP_OK || !config.configured) {
        return true;
    }
    root = cJSON_Parse(json_text_payload);
    if (root == NULL) {
        return false;
    }

    cmd = json_text(root, "cmd");
    protocol_version = json_text(root, "protocol_version");
    server_id = json_text(root, "server_id");
    pairing_id = json_text(root, "pairing_id");
    ip = json_text(root, "ip");
    nonce = json_text(root, "nonce");
    auth = json_text(root, "auth");
    port_item = cJSON_GetObjectItem(root, "port");
    if (cJSON_IsNumber(port_item)) {
        port = port_item->valueint;
    }

    if (strcmp(cmd, "ANNOUNCE") != 0 ||
        strcmp(server_id, config.server_id) != 0 ||
        strcmp(pairing_id, config.pairing_id) != 0 ||
        expected_nonce == NULL ||
        strcmp(nonce, expected_nonce) != 0 ||
        ip[0] == '\0' ||
        port <= 0 ||
        auth[0] == '\0') {
        cJSON_Delete(root);
        return false;
    }

    snprintf(canonical,
             sizeof(canonical),
             "cmd=%s\nprotocol_version=%s\nserver_id=%s\npairing_id=%s\nip=%s\nport=%d\nnonce=%s",
             cmd,
             protocol_version,
             server_id,
             pairing_id,
             ip,
             port,
             nonce);
    ok = hmac_sha256_hex(config.pairing_secret, canonical, expected_auth, sizeof(expected_auth)) &&
         strcmp(expected_auth, auth) == 0;
    cJSON_Delete(root);
    return ok;
}
