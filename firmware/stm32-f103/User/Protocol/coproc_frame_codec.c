#include "coproc_frame_codec.h"

#include "coproc_crc16.h"

#include <string.h>

static void CoprocFrameCodec_WriteU16Le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void CoprocFrameCodec_WriteU32Le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t CoprocFrameCodec_ReadU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8u));
}

static uint32_t CoprocFrameCodec_ReadU32Le(const uint8_t *src)
{
    return (uint32_t)src[0]
           | ((uint32_t)src[1] << 8u)
           | ((uint32_t)src[2] << 16u)
           | ((uint32_t)src[3] << 24u);
}

static uint8_t CoprocFrameCodec_HeaderIsValid(const CoprocFrameHeader *header)
{
    if (header == NULL) {
        return 0u;
    }

    if (header->magic0 != COPROC_FRAME_MAGIC0 || header->magic1 != COPROC_FRAME_MAGIC1) {
        return 0u;
    }

    if (header->protoVersion != COPROC_FRAME_PROTO_VERSION) {
        return 0u;
    }

    return (header->payloadLength <= COPROC_FRAME_MAX_PAYLOAD_SIZE) ? 1u : 0u;
}

void CoprocFrameCodec_InitHeader(CoprocFrameHeader *header,
                                 uint8_t msgClass,
                                 uint8_t msgId,
                                 uint8_t flags,
                                 uint32_t seq,
                                 uint16_t payloadLength)
{
    if (header == NULL) {
        return;
    }

    header->magic0 = COPROC_FRAME_MAGIC0;
    header->magic1 = COPROC_FRAME_MAGIC1;
    header->protoVersion = COPROC_FRAME_PROTO_VERSION;
    header->msgClass = msgClass;
    header->msgId = msgId;
    header->flags = flags;
    header->seq = seq;
    header->payloadLength = payloadLength;
}

uint16_t CoprocFrameCodec_ComputeCrc(const CoprocFrameHeader *header, const uint8_t *payload)
{
    uint8_t headerBytes[COPROC_FRAME_PREFIX_SIZE];
    size_t offset = 0u;
    uint16_t crc;

    if (header == NULL || (header->payloadLength > 0u && payload == NULL)) {
        return 0u;
    }

    headerBytes[offset++] = header->magic0;
    headerBytes[offset++] = header->magic1;
    headerBytes[offset++] = header->protoVersion;
    headerBytes[offset++] = header->msgClass;
    headerBytes[offset++] = header->msgId;
    headerBytes[offset++] = header->flags;
    CoprocFrameCodec_WriteU32Le(&headerBytes[offset], header->seq);
    offset += sizeof(uint32_t);
    CoprocFrameCodec_WriteU16Le(&headerBytes[offset], header->payloadLength);
    offset += sizeof(uint16_t);

    crc = CoprocCrc16_Compute(headerBytes, offset);
    if (header->payloadLength > 0u) {
        crc = CoprocCrc16_Update(crc, payload, header->payloadLength);
    }

    return crc;
}

CoprocStatus CoprocFrameCodec_Pack(const CoprocFrameHeader *header,
                                   const uint8_t *payload,
                                   uint8_t *buffer,
                                   size_t bufferLength,
                                   size_t *encodedLength)
{
    uint8_t *cursor = buffer;
    uint16_t crc;

    if (header == NULL || buffer == NULL || encodedLength == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (header->payloadLength > 0u && payload == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (CoprocFrameCodec_HeaderIsValid(header) == 0u) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (bufferLength < ((size_t)COPROC_FRAME_PREFIX_SIZE + header->payloadLength + COPROC_FRAME_CRC_SIZE)) {
        return COPROC_STATUS_NO_MEM;
    }

    *cursor++ = header->magic0;
    *cursor++ = header->magic1;
    *cursor++ = header->protoVersion;
    *cursor++ = header->msgClass;
    *cursor++ = header->msgId;
    *cursor++ = header->flags;
    CoprocFrameCodec_WriteU32Le(cursor, header->seq);
    cursor += sizeof(uint32_t);
    CoprocFrameCodec_WriteU16Le(cursor, header->payloadLength);
    cursor += sizeof(uint16_t);

    if (header->payloadLength > 0u) {
        memcpy(cursor, payload, header->payloadLength);
        cursor += header->payloadLength;
    }

    crc = CoprocFrameCodec_ComputeCrc(header, payload);
    CoprocFrameCodec_WriteU16Le(cursor, crc);
    cursor += sizeof(uint16_t);

    *encodedLength = (size_t)(cursor - buffer);
    return COPROC_STATUS_OK;
}

CoprocStatus CoprocFrameCodec_Unpack(const uint8_t *buffer,
                                     size_t bufferLength,
                                     CoprocFrame *frame,
                                     size_t *payloadLength)
{
    CoprocFrameHeader header;
    uint16_t expectedCrc;
    uint16_t actualCrc;

    if (buffer == NULL || frame == NULL || payloadLength == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (bufferLength < ((size_t)COPROC_FRAME_PREFIX_SIZE + COPROC_FRAME_CRC_SIZE)) {
        return COPROC_STATUS_INVALID_SIZE;
    }

    header.magic0 = buffer[0];
    header.magic1 = buffer[1];
    header.protoVersion = buffer[2];
    header.msgClass = buffer[3];
    header.msgId = buffer[4];
    header.flags = buffer[5];
    header.seq = CoprocFrameCodec_ReadU32Le(&buffer[6]);
    header.payloadLength = CoprocFrameCodec_ReadU16Le(&buffer[10]);

    if (CoprocFrameCodec_HeaderIsValid(&header) == 0u) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (((size_t)COPROC_FRAME_PREFIX_SIZE + header.payloadLength + COPROC_FRAME_CRC_SIZE) != bufferLength) {
        return COPROC_STATUS_INVALID_SIZE;
    }

    expectedCrc = CoprocFrameCodec_ReadU16Le(&buffer[COPROC_FRAME_PREFIX_SIZE + header.payloadLength]);
    actualCrc = CoprocFrameCodec_ComputeCrc(&header, &buffer[COPROC_FRAME_PREFIX_SIZE]);
    if (expectedCrc != actualCrc) {
        return COPROC_STATUS_INVALID_CRC;
    }

    frame->header = header;
    frame->crc16 = expectedCrc;
    if (header.payloadLength > 0u) {
        memcpy(frame->payload, &buffer[COPROC_FRAME_PREFIX_SIZE], header.payloadLength);
    }
    *payloadLength = header.payloadLength;
    return COPROC_STATUS_OK;
}
