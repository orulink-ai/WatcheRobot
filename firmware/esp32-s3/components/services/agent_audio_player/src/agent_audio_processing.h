#ifndef AGENT_AUDIO_PROCESSING_H
#define AGENT_AUDIO_PROCESSING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void agent_audio_apply_gain_q8(uint8_t *pcm, size_t len, int gain_q8);

#ifdef __cplusplus
}
#endif

#endif /* AGENT_AUDIO_PROCESSING_H */
