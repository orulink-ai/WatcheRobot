#include "coproc_crc16.h"

uint16_t CoprocCrc16_Update(uint16_t seed, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = seed;
    size_t index;

    if (bytes == NULL || length == 0u) {
        return crc;
    }

    for (index = 0u; index < length; ++index) {
        int bit;

        crc ^= ((uint16_t)bytes[index] << 8u);
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            } else {
                crc <<= 1u;
            }
        }
    }

    return crc;
}

uint16_t CoprocCrc16_Compute(const void *data, size_t length)
{
    return CoprocCrc16_Update(COPROC_CRC16_CCITT_FALSE_INIT, data, length);
}
