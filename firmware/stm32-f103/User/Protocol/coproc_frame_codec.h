#ifndef USER_PROTOCOL_COPROC_FRAME_CODEC_H
#define USER_PROTOCOL_COPROC_FRAME_CODEC_H

#include "coproc_protocol_types.h"

typedef struct
{
    uint8_t magic0;
    uint8_t magic1;
    uint8_t protoVersion;
    uint8_t msgClass;
    uint8_t msgId;
    uint8_t flags;
    uint32_t seq;
    uint16_t payloadLength;
} CoprocFrameHeader;

typedef struct
{
    CoprocFrameHeader header;
    uint16_t crc16;
    uint8_t payload[COPROC_FRAME_MAX_PAYLOAD_SIZE];
} CoprocFrame;

void CoprocFrameCodec_InitHeader(CoprocFrameHeader *header,
                                 uint8_t msgClass,
                                 uint8_t msgId,
                                 uint8_t flags,
                                 uint32_t seq,
                                 uint16_t payloadLength);
uint16_t CoprocFrameCodec_ComputeCrc(const CoprocFrameHeader *header, const uint8_t *payload);
CoprocStatus CoprocFrameCodec_Pack(const CoprocFrameHeader *header,
                                   const uint8_t *payload,
                                   uint8_t *buffer,
                                   size_t bufferLength,
                                   size_t *encodedLength);
CoprocStatus CoprocFrameCodec_Unpack(const uint8_t *buffer,
                                     size_t bufferLength,
                                     CoprocFrame *frame,
                                     size_t *payloadLength);

#endif /* USER_PROTOCOL_COPROC_FRAME_CODEC_H */
