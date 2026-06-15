#include "coproc_cobs.h"

size_t CoprocCobs_MaxEncodedSize(size_t inputLength)
{
    return inputLength + (inputLength / 254u) + 1u;
}

CoprocStatus CoprocCobs_Encode(const uint8_t *input,
                               size_t inputLength,
                               uint8_t *output,
                               size_t outputLength,
                               size_t *encodedLength)
{
    size_t codeIndex = 0u;
    size_t writeIndex = 1u;
    uint8_t code = 1u;
    size_t readIndex;

    if (output == NULL || encodedLength == NULL || (inputLength > 0u && input == NULL)) {
        return COPROC_STATUS_INVALID_ARG;
    }

    *encodedLength = 0u;
    if (outputLength < CoprocCobs_MaxEncodedSize(inputLength)) {
        return COPROC_STATUS_NO_MEM;
    }

    for (readIndex = 0u; readIndex < inputLength; ++readIndex) {
        uint8_t byte = input[readIndex];

        if (byte == 0u) {
            output[codeIndex] = code;
            codeIndex = writeIndex++;
            code = 1u;
            continue;
        }

        output[writeIndex++] = byte;
        code++;
        if (code == 0xFFu) {
            output[codeIndex] = code;
            codeIndex = writeIndex++;
            code = 1u;
        }
    }

    output[codeIndex] = code;
    *encodedLength = writeIndex;
    return COPROC_STATUS_OK;
}

CoprocStatus CoprocCobs_Decode(const uint8_t *input,
                               size_t inputLength,
                               uint8_t *output,
                               size_t outputLength,
                               size_t *decodedLength)
{
    size_t readIndex = 0u;
    size_t writeIndex = 0u;

    if (input == NULL || output == NULL || decodedLength == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    *decodedLength = 0u;
    while (readIndex < inputLength) {
        uint8_t code = input[readIndex++];
        uint8_t index;

        if (code == 0u) {
            return COPROC_STATUS_INVALID_ARG;
        }

        for (index = 1u; index < code; ++index) {
            if (readIndex >= inputLength) {
                return COPROC_STATUS_INVALID_SIZE;
            }
            if (writeIndex >= outputLength) {
                return COPROC_STATUS_NO_MEM;
            }

            output[writeIndex++] = input[readIndex++];
        }

        if (code != 0xFFu && readIndex < inputLength) {
            if (writeIndex >= outputLength) {
                return COPROC_STATUS_NO_MEM;
            }
            output[writeIndex++] = 0u;
        }
    }

    *decodedLength = writeIndex;
    return COPROC_STATUS_OK;
}
