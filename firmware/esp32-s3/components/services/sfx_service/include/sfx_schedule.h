#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFX_SCHEDULE_CAPACITY 16
#define SFX_SCHEDULE_ID_LEN 32

typedef struct {
    bool active;
    int64_t due_ms;
    uint32_t sequence;
    char sound_id[SFX_SCHEDULE_ID_LEN];
} sfx_scheduled_request_t;

typedef struct {
    uint32_t next_sequence;
    sfx_scheduled_request_t requests[SFX_SCHEDULE_CAPACITY];
} sfx_schedule_t;

void sfx_schedule_clear(sfx_schedule_t *schedule);
bool sfx_schedule_has_active(const sfx_schedule_t *schedule);
bool sfx_schedule_has_due(const sfx_schedule_t *schedule, int64_t now_ms);
bool sfx_schedule_enqueue(sfx_schedule_t *schedule, const char *sound_id, int64_t due_ms, char *replaced_sound_id,
                          size_t replaced_sound_id_size);
bool sfx_schedule_take_due(sfx_schedule_t *schedule, int64_t now_ms, char *sound_id, size_t sound_id_size,
                           int64_t *late_ms);

#ifdef __cplusplus
}
#endif
