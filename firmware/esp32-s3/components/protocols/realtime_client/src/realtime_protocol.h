#ifndef REALTIME_PROTOCOL_H
#define REALTIME_PROTOCOL_H

#include "realtime_client.h"

esp_err_t realtime_protocol_build_session_update(const realtime_client_config_t *config, char *out, size_t out_size);
esp_err_t realtime_protocol_build_audio_append_with_scratch(const uint8_t *pcm, size_t len, char *scratch,
                                                            size_t scratch_size, char *out, size_t out_size);
esp_err_t realtime_protocol_build_audio_append(const uint8_t *pcm, size_t len, char *out, size_t out_size);
esp_err_t realtime_protocol_build_event(const char *type, char *out, size_t out_size);
esp_err_t realtime_protocol_extract_string(const char *json, const char *key, char *out, size_t out_size);
esp_err_t realtime_protocol_parse_event_type(const char *json, char *out, size_t out_size);
esp_err_t realtime_protocol_decode_audio_delta_with_scratch(const char *json, char *scratch, size_t scratch_size,
                                                            uint8_t *out, size_t out_size, size_t *out_len);
esp_err_t realtime_protocol_decode_audio_delta(const char *json, uint8_t *out, size_t out_size, size_t *out_len);

#endif /* REALTIME_PROTOCOL_H */
