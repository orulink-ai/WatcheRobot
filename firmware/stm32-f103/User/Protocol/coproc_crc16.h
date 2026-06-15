#ifndef USER_PROTOCOL_COPROC_CRC16_H
#define USER_PROTOCOL_COPROC_CRC16_H

#include <stddef.h>
#include <stdint.h>

#define COPROC_CRC16_CCITT_FALSE_INIT 0xFFFFu

uint16_t CoprocCrc16_Update(uint16_t seed, const void *data, size_t length);
uint16_t CoprocCrc16_Compute(const void *data, size_t length);

#endif /* USER_PROTOCOL_COPROC_CRC16_H */
