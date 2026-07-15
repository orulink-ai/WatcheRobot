#ifndef ANIM_RGB565_FADE_H
#define ANIM_RGB565_FADE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t visible[64];
} anim_rgb565_fade_pattern_t;

/**
 * Prepares an 8x8 ordered fade pattern for one alpha value.
 *
 * Increasing alpha changes spatial coverage instead of scaling the three
 * quantized RGB565 channels independently.
 */
void anim_rgb565_fade_pattern_prepare(anim_rgb565_fade_pattern_t *pattern, uint8_t alpha);

/** Returns either the byte-for-byte source color or black. */
static inline uint16_t anim_rgb565_fade_pattern_apply(const anim_rgb565_fade_pattern_t *pattern, uint16_t color,
                                                      uint32_t x, uint32_t y) {
    if (pattern == NULL) {
        return color;
    }
    return pattern->visible[((y & 7U) * 8U) + (x & 7U)] != 0U ? color : 0U;
}

#endif
