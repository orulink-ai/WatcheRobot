#include "coproc_framing.h"

void CoprocFraming_Init(CoprocFraming *framing)
{
    CoprocFraming_Reset(framing);
}

void CoprocFraming_Reset(CoprocFraming *framing)
{
    if (framing == NULL) {
        return;
    }

    framing->length = 0u;
    framing->truncated = 0u;
}

CoprocFramingResult CoprocFraming_ConsumeByte(CoprocFraming *framing, uint8_t byte, size_t *outLength)
{
    if (framing == NULL || outLength == NULL) {
        return COPROC_FRAMING_RESULT_NONE;
    }

    *outLength = 0u;

    if (byte == 0u) {
        if (framing->truncated != 0u || framing->length == 0u) {
            CoprocFraming_Reset(framing);
            return COPROC_FRAMING_RESULT_FRAME_DROPPED;
        }

        *outLength = framing->length;
        framing->length = 0u;
        framing->truncated = 0u;
        return COPROC_FRAMING_RESULT_CANDIDATE_READY;
    }

    if (framing->truncated != 0u) {
        return COPROC_FRAMING_RESULT_NONE;
    }

    if (framing->length >= (sizeof(framing->candidate) - 1u)) {
        framing->truncated = 1u;
        return COPROC_FRAMING_RESULT_NONE;
    }

    framing->candidate[framing->length++] = byte;
    return COPROC_FRAMING_RESULT_NONE;
}

const uint8_t *CoprocFraming_GetCandidate(const CoprocFraming *framing)
{
    return (framing != NULL) ? framing->candidate : NULL;
}
