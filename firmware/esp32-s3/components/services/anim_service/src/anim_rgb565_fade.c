#include "anim_rgb565_fade.h"

#define ANIM_RGB565_ALPHA_MAX 255U

static const uint8_t g_fade_dither8x8[64] = {
    0U,  48U, 12U, 60U, 3U,  51U, 15U, 63U, 32U, 16U, 44U, 28U, 35U, 19U, 47U, 31U, 8U,  56U, 4U,  52U, 11U, 59U,
    7U,  55U, 40U, 24U, 36U, 20U, 43U, 27U, 39U, 23U, 2U,  50U, 14U, 62U, 1U,  49U, 13U, 61U, 34U, 18U, 46U, 30U,
    33U, 17U, 45U, 29U, 10U, 58U, 6U,  54U, 9U,  57U, 5U,  53U, 42U, 26U, 38U, 22U, 41U, 25U, 37U, 21U,
};

void anim_rgb565_fade_pattern_prepare(anim_rgb565_fade_pattern_t *pattern, uint8_t alpha) {
    if (pattern == NULL) {
        return;
    }

    for (uint32_t index = 0U; index < 64U; ++index) {
        uint32_t threshold = ((uint32_t)g_fade_dither8x8[index] * 4U) + 2U;
        pattern->visible[index] = alpha >= ANIM_RGB565_ALPHA_MAX || (uint32_t)alpha >= threshold ? 1U : 0U;
    }
}
