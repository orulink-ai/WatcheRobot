#include "realtime_protocol.h"

#include <stdio.h>
#include <string.h>

static const char k_b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    if (c == '=') {
        return -2;
    }
    return -1;
}

static esp_err_t json_escape(const char *input, char *out, size_t out_size) {
    size_t used = 0;

    if (out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (input == NULL) {
        input = "";
    }

    for (const char *p = input; *p != '\0'; ++p) {
        const char *replacement = NULL;
        char scratch[3] = {0};

        if (*p == '"' || *p == '\\') {
            scratch[0] = '\\';
            scratch[1] = *p;
            replacement = scratch;
        } else if (*p == '\n') {
            replacement = "\\n";
        } else if (*p == '\r') {
            replacement = "\\r";
        } else if (*p == '\t') {
            replacement = "\\t";
        }

        if (replacement != NULL) {
            size_t repl_len = strlen(replacement);
            if (used + repl_len >= out_size) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(out + used, replacement, repl_len);
            used += repl_len;
            continue;
        }

        if (used + 1 >= out_size) {
            return ESP_ERR_NO_MEM;
        }
        out[used++] = *p;
    }

    out[used] = '\0';
    return ESP_OK;
}

static esp_err_t base64_encode(const uint8_t *input, size_t input_len, char *out, size_t out_size) {
    size_t required = ((input_len + 2U) / 3U) * 4U;
    size_t pos = 0;

    if ((input == NULL && input_len > 0) || out == NULL || out_size <= required) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < input_len; i += 3U) {
        uint32_t v = ((uint32_t)input[i]) << 16;
        bool have_b = i + 1U < input_len;
        bool have_c = i + 2U < input_len;

        if (have_b) {
            v |= ((uint32_t)input[i + 1U]) << 8;
        }
        if (have_c) {
            v |= input[i + 2U];
        }

        out[pos++] = k_b64_table[(v >> 18) & 0x3F];
        out[pos++] = k_b64_table[(v >> 12) & 0x3F];
        out[pos++] = have_b ? k_b64_table[(v >> 6) & 0x3F] : '=';
        out[pos++] = have_c ? k_b64_table[v & 0x3F] : '=';
    }

    out[pos] = '\0';
    return ESP_OK;
}

