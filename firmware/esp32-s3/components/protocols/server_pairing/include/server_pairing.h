#ifndef SERVER_PAIRING_H
#define SERVER_PAIRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SERVER_PAIRING_PROTOCOL_VERSION "0.1.6"
#define SERVER_PAIRING_SERVER_ID_LEN 64
#define SERVER_PAIRING_SERVER_NAME_LEN 64
#define SERVER_PAIRING_PAIRING_ID_LEN 32
#define SERVER_PAIRING_SECRET_HEX_LEN 65
#define SERVER_PAIRING_NONCE_LEN 33

typedef struct {
    bool configured;
    char server_id[SERVER_PAIRING_SERVER_ID_LEN];
    char server_name[SERVER_PAIRING_SERVER_NAME_LEN];
    char pairing_id[SERVER_PAIRING_PAIRING_ID_LEN];
    char pairing_secret[SERVER_PAIRING_SECRET_HEX_LEN];
    char protocol_version[16];
    uint16_t ws_port;
    uint16_t discovery_port;
} server_pairing_config_t;

esp_err_t server_pairing_load(server_pairing_config_t *out);
esp_err_t server_pairing_save(const server_pairing_config_t *config);
esp_err_t server_pairing_clear(void);
bool server_pairing_is_configured(void);

esp_err_t server_pairing_make_nonce(char *out, size_t out_len);
esp_err_t server_pairing_build_discover_json(char *out,
                                             size_t out_len,
                                             const char *device_id,
                                             const char *mac,
                                             const char *nonce);
bool server_pairing_verify_announce_json(const char *json_text, const char *expected_nonce);

#endif /* SERVER_PAIRING_H */
