#include "watcher_sdk_discovery.h"

#include "cJSON.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_DISCOVERY_BUFFER_SIZE 384U
#define SDK_DISCOVERY_RETRY_MS 1000U
#define SDK_DISCOVERY_REQUEST_ID_LENGTH 16U

static bool pairing_code_is_valid(const char *pairing_code) {
    size_t index;

    if (pairing_code == NULL || strlen(pairing_code) != WATCHER_SDK_PAIRING_CODE_LENGTH) {
        return false;
    }
    for (index = 0U; index < WATCHER_SDK_PAIRING_CODE_LENGTH; ++index) {
        if (!isdigit((unsigned char)pairing_code[index])) {
            return false;
        }
    }
    return true;
}

void watcher_sdk_device_identity(char *device_id, size_t device_id_size, char *mac, size_t mac_size) {
    uint8_t bytes[6] = {0};

    (void)esp_wifi_get_mac(WIFI_IF_STA, bytes);
    if (device_id != NULL && device_id_size > 0U) {
        snprintf(device_id, device_id_size, "watcher-%02X%02X", bytes[4], bytes[5]);
    }
    if (mac != NULL && mac_size > 0U) {
        snprintf(mac, mac_size, "%02X:%02X:%02X:%02X:%02X:%02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
                 bytes[5]);
    }
}

static int build_discover(char *buffer, size_t buffer_size, const char *pairing_code, const char *request_id) {
    cJSON *root = cJSON_CreateObject();
    char *serialized;
    char device_id[32];
    char mac[18];
    size_t length;

    if (root == NULL || buffer == NULL || !pairing_code_is_valid(pairing_code) || request_id == NULL ||
        strlen(request_id) != SDK_DISCOVERY_REQUEST_ID_LENGTH) {
        cJSON_Delete(root);
        return -1;
    }
    watcher_sdk_device_identity(device_id, sizeof(device_id), mac, sizeof(mac));
    cJSON_AddStringToObject(root, "cmd", "SDK_DISCOVER");
    cJSON_AddStringToObject(root, "service", "watcher-sdk");
    cJSON_AddStringToObject(root, "protocol_version", WATCHER_SDK_DISCOVERY_PROTOCOL_VERSION);
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddStringToObject(root, "mac", mac);
    cJSON_AddStringToObject(root, "pairing_code", pairing_code);
    cJSON_AddStringToObject(root, "request_id", request_id);
    serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == NULL) {
        return -1;
    }
    length = strlen(serialized);
    if (length >= buffer_size) {
        cJSON_free(serialized);
        return -1;
    }
    memcpy(buffer, serialized, length + 1U);
    cJSON_free(serialized);
    return 0;
}