static esp_err_t base64_decode(const char *input, uint8_t *out, size_t out_size, size_t *out_len) {
    size_t input_len;
    size_t pos = 0;

    if (input == NULL || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    input_len = strlen(input);
    if ((input_len % 4U) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < input_len; i += 4U) {
        int a = b64_value(input[i]);
        int b = b64_value(input[i + 1U]);
        int c = b64_value(input[i + 2U]);
        int d = b64_value(input[i + 3U]);
        uint32_t v;

        if (a < 0 || b < 0 || c == -1 || d == -1) {
            return ESP_ERR_INVALID_ARG;
        }

        v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | (c >= 0 ? ((uint32_t)c << 6) : 0U) |
            (d >= 0 ? (uint32_t)d : 0U);

        if (pos >= out_size) {
            return ESP_ERR_NO_MEM;
        }
        out[pos++] = (uint8_t)((v >> 16) & 0xFF);
        if (c != -2) {
            if (pos >= out_size) {
                return ESP_ERR_NO_MEM;
            }
            out[pos++] = (uint8_t)((v >> 8) & 0xFF);
        }
        if (d != -2) {
            if (pos >= out_size) {
                return ESP_ERR_NO_MEM;
            }
            out[pos++] = (uint8_t)(v & 0xFF);
        }
    }

    *out_len = pos;
    return ESP_OK;
}

esp_err_t realtime_protocol_build_session_update(const realtime_client_config_t *config, char *out, size_t out_size) {
    char voice[REALTIME_CLIENT_VOICE_MAX * 2] = {0};
    char instructions[REALTIME_CLIENT_INSTRUCTIONS_MAX * 2] = {0};
    const char *safe_voice = config != NULL && config->voice != NULL ? config->voice : "Aiden";
    const char *safe_instructions =
        config != NULL && config->instructions != NULL ? config->instructions : "Answer briefly and naturally.";
    int written;

    if (out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (json_escape(safe_voice, voice, sizeof(voice)) != ESP_OK ||
        json_escape(safe_instructions, instructions, sizeof(instructions)) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    written = snprintf(out, out_size,
                       "{\"type\":\"session.update\",\"session\":{\"type\":\"realtime\","
                       "\"instructions\":\"%s\","
                       "\"audio\":{\"output\":{\"voice\":\"%s\",\"format\":{\"type\":\"audio/pcm\",\"rate\":24000}}},"
                       "\"turn_detection\":{\"type\":\"server_vad\"},"
                       "\"input_audio_transcription\":{\"model\":\"local\"}}}",
                       instructions, voice);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t realtime_protocol_build_audio_append_with_scratch(const uint8_t *pcm, size_t len, char *scratch,
                                                            size_t scratch_size, char *out, size_t out_size) {
    int written;

    if (len > 1920U) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (base64_encode(pcm, len, scratch, scratch_size) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(out, out_size, "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", scratch);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t realtime_protocol_build_audio_append(const uint8_t *pcm, size_t len, char *out, size_t out_size) {
    char b64[(1920U * 4U / 3U) + 8U] = {0};
    return realtime_protocol_build_audio_append_with_scratch(pcm, len, b64, sizeof(b64), out, out_size);
}

esp_err_t realtime_protocol_build_event(const char *type, char *out, size_t out_size) {
    char escaped[80] = {0};
    int written;

    if (type == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (json_escape(type, escaped, sizeof(escaped)) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    written = snprintf(out, out_size, "{\"type\":\"%s\"}", escaped);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t realtime_protocol_extract_string(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[64];
    const char *start;
    const char *cursor;
    size_t used = 0;
    int written;

    if (json == NULL || key == NULL || out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return ESP_ERR_INVALID_ARG;
    }

    start = strstr(json, pattern);
    if (start == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    start = strchr(start + strlen(pattern), ':');
    if (start == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    start++;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (*start != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    cursor = start + 1;
    while (*cursor != '\0' && *cursor != '"') {
        char value = *cursor++;
        if (value == '\\' && *cursor != '\0') {
            value = *cursor++;
        }
        if (used + 1 >= out_size) {
            return ESP_ERR_NO_MEM;
        }
        out[used++] = value;
    }
    if (*cursor != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    out[used] = '\0';
    return ESP_OK;
}

esp_err_t realtime_protocol_parse_event_type(const char *json, char *out, size_t out_size) {
    return realtime_protocol_extract_string(json, "type", out, out_size);
}

esp_err_t realtime_protocol_decode_audio_delta_with_scratch(const char *json, char *scratch, size_t scratch_size,
                                                            uint8_t *out, size_t out_size, size_t *out_len) {
    esp_err_t ret;

    if (scratch == NULL || scratch_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    scratch[0] = '\0';
    ret = realtime_protocol_extract_string(json, "delta", scratch, scratch_size);
    if (ret != ESP_OK) {
        ret = realtime_protocol_extract_string(json, "audio", scratch, scratch_size);
    }
    if (ret != ESP_OK) {
        return ret;
    }
    return base64_decode(scratch, out, out_size, out_len);
}

esp_err_t realtime_protocol_decode_audio_delta(const char *json, uint8_t *out, size_t out_size, size_t *out_len) {
    char b64[8192] = {0};
    return realtime_protocol_decode_audio_delta_with_scratch(json, b64, sizeof(b64), out, out_size, out_len);
}

esp_err_t realtime_client_build_session_update_for_test(const realtime_client_config_t *config, char *out,
                                                        size_t out_size) {
    return realtime_protocol_build_session_update(config, out, out_size);
}

esp_err_t realtime_client_build_audio_append_for_test(const uint8_t *pcm, size_t len, char *out, size_t out_size) {
    return realtime_protocol_build_audio_append(pcm, len, out, out_size);
}

esp_err_t realtime_client_parse_event_type_for_test(const char *json, char *out, size_t out_size) {
    return realtime_protocol_parse_event_type(json, out, out_size);
}

esp_err_t realtime_client_decode_audio_delta_for_test(const char *json, uint8_t *out, size_t out_size,
                                                      size_t *out_len) {
    return realtime_protocol_decode_audio_delta(json, out, out_size, out_len);
}
