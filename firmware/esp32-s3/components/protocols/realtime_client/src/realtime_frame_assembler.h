#ifndef REALTIME_FRAME_ASSEMBLER_H
#define REALTIME_FRAME_ASSEMBLER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t message_len;
    size_t frame_expected_len;
    size_t frame_received_len;
    bool active;
} realtime_frame_assembler_t;

void realtime_frame_assembler_init(realtime_frame_assembler_t *assembler, char *buffer, size_t capacity);
void realtime_frame_assembler_reset(realtime_frame_assembler_t *assembler);
esp_err_t realtime_frame_assembler_append(realtime_frame_assembler_t *assembler, const char *data, size_t data_len,
                                          size_t frame_payload_len, size_t frame_payload_offset, bool frame_fin,
                                          bool frame_starts_text, bool frame_continues_text, const char **complete_text);

#endif /* REALTIME_FRAME_ASSEMBLER_H */
