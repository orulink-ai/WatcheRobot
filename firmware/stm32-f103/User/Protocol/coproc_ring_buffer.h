#ifndef USER_PROTOCOL_COPROC_RING_BUFFER_H
#define USER_PROTOCOL_COPROC_RING_BUFFER_H

#include "coproc_protocol_types.h"

typedef struct
{
    uint8_t *storage;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} CoprocRingBuffer;

void CoprocRingBuffer_Init(CoprocRingBuffer *buffer, uint8_t *storage, size_t capacity);
void CoprocRingBuffer_Reset(CoprocRingBuffer *buffer);
size_t CoprocRingBuffer_Count(const CoprocRingBuffer *buffer);
size_t CoprocRingBuffer_FreeSpace(const CoprocRingBuffer *buffer);
CoprocStatus CoprocRingBuffer_Write(CoprocRingBuffer *buffer, const uint8_t *data, size_t length);
CoprocStatus CoprocRingBuffer_ReadByte(CoprocRingBuffer *buffer, uint8_t *outByte);

#endif /* USER_PROTOCOL_COPROC_RING_BUFFER_H */
