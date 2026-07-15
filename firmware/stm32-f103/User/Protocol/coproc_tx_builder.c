#include "coproc_tx_builder.h"

#include "app_config.h"
#include "coproc_cobs.h"
#include "watcher_build_info.h"

#include <string.h>

static size_t CoprocTxBuilder_BoundedStringLen(const char *value, size_t maxLength)
{
    size_t length = 0u;

    if (value == NULL) {
        return 0u;
    }

    while (length < maxLength && value[length] != '\0') {
        length++;
    }
    return length;
}

static size_t CoprocTxBuilder_WriteTlvString(uint8_t *payload, size_t offset, uint8_t type, const char *value)
{
    size_t valueLength;
    size_t available;

    if (payload == NULL || offset >= COPROC_FRAME_MAX_PAYLOAD_SIZE || (offset + 2u) > COPROC_FRAME_MAX_PAYLOAD_SIZE) {
        return offset;
    }

    available = COPROC_FRAME_MAX_PAYLOAD_SIZE - offset - 2u;
    if (available > 63u) {
        available = 63u;
    }
    valueLength = CoprocTxBuilder_BoundedStringLen(value, available);
    payload[offset++] = type;
    payload[offset++] = (uint8_t)valueLength;
    if (valueLength > 0u) {
        memcpy(&payload[offset], value, valueLength);
        offset += valueLength;
    }
    return offset;
}

static size_t CoprocTxBuilder_WriteTlvU8(uint8_t *payload, size_t offset, uint8_t type, uint8_t value)
{
    if (payload == NULL || (offset + 3u) > COPROC_FRAME_MAX_PAYLOAD_SIZE) {
        return offset;
    }

    payload[offset++] = type;
    payload[offset++] = 1u;
    payload[offset++] = value;
    return offset;
}

static void CoprocTxBuilder_WriteU16Le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void CoprocTxBuilder_WriteU32Le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void CoprocTxBuilder_WriteI16Le(uint8_t *dst, int16_t value)
{
    CoprocTxBuilder_WriteU16Le(dst, (uint16_t)value);
}

