#ifndef USER_PROTOCOL_COPROC_FRAMING_H
#define USER_PROTOCOL_COPROC_FRAMING_H

#include "coproc_protocol_types.h"

typedef enum
{
    COPROC_FRAMING_RESULT_NONE = 0,
    COPROC_FRAMING_RESULT_CANDIDATE_READY,
    COPROC_FRAMING_RESULT_FRAME_DROPPED
} CoprocFramingResult;

typedef struct
{
    uint8_t candidate[COPROC_FRAME_MAX_WIRE_SIZE];
    size_t length;
    uint8_t truncated;
} CoprocFraming;

void CoprocFraming_Init(CoprocFraming *framing);
void CoprocFraming_Reset(CoprocFraming *framing);
CoprocFramingResult CoprocFraming_ConsumeByte(CoprocFraming *framing, uint8_t byte, size_t *outLength);
const uint8_t *CoprocFraming_GetCandidate(const CoprocFraming *framing);

#endif /* USER_PROTOCOL_COPROC_FRAMING_H */
