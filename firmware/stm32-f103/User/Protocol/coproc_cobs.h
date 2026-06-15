#ifndef USER_PROTOCOL_COPROC_COBS_H
#define USER_PROTOCOL_COPROC_COBS_H

#include "coproc_protocol_types.h"

size_t CoprocCobs_MaxEncodedSize(size_t inputLength);
CoprocStatus CoprocCobs_Encode(const uint8_t *input,
                               size_t inputLength,
                               uint8_t *output,
                               size_t outputLength,
                               size_t *encodedLength);
CoprocStatus CoprocCobs_Decode(const uint8_t *input,
                               size_t inputLength,
                               uint8_t *output,
                               size_t outputLength,
                               size_t *decodedLength);

#endif /* USER_PROTOCOL_COPROC_COBS_H */
