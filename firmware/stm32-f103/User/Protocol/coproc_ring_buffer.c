#include "coproc_ring_buffer.h"

void CoprocRingBuffer_Init(CoprocRingBuffer *buffer, uint8_t *storage, size_t capacity)
{
    if (buffer == NULL) {
        return;
    }

    buffer->storage = storage;
    buffer->capacity = capacity;
    buffer->head = 0u;
    buffer->tail = 0u;
    buffer->count = 0u;
}

void CoprocRingBuffer_Reset(CoprocRingBuffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    buffer->head = 0u;
    buffer->tail = 0u;
    buffer->count = 0u;
}

size_t CoprocRingBuffer_Count(const CoprocRingBuffer *buffer)
{
    return (buffer != NULL) ? buffer->count : 0u;
}

size_t CoprocRingBuffer_FreeSpace(const CoprocRingBuffer *buffer)
{
    if (buffer == NULL || buffer->capacity < buffer->count) {
        return 0u;
    }

    return buffer->capacity - buffer->count;
}

CoprocStatus CoprocRingBuffer_Write(CoprocRingBuffer *buffer, const uint8_t *data, size_t length)
{
    size_t index;

    if (buffer == NULL || (length > 0u && data == NULL)) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (CoprocRingBuffer_FreeSpace(buffer) < length) {
        return COPROC_STATUS_NO_MEM;
    }

    for (index = 0u; index < length; ++index) {
        buffer->storage[buffer->head] = data[index];
        buffer->head = (buffer->head + 1u) % buffer->capacity;
    }

    buffer->count += length;
    return COPROC_STATUS_OK;
}

CoprocStatus CoprocRingBuffer_ReadByte(CoprocRingBuffer *buffer, uint8_t *outByte)
{
    if (buffer == NULL || outByte == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (buffer->count == 0u) {
        return COPROC_STATUS_EMPTY;
    }

    *outByte = buffer->storage[buffer->tail];
    buffer->tail = (buffer->tail + 1u) % buffer->capacity;
    buffer->count--;
    return COPROC_STATUS_OK;
}