static int parse_announce(const char *json, const struct sockaddr_in *source, const char *pairing_code,
                          const char *request_id, watcher_sdk_gateway_info_t *gateway) {
    cJSON *root = cJSON_Parse(json);
    cJSON *port;
    const cJSON *cmd;
    const cJSON *service;
    const cJSON *protocol;
    const cJSON *response_pairing_code;
    const cJSON *response_request_id;
    const cJSON *server;

    if (root == NULL || source == NULL || gateway == NULL || !pairing_code_is_valid(pairing_code) ||
        request_id == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    cmd = cJSON_GetObjectItem(root, "cmd");
    service = cJSON_GetObjectItem(root, "service");
    protocol = cJSON_GetObjectItem(root, "protocol_version");
    port = cJSON_GetObjectItem(root, "port");
    response_pairing_code = cJSON_GetObjectItem(root, "pairing_code");
    response_request_id = cJSON_GetObjectItem(root, "request_id");
    if (!cJSON_IsString(cmd) || strcmp(cmd->valuestring, "ANNOUNCE") != 0 || !cJSON_IsString(service) ||
        strcmp(service->valuestring, "watcher-sdk") != 0 || !cJSON_IsString(protocol) ||
        strcmp(protocol->valuestring, WATCHER_SDK_DISCOVERY_PROTOCOL_VERSION) != 0 || !cJSON_IsNumber(port) ||
        port->valueint <= 0 || port->valueint > UINT16_MAX || !cJSON_IsString(response_pairing_code) ||
        strcmp(response_pairing_code->valuestring, pairing_code) != 0 || !cJSON_IsString(response_request_id) ||
        strcmp(response_request_id->valuestring, request_id) != 0) {
        cJSON_Delete(root);
        return -1;
    }
    memset(gateway, 0, sizeof(*gateway));
    strncpy(gateway->ip, inet_ntoa(source->sin_addr), sizeof(gateway->ip) - 1U);
    gateway->port = (uint16_t)port->valueint;
    strncpy(gateway->protocol_version, protocol->valuestring, sizeof(gateway->protocol_version) - 1U);
    server = cJSON_GetObjectItem(root, "server");
    if (cJSON_IsString(server) && server->valuestring != NULL) {
        strncpy(gateway->server, server->valuestring, sizeof(gateway->server) - 1U);
    }
    cJSON_Delete(root);
    return 0;
}

int watcher_sdk_discovery_start(watcher_sdk_gateway_info_t *out_gateway, const char *pairing_code, uint32_t timeout_ms,
                                watcher_sdk_discovery_cancel_fn_t cancel_fn, void *cancel_context) {
    int socket_handle;
    int broadcast = 1;
    struct timeval receive_timeout = {.tv_sec = 0, .tv_usec = 250000};
    struct sockaddr_in destination = {0};
    char transmit[SDK_DISCOVERY_BUFFER_SIZE];
    char receive[SDK_DISCOVERY_BUFFER_SIZE];
    char request_id[SDK_DISCOVERY_REQUEST_ID_LENGTH + 1U];
    uint32_t started_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t last_sent_ms = UINT32_MAX;
    int result = -1;

    if (out_gateway == NULL || !pairing_code_is_valid(pairing_code)) {
        return -1;
    }
    (void)snprintf(request_id, sizeof(request_id), "%08" PRIX32 "%08" PRIX32, esp_random(), esp_random());
    if (build_discover(transmit, sizeof(transmit), pairing_code, request_id) != 0) {
        return -1;
    }
    socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle < 0) {
        return -1;
    }
    (void)setsockopt(socket_handle, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    (void)setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(WATCHER_SDK_DISCOVERY_PORT);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    while ((uint32_t)((uint32_t)(esp_timer_get_time() / 1000ULL) - started_ms) < timeout_ms) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        struct sockaddr_in source = {0};
        socklen_t source_size = sizeof(source);
        int received;

        if (cancel_fn != NULL && cancel_fn(cancel_context)) {
            break;
        }
        if (last_sent_ms == UINT32_MAX || (uint32_t)(now_ms - last_sent_ms) >= SDK_DISCOVERY_RETRY_MS) {
            (void)sendto(socket_handle, transmit, strlen(transmit), 0, (struct sockaddr *)&destination,
                         sizeof(destination));
            last_sent_ms = now_ms;
        }
        received = recvfrom(socket_handle, receive, sizeof(receive) - 1U, 0, (struct sockaddr *)&source, &source_size);
        if (received > 0) {
            receive[received] = '\0';
            if (parse_announce(receive, &source, pairing_code, request_id, out_gateway) == 0) {
                result = 0;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    close(socket_handle);
    return result;
}

char *watcher_sdk_discovery_ws_url(const watcher_sdk_gateway_info_t *gateway) {
    char *url;
    if (gateway == NULL || gateway->ip[0] == '\0' || gateway->port == 0U) {
        return NULL;
    }
    url = (char *)malloc(64U);
    if (url != NULL) {
        snprintf(url, 64U, "ws://%s:%u", gateway->ip, (unsigned)gateway->port);
    }
    return url;
}
