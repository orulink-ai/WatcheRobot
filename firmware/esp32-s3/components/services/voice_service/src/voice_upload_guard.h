#ifndef VOICE_UPLOAD_GUARD_H
#define VOICE_UPLOAD_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#define VOICE_UPLOAD_MAX_CONSECUTIVE_SEND_FAILURES 3U
#define VOICE_UPLOAD_MAX_DROPPED_FRAMES 8U
/* PCM frames are ~60 ms.  Delay the drop decision briefly for startup, but do
 * not hide more loss than the eight-frame uplink queue can absorb. */
#define VOICE_UPLOAD_DROP_GRACE_OBSERVATIONS 8U

typedef enum {
    VOICE_UPLOAD_CONTINUE = 0,
    VOICE_UPLOAD_ABORT_CLOUD_LOST,
    VOICE_UPLOAD_ABORT_SEND_FAILURES,
    VOICE_UPLOAD_ABORT_QUEUE_OVERRUN,
} voice_upload_guard_result_t;

typedef struct {
    uint32_t consecutive_send_failures;
    uint32_t dropped_frames_since_start;
    uint32_t last_dropped_frames;
    uint32_t observations_since_start;
} voice_upload_guard_t;

void voice_upload_guard_reset(voice_upload_guard_t *guard, uint32_t dropped_frames);
voice_upload_guard_result_t voice_upload_guard_observe(voice_upload_guard_t *guard, bool transport_ready,
                                                       bool send_succeeded, uint32_t dropped_frames);

#endif