static CoprocStatus CoprocTxBuilder_EncodeWire(CoprocTxMessage *message)
{
    uint8_t raw[COPROC_FRAME_MAX_RAW_SIZE];
    size_t rawLength = 0u;
    size_t encodedLength = 0u;
    CoprocStatus status;

    if (message == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    status = CoprocFrameCodec_Pack(&message->frame.header, message->frame.payload, raw, sizeof(raw), &rawLength);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocCobs_Encode(raw, rawLength, message->wire, sizeof(message->wire), &encodedLength);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    if (encodedLength >= sizeof(message->wire)) {
        return COPROC_STATUS_NO_MEM;
    }

    message->wire[encodedLength] = 0u;
    message->wireLength = encodedLength + 1u;
    message->frame.crc16 = CoprocFrameCodec_ComputeCrc(&message->frame.header, message->frame.payload);
    return COPROC_STATUS_OK;
}

CoprocStatus CoprocTxBuilder_BuildAck(uint32_t seq, uint32_t refSeq, CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_ACK,
                                COPROC_FRAME_FLAG_RESP,
                                seq,
                                6u);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, refSeq);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[4], 0u);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildNack(uint32_t seq, uint32_t refSeq, uint16_t reasonCode, CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_NACK,
                                COPROC_FRAME_FLAG_RESP | COPROC_FRAME_FLAG_FINAL,
                                seq,
                                8u);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, refSeq);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[4], 1u);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[6], reasonCode);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildFault(uint32_t seq,
                                        uint32_t refSeq,
                                        uint8_t faultSource,
                                        uint16_t faultCode,
                                        uint16_t detail,
                                        CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_FAULT,
                                COPROC_FRAME_FLAG_RESP | COPROC_FRAME_FLAG_FINAL,
                                seq,
                                9u);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, refSeq);
    outMessage->frame.payload[4] = faultSource;
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[5], faultCode);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[7], detail);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildHelloRsp(uint32_t seq, CoprocTxMessage *outMessage)
{
    uint8_t capabilityBitmap;
    uint8_t sensorBitmap;
    size_t payloadLength = COPROC_HELLO_BASE_PAYLOAD_LEN;

    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    capabilityBitmap = COPROC_CAPABILITY_MOTION | COPROC_CAPABILITY_POWER;
    sensorBitmap = 0u;
#if (APP_TOUCH_ENABLE != 0U)
    capabilityBitmap |= COPROC_CAPABILITY_TOUCH;
    sensorBitmap |= COPROC_SENSOR_TOUCH;
#endif

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_HELLO_RSP,
                                COPROC_FRAME_FLAG_RESP,
                                seq,
                                0u);

    outMessage->frame.payload[0] = COPROC_DEVICE_ROLE_STM32_COPROC;
    outMessage->frame.payload[1] = COPROC_FW_VERSION_MAJOR;
    outMessage->frame.payload[2] = COPROC_FW_VERSION_MINOR;
    outMessage->frame.payload[3] = COPROC_FW_VERSION_PATCH;
    outMessage->frame.payload[4] = COPROC_HW_VERSION;
    outMessage->frame.payload[5] = capabilityBitmap;
    outMessage->frame.payload[6] = sensorBitmap;
    outMessage->frame.payload[7] = COPROC_BOOT_REASON_POWER_ON;
    outMessage->frame.payload[8] = COPROC_DEFAULT_STREAM_PROFILE_V1;

    payloadLength = CoprocTxBuilder_WriteTlvString(outMessage->frame.payload,
                                                   payloadLength,
                                                   COPROC_HELLO_TLV_GIT_BRANCH,
                                                   WATCHER_BUILD_GIT_BRANCH);
    payloadLength = CoprocTxBuilder_WriteTlvString(outMessage->frame.payload,
                                                   payloadLength,
                                                   COPROC_HELLO_TLV_GIT_COMMIT,
                                                   WATCHER_BUILD_GIT_COMMIT);
    payloadLength = CoprocTxBuilder_WriteTlvU8(outMessage->frame.payload,
                                               payloadLength,
                                               COPROC_HELLO_TLV_GIT_DIRTY,
                                               WATCHER_BUILD_GIT_DIRTY != 0 ? 1u : 0u);
    outMessage->frame.header.payloadLength = (uint16_t)payloadLength;
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildMotionDone(uint32_t seq,
                                             uint32_t refSeq,
                                             uint8_t resultCode,
                                             int16_t finalXDegX10,
                                             int16_t finalYDegX10,
                                             uint16_t execTimeMs,
                                             CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_MOTION_DONE,
                                COPROC_FRAME_FLAG_FINAL,
                                seq,
                                COPROC_PAYLOAD_LEN_MOTION_DONE);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, refSeq);
    outMessage->frame.payload[4] = resultCode;
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[5], finalXDegX10);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[7], finalYDegX10);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[9], execTimeMs);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildMotionState(uint32_t seq,
                                              uint32_t timestampMs,
                                              uint8_t validMask,
                                              int16_t xDegX10,
                                              int16_t yDegX10,
                                              uint16_t servo1Raw,
                                              uint16_t servo2Raw,
                                              CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_MOTION_STATE,
                                0u,
                                seq,
                                COPROC_PAYLOAD_LEN_MOTION_STATE);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, timestampMs);
    outMessage->frame.payload[4] = validMask;
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[5], xDegX10);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[7], yDegX10);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[9], servo1Raw);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[11], servo2Raw);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildServoFeedbackRsp(uint32_t seq,
                                                   uint8_t validMask,
                                                   uint16_t servo1Raw,
                                                   int16_t servo1DegX10,
                                                   uint16_t servo2Raw,
                                                   int16_t servo2DegX10,
                                                   uint32_t timestampMs,
                                                   CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_FEEDBACK_RSP,
                                COPROC_FRAME_FLAG_RESP | COPROC_FRAME_FLAG_FINAL,
                                seq,
                                COPROC_PAYLOAD_LEN_SERVO_FEEDBACK_RSP);
    outMessage->frame.payload[0] = validMask;
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[1], servo1Raw);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[3], servo1DegX10);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[5], servo2Raw);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[7], servo2DegX10);
    CoprocTxBuilder_WriteU32Le(&outMessage->frame.payload[9], timestampMs);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildLedDone(uint32_t seq,
                                          uint32_t refSeq,
                                          uint8_t resultCode,
                                          CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_LED_DONE,
                                COPROC_FRAME_FLAG_FINAL,
                                seq,
                                COPROC_PAYLOAD_LEN_LED_DONE);
    CoprocTxBuilder_WriteU32Le(outMessage->frame.payload, refSeq);
    outMessage->frame.payload[4] = resultCode;
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildTouchEvent(uint32_t seq,
                                             uint8_t touchId,
                                             uint8_t eventCode,
                                             uint32_t timestampMs,
                                             CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SENSOR,
                                COPROC_SENSOR_MSG_TOUCH_EVENT,
                                0u,
                                seq,
                                COPROC_PAYLOAD_LEN_TOUCH_EVENT);
    outMessage->frame.payload[0] = touchId;
    outMessage->frame.payload[1] = eventCode;
    CoprocTxBuilder_WriteU32Le(&outMessage->frame.payload[2], timestampMs);
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildMagState(uint32_t seq,
                                           uint16_t headingDegX100,
                                           uint16_t fieldNormUt,
                                           uint8_t quality,
                                           uint8_t statusBits,
                                           CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SENSOR,
                                COPROC_SENSOR_MSG_MAG_STATE,
                                0u,
                                seq,
                                COPROC_PAYLOAD_LEN_MAG_STATE);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[0], headingDegX100);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[2], fieldNormUt);
    outMessage->frame.payload[4] = quality;
    outMessage->frame.payload[5] = statusBits;
    return CoprocTxBuilder_EncodeWire(outMessage);
}

CoprocStatus CoprocTxBuilder_BuildImuState(uint32_t seq,
                                           int16_t rollDegX100,
                                           int16_t pitchDegX100,
                                           int16_t yawDegX100,
                                           uint16_t accNormMg,
                                           uint16_t gyroNormDpsX10,
                                           uint8_t motionFlags,
                                           CoprocTxMessage *outMessage)
{
    if (outMessage == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    CoprocFrameCodec_InitHeader(&outMessage->frame.header,
                                COPROC_MSG_CLASS_SENSOR,
                                COPROC_SENSOR_MSG_IMU_STATE,
                                0u,
                                seq,
                                COPROC_PAYLOAD_LEN_IMU_STATE);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[0], rollDegX100);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[2], pitchDegX100);
    CoprocTxBuilder_WriteI16Le(&outMessage->frame.payload[4], yawDegX100);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[6], accNormMg);
    CoprocTxBuilder_WriteU16Le(&outMessage->frame.payload[8], gyroNormDpsX10);
    outMessage->frame.payload[10] = motionFlags;
    return CoprocTxBuilder_EncodeWire(outMessage);
}
