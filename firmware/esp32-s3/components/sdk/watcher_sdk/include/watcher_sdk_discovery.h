#ifndef WATCHER_SDK_DISCOVERY_H
#define WATCHER_SDK_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHER_SDK_DISCOVERY_PORT 37021U
#define WATCHER_SDK_WEBSOCKET_DEFAULT_PORT 8766U
#define WATCHER_SDK_DISCOVERY_PROTOCOL_VERSION "1.1"
#define WATCHER_SDK_PAIRING_CODE_LENGTH 6U

typedef struct {
    char ip[16];
    uint16_t port;
    char protocol_version[16];
    char server[32];
} watcher_sdk_gateway_info_t;

typedef bool (*watcher_sdk_discovery_cancel_fn_t)(void *context);

int watcher_sdk_discovery_start(watcher_sdk_gateway_info_t *out_gateway, const char *pairing_code, uint32_t timeout_ms,
                                watcher_sdk_discovery_cancel_fn_t cancel_fn, void *cancel_context);
char *watcher_sdk_discovery_ws_url(const watcher_sdk_gateway_info_t *gateway);
void watcher_sdk_device_identity(char *device_id, size_t device_id_size, char *mac, size_t mac_size);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_DISCOVERY_H */
