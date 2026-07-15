#include "coproc_cobs.h"
#include "coproc_crc16.h"
#include "coproc_frame_codec.h"
#include "coproc_framing.h"
#include "coproc_protocol.h"
#include "coproc_ring_buffer.h"
#include "coproc_runtime.h"
#include "coproc_stress_sim.h"
#include "coproc_tx_builder.h"
#include "watcher_build_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_testFailures = 0;

#define ASSERT_TRUE(expr)                                                                                               \
    do {                                                                                                                \
        if (!(expr)) {                                                                                                  \
            fprintf(stderr, "ASSERT_TRUE failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                            \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

#define ASSERT_EQ_U32(actual, expected)                                                                                 \
    do {                                                                                                                \
        if ((uint32_t)(actual) != (uint32_t)(expected)) {                                                               \
            fprintf(stderr, "ASSERT_EQ_U32 failed: got=%lu expected=%lu (%s:%d)\n",                                    \
                    (unsigned long)(actual), (unsigned long)(expected), __FILE__, __LINE__);                           \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

#define ASSERT_EQ_SIZE(actual, expected)                                                                                \
    do {                                                                                                                \
        if ((size_t)(actual) != (size_t)(expected)) {                                                                   \
            fprintf(stderr, "ASSERT_EQ_SIZE failed: got=%zu expected=%zu (%s:%d)\n",                                   \
                    (size_t)(actual), (size_t)(expected), __FILE__, __LINE__);                                         \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

#define ASSERT_EQ_STATUS(actual, expected)                                                                              \
    do {                                                                                                                \
        if ((actual) != (expected)) {                                                                                   \
            fprintf(stderr, "ASSERT_EQ_STATUS failed: got=%s expected=%s (%s:%d)\n",                                   \
                    CoprocStatus_ToString(actual), CoprocStatus_ToString(expected), __FILE__, __LINE__);               \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

typedef struct
{
    uint8_t message[64][COPROC_FRAME_MAX_WIRE_SIZE];
    size_t length[64];
    size_t count;
} TxCapture;

typedef struct
{
    size_t failOnCall;
    size_t callCount;
} TxFailureCapture;

typedef struct
{
    CoprocProtocolObsEvent events[16];
    size_t count;
} ObsCapture;

typedef struct
{
    uint32_t nowMs;
    uint8_t servoSetCount;
    uint8_t servoStopCount;
    uint8_t servoReleaseCount;
    uint8_t feedbackReadCount;
    uint8_t feedbackFail;
    uint8_t ledRgbCount;
    uint8_t ledBreatheCount;
    uint8_t ledCountSetCount;
    uint8_t ledOffCount;
    uint8_t bottomLedRgbCount;
    uint8_t bottomLedBreatheCount;
    uint8_t bottomLedCountSetCount;
    uint8_t bottomLedOffCount;
    uint8_t lastServoIndex;
    uint8_t lastServoAngle;
    int16_t lastServoDegX10;
    uint8_t commandAngle[3];
    uint8_t feedbackAngle[3];
    uint8_t lastLedCount;
    uint8_t lastBottomLedCount;
    uint8_t lastRed;
    uint8_t lastGreen;
    uint8_t lastBlue;
    uint8_t lastBottomRed;
    uint8_t lastBottomGreen;
    uint8_t lastBottomBlue;
    uint8_t lastStep;
    uint8_t lastIntervalMs;
    uint8_t powerSetCount;
    uint8_t lastPowerEnabled;
    uint8_t lastPowerSourceTag;
    uint8_t touchRawState;
} RuntimeStub;

static CoprocStatus Test_DecodeWireMessage(const uint8_t *wire, size_t wireLength, CoprocFrame *frame, size_t *payloadLength);
static CoprocStatus Test_EncodeWireFrame(const CoprocFrameHeader *header,
                                         const uint8_t *payload,
                                         uint8_t *wire,
                                         size_t wireLength,
                                         size_t *encodedLength);

static CoprocStatus Test_TxCapture(void *ctx, const uint8_t *data, size_t length)
{
    TxCapture *capture = (TxCapture *)ctx;

    if (capture == NULL || data == NULL || capture->count >= 64u || length > COPROC_FRAME_MAX_WIRE_SIZE) {
        return COPROC_STATUS_INVALID_ARG;
    }

    memcpy(capture->message[capture->count], data, length);
    capture->length[capture->count] = length;
    capture->count++;
    return COPROC_STATUS_OK;
}

static CoprocStatus Test_TxFailOnCall(void *ctx, const uint8_t *data, size_t length)
{
    TxFailureCapture *capture = (TxFailureCapture *)ctx;

    (void)data;
    (void)length;
    if (capture == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    capture->callCount++;
    if (capture->callCount == capture->failOnCall) {
        return COPROC_STATUS_INVALID_STATE;
    }

    return COPROC_STATUS_OK;
}

static void Test_ObsCapture(void *ctx, const CoprocProtocolObsEvent *event)
{
    ObsCapture *capture = (ObsCapture *)ctx;

    if (capture == NULL || event == NULL || capture->count >= 16u) {
        return;
    }

    capture->events[capture->count++] = *event;
}

static size_t Test_CountObsType(const ObsCapture *capture, CoprocProtocolObsEventType type)
{
    size_t count = 0u;

    if (capture == NULL) {
        return 0u;
    }

    for (size_t index = 0u; index < capture->count; ++index) {
        if (capture->events[index].type == type) {
            count++;
        }
    }

    return count;
}

static size_t Test_CountDecodedFramesByType(const TxCapture *capture, uint8_t msgClass, uint8_t msgId)
{
    size_t count = 0u;

    if (capture == NULL) {
        return 0u;
    }

    for (size_t index = 0u; index < capture->count; ++index) {
        CoprocFrame frame;
        size_t payloadLength = 0u;

        if (Test_DecodeWireMessage(capture->message[index], capture->length[index], &frame, &payloadLength) != COPROC_STATUS_OK) {
            continue;
        }

        if (frame.header.msgClass == msgClass && frame.header.msgId == msgId) {
            count++;
        }
    }

    return count;
}

static const uint8_t *Test_FindHelloTlv(const CoprocFrame *frame, uint8_t type, size_t *outLength)
{
    size_t offset = COPROC_HELLO_BASE_PAYLOAD_LEN;

    if (outLength != NULL) {
        *outLength = 0u;
    }
    if (frame == NULL || frame->header.payloadLength < COPROC_HELLO_BASE_PAYLOAD_LEN) {
        return NULL;
    }

    while ((offset + 2u) <= frame->header.payloadLength) {
        uint8_t itemType = frame->payload[offset++];
        uint8_t itemLength = frame->payload[offset++];

        if ((offset + itemLength) > frame->header.payloadLength) {
            return NULL;
        }
        if (itemType == type) {
            if (outLength != NULL) {
                *outLength = itemLength;
            }
            return &frame->payload[offset];
        }
        offset += itemLength;
    }

    return NULL;
}

static uint32_t Test_AllocSeq(void *ctx)
{
    uint32_t *nextSeq = (uint32_t *)ctx;
    uint32_t seq = 0u;

    if (nextSeq == NULL) {
        return 0u;
    }

    seq = *nextSeq;
    (*nextSeq)++;
    return seq;
}

static uint32_t Test_NowMs(void *ctx)
{
    return (ctx != NULL) ? *(uint32_t *)ctx : 0u;
}

static uint32_t Test_RuntimeNowMs(void *ctx)
{
    return (ctx != NULL) ? ((RuntimeStub *)ctx)->nowMs : 0u;
}

static uint8_t Test_RuntimeReadTouch(void *ctx)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    return (stub->touchRawState == 0u) ? 1u : 0u;
}

static uint8_t Test_RuntimeSetServoAngle(void *ctx, uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->servoSetCount++;
    stub->lastServoIndex = servoIndex;
    stub->lastServoAngle = angle;
    if (servoIndex < 3u) {
        stub->commandAngle[servoIndex] = angle;
        stub->feedbackAngle[servoIndex] = angle;
    }
    if (pulseUs != NULL) {
        *pulseUs = (uint16_t)(500u + (((uint16_t)angle * 2000u) / 180u));
    }
    return 1u;
}

static uint8_t Test_RuntimeSetServoDegX10(void *ctx, uint8_t servoIndex, int16_t degX10, uint16_t *pulseUs)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;
    uint8_t angle;

    if (stub == NULL || degX10 < 0 || degX10 > 1800) {
        return 0u;
    }

    angle = (uint8_t)(((uint16_t)degX10 + 5u) / 10u);
    stub->servoSetCount++;
    stub->lastServoIndex = servoIndex;
    stub->lastServoAngle = angle;
    stub->lastServoDegX10 = degX10;
    if (servoIndex < 3u) {
        stub->commandAngle[servoIndex] = angle;
        stub->feedbackAngle[servoIndex] = angle;
    }
    if (pulseUs != NULL) {
        *pulseUs = (uint16_t)(500u + (((uint32_t)(uint16_t)degX10 * 2000u + 900u) / 1800u));
    }
    return 1u;
}

static uint8_t Test_RuntimeStopServo(void *ctx, uint8_t servoIndex)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL || servoIndex == 0u) {
        return 0u;
    }

    stub->servoStopCount++;
    return 1u;
}

static uint8_t Test_RuntimeReleaseServo(void *ctx, uint8_t servoIndex)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL || servoIndex == 0u) {
        return 0u;
    }

    stub->servoReleaseCount++;
    stub->lastServoIndex = servoIndex;
    return 1u;
}

static uint8_t Test_RuntimeReadServoFeedback(void *ctx,
                                             uint8_t servoIndex,
                                             uint8_t *commandAngle,
                                             uint16_t *feedbackRaw,
                                             uint16_t *feedbackMv,
                                             uint8_t *feedbackAngle)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;
    uint8_t angle;

    if (stub == NULL || servoIndex == 0u || servoIndex >= 3u || commandAngle == NULL || feedbackRaw == NULL ||
        feedbackMv == NULL || feedbackAngle == NULL || stub->feedbackFail != 0u) {
        return 0u;
    }

    stub->feedbackReadCount++;
    angle = stub->feedbackAngle[servoIndex];
    *commandAngle = stub->commandAngle[servoIndex];
    *feedbackRaw = (uint16_t)(204u + (((uint16_t)angle * (1033u - 204u)) / 180u));
    *feedbackMv = (uint16_t)(((uint32_t)*feedbackRaw * 3300u) / 4095u);
    *feedbackAngle = angle;
    return 1u;
}

static uint8_t Test_RuntimeSetLedCount(void *ctx, uint16_t ledCount)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL || ledCount == 0u || ledCount > 16u) {
        return 0u;
    }

    stub->ledCountSetCount++;
    stub->lastLedCount = (uint8_t)ledCount;
    return 1u;
}

static uint16_t Test_RuntimeGetLedMaxCount(void *ctx)
{
    (void)ctx;

    return 16u;
}

static uint8_t Test_RuntimeSetBottomLedCount(void *ctx, uint16_t ledCount)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL || ledCount == 0u || ledCount > 40u) {
        return 0u;
    }

    stub->bottomLedCountSetCount++;
    stub->lastBottomLedCount = (uint8_t)ledCount;
    return 1u;
}

static uint16_t Test_RuntimeGetBottomLedMaxCount(void *ctx)
{
    (void)ctx;

    return 40u;
}

static uint8_t Test_RuntimeSetLedRgb(void *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->ledRgbCount++;
    stub->lastRed = red;
    stub->lastGreen = green;
    stub->lastBlue = blue;
    return 1u;
}

static uint8_t Test_RuntimeSetBottomLedRgb(void *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->bottomLedRgbCount++;
    stub->lastBottomRed = red;
    stub->lastBottomGreen = green;
    stub->lastBottomBlue = blue;
    return 1u;
}

static uint8_t Test_RuntimeStartLedBreathe(void *ctx,
                                           uint8_t red,
                                           uint8_t green,
                                           uint8_t blue,
                                           uint8_t step,
                                           uint8_t intervalMs)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->ledBreatheCount++;
    stub->lastRed = red;
    stub->lastGreen = green;
    stub->lastBlue = blue;
    stub->lastStep = step;
    stub->lastIntervalMs = intervalMs;
    return 1u;
}

static uint8_t Test_RuntimeStartBottomLedBreathe(void *ctx,
                                                 uint8_t red,
                                                 uint8_t green,
                                                 uint8_t blue,
                                                 uint8_t step,
                                                 uint8_t intervalMs)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->bottomLedBreatheCount++;
    stub->lastBottomRed = red;
    stub->lastBottomGreen = green;
    stub->lastBottomBlue = blue;
    stub->lastStep = step;
    stub->lastIntervalMs = intervalMs;
    return 1u;
}

static void Test_RuntimeStopLedEffect(void *ctx)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub != NULL) {
        stub->ledOffCount++;
    }
}

static void Test_RuntimeStopBottomLedEffect(void *ctx)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub != NULL) {
        stub->bottomLedOffCount++;
    }
}

static uint8_t Test_RuntimeSetPower5V(void *ctx, uint8_t enabled, uint8_t sourceTag)
{
    RuntimeStub *stub = (RuntimeStub *)ctx;

    if (stub == NULL) {
        return 0u;
    }

    stub->powerSetCount++;
    stub->lastPowerEnabled = enabled;
    stub->lastPowerSourceTag = sourceTag;
    return 1u;
}

static void Test_RuntimeDispatchObs(void *ctx, const CoprocDispatchEvent *event)
{
    ObsCapture *capture = (ObsCapture *)ctx;

    if (capture == NULL || event == NULL || capture->count >= 16u) {
        return;
    }

    memset(&capture->events[capture->count], 0, sizeof(capture->events[capture->count]));
    switch (event->type) {
        case COPROC_DISPATCH_EVENT_ACK:
            capture->events[capture->count].type = COPROC_OBS_EVENT_ACK;
            break;
        case COPROC_DISPATCH_EVENT_NACK:
            capture->events[capture->count].type = COPROC_OBS_EVENT_NACK;
            break;
        case COPROC_DISPATCH_EVENT_MOTION_DONE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_MOTION_DONE;
            break;
        case COPROC_DISPATCH_EVENT_MOTION_STATE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_MOTION_STATE;
            break;
        case COPROC_DISPATCH_EVENT_LED_SET_RGB:
            capture->events[capture->count].type = COPROC_OBS_EVENT_LED_SET_RGB;
            break;
        case COPROC_DISPATCH_EVENT_LED_BREATHE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_LED_BREATHE;
            break;
        case COPROC_DISPATCH_EVENT_LED_DONE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_LED_DONE;
            break;
        case COPROC_DISPATCH_EVENT_TOUCH_EVENT:
            capture->events[capture->count].type = COPROC_OBS_EVENT_TOUCH_EVENT;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_ENABLE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_POWER_5V_ENABLE;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_DISABLE:
            capture->events[capture->count].type = COPROC_OBS_EVENT_POWER_5V_DISABLE;
            break;
        default:
            return;
    }
    capture->events[capture->count].header = event->header;
    capture->events[capture->count].refSeq = event->refSeq;
    capture->events[capture->count].reasonCode = event->reasonCode;
    capture->events[capture->count].motionResult = event->motionResult;
    capture->events[capture->count].ledResult = event->ledResult;
    capture->events[capture->count].touchCode = event->touchCode;
    capture->events[capture->count].timestampMs = event->timestampMs;
    capture->events[capture->count].sourceTag = event->sourceTag;
    capture->events[capture->count].powerEnabled = event->powerEnabled;
    capture->count++;
}

static void Test_InitRuntime(CoprocRuntime *runtime, RuntimeStub *stub)
{
    CoprocRuntimeConfig config;

    memset(&config, 0, sizeof(config));
    config.nowMsFn = Test_RuntimeNowMs;
    config.nowMsCtx = stub;
    config.readTouchFn = Test_RuntimeReadTouch;
    config.readTouchCtx = stub;
    config.setServoAngleFn = Test_RuntimeSetServoAngle;
    config.setServoAngleCtx = stub;
    config.setServoDegX10Fn = Test_RuntimeSetServoDegX10;
    config.setServoDegX10Ctx = stub;
    config.stopServoFn = Test_RuntimeStopServo;
    config.stopServoCtx = stub;
    config.releaseServoFn = Test_RuntimeReleaseServo;
    config.releaseServoCtx = stub;
    config.readServoFeedbackFn = Test_RuntimeReadServoFeedback;
    config.readServoFeedbackCtx = stub;
    config.setPower5VFn = Test_RuntimeSetPower5V;
    config.setPower5VCtx = stub;
    config.touchDebounceMs = 30u;
    stub->touchRawState = 1u;
    stub->commandAngle[1] = 90u;
    stub->commandAngle[2] = 90u;
    stub->feedbackAngle[1] = 90u;
    stub->feedbackAngle[2] = 90u;
    CoprocRuntime_Init(runtime, &config);
}

static void Test_InitRuntimeWithLed(CoprocRuntime *runtime, RuntimeStub *stub)
{
    CoprocRuntimeConfig config;

    memset(&config, 0, sizeof(config));
    config.nowMsFn = Test_RuntimeNowMs;
    config.nowMsCtx = stub;
    config.setLedCountFn = Test_RuntimeSetLedCount;
    config.setLedCountCtx = stub;
    config.getLedMaxCountFn = Test_RuntimeGetLedMaxCount;
    config.getLedMaxCountCtx = stub;
    config.setLedRgbFn = Test_RuntimeSetLedRgb;
    config.setLedRgbCtx = stub;
    config.startLedBreatheFn = Test_RuntimeStartLedBreathe;
    config.startLedBreatheCtx = stub;
    config.stopLedEffectFn = Test_RuntimeStopLedEffect;
    config.stopLedEffectCtx = stub;
    config.setBottomLedCountFn = Test_RuntimeSetBottomLedCount;
    config.setBottomLedCountCtx = stub;
    config.getBottomLedMaxCountFn = Test_RuntimeGetBottomLedMaxCount;
    config.getBottomLedMaxCountCtx = stub;
    config.setBottomLedRgbFn = Test_RuntimeSetBottomLedRgb;
    config.setBottomLedRgbCtx = stub;
    config.startBottomLedBreatheFn = Test_RuntimeStartBottomLedBreathe;
    config.startBottomLedBreatheCtx = stub;
    config.stopBottomLedEffectFn = Test_RuntimeStopBottomLedEffect;
    config.stopBottomLedEffectCtx = stub;
    CoprocRuntime_Init(runtime, &config);
}

static CoprocStatus Test_DecodeWireMessage(const uint8_t *wire, size_t wireLength, CoprocFrame *frame, size_t *payloadLength)
{
    uint8_t raw[COPROC_FRAME_MAX_RAW_SIZE];
    size_t rawLength = 0u;
    CoprocStatus status;

    if (wire == NULL || frame == NULL || payloadLength == NULL || wireLength == 0u) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (wire[wireLength - 1u] != 0u) {
        return COPROC_STATUS_INVALID_ARG;
    }
    status = CoprocCobs_Decode(wire, wireLength - 1u, raw, sizeof(raw), &rawLength);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    return CoprocFrameCodec_Unpack(raw, rawLength, frame, payloadLength);
}

static CoprocStatus Test_EncodeWireFrame(const CoprocFrameHeader *header,
                                         const uint8_t *payload,
                                         uint8_t *wire,
                                         size_t wireLength,
                                         size_t *encodedLength)
{
    uint8_t raw[COPROC_FRAME_MAX_RAW_SIZE];
    size_t rawLength = 0u;
    size_t cobsLength = 0u;
    CoprocStatus status;

    status = CoprocFrameCodec_Pack(header, payload, raw, sizeof(raw), &rawLength);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocCobs_Encode(raw, rawLength, wire, wireLength, &cobsLength);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    wire[cobsLength] = 0u;
    *encodedLength = cobsLength + 1u;
    return COPROC_STATUS_OK;
}

static void test_ring_buffer_write_read_reset(void)
{
    uint8_t storage[8];
    uint8_t value = 0u;
    uint8_t data[] = {0x11u, 0x22u, 0x33u};
    CoprocRingBuffer buffer;

    CoprocRingBuffer_Init(&buffer, storage, sizeof(storage));
    ASSERT_EQ_SIZE(CoprocRingBuffer_Count(&buffer), 0u);
    ASSERT_EQ_STATUS(CoprocRingBuffer_Write(&buffer, data, sizeof(data)), COPROC_STATUS_OK);
    ASSERT_EQ_SIZE(CoprocRingBuffer_Count(&buffer), sizeof(data));
    ASSERT_EQ_STATUS(CoprocRingBuffer_ReadByte(&buffer, &value), COPROC_STATUS_OK);
    ASSERT_EQ_U32(value, 0x11u);
    CoprocRingBuffer_Reset(&buffer);
    ASSERT_EQ_SIZE(CoprocRingBuffer_Count(&buffer), 0u);
}

static void test_ring_buffer_overflow(void)
{
    uint8_t storage[4];
    uint8_t data[] = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
    CoprocRingBuffer buffer;

    CoprocRingBuffer_Init(&buffer, storage, sizeof(storage));
    ASSERT_EQ_STATUS(CoprocRingBuffer_Write(&buffer, data, sizeof(storage)), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocRingBuffer_Write(&buffer, data, sizeof(data)), COPROC_STATUS_NO_MEM);
}

static void test_framing_delimiter_and_truncate(void)
{
    CoprocFraming framing;
    size_t candidateLength = 0u;
    CoprocFraming_Init(&framing);

    ASSERT_TRUE(CoprocFraming_ConsumeByte(&framing, 0x11u, &candidateLength) == COPROC_FRAMING_RESULT_NONE);
    ASSERT_TRUE(CoprocFraming_ConsumeByte(&framing, 0x22u, &candidateLength) == COPROC_FRAMING_RESULT_NONE);
    ASSERT_TRUE(CoprocFraming_ConsumeByte(&framing, 0x00u, &candidateLength) == COPROC_FRAMING_RESULT_CANDIDATE_READY);
    ASSERT_EQ_SIZE(candidateLength, 2u);
    ASSERT_EQ_U32(CoprocFraming_GetCandidate(&framing)[0], 0x11u);
    ASSERT_EQ_U32(CoprocFraming_GetCandidate(&framing)[1], 0x22u);
    ASSERT_TRUE(CoprocFraming_ConsumeByte(&framing, 0x00u, &candidateLength) == COPROC_FRAMING_RESULT_FRAME_DROPPED);

    for (size_t index = 0u; index < COPROC_FRAME_MAX_WIRE_SIZE; ++index) {
        (void)CoprocFraming_ConsumeByte(&framing, 0x33u, &candidateLength);
    }
    ASSERT_TRUE(CoprocFraming_ConsumeByte(&framing, 0x00u, &candidateLength) == COPROC_FRAMING_RESULT_FRAME_DROPPED);
}

static void test_cobs_round_trip_and_invalid(void)
{
    uint8_t input[] = {0x11u, 0x00u, 0x22u, 0x33u, 0x00u};
    uint8_t encoded[32];
    uint8_t decoded[32];
    size_t encodedLength = 0u;
    size_t decodedLength = 0u;

    ASSERT_EQ_STATUS(CoprocCobs_Encode(input, sizeof(input), encoded, sizeof(encoded), &encodedLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocCobs_Decode(encoded, encodedLength, decoded, sizeof(decoded), &decodedLength), COPROC_STATUS_OK);
    ASSERT_EQ_SIZE(decodedLength, sizeof(input));
    ASSERT_TRUE(memcmp(decoded, input, sizeof(input)) == 0);

    ASSERT_EQ_STATUS(CoprocCobs_Decode((const uint8_t[]){0x03u, 0x11u}, 2u, decoded, sizeof(decoded), &decodedLength),
                     COPROC_STATUS_INVALID_SIZE);
}

static void test_crc_vector(void)
{
    static const char vector[] = "123456789";
    ASSERT_EQ_U32(CoprocCrc16_Compute(vector, 9u), 0x29B1u);
}

static void test_frame_pack_unpack_and_crc_fail(void)
{
    CoprocFrameHeader header;
    CoprocFrame frame;
    uint8_t buffer[COPROC_FRAME_MAX_RAW_SIZE];
    uint8_t payload[] = {0xAAu, 0x55u, 0x10u};
    size_t packedLength = 0u;
    size_t payloadLength = 0u;

    CoprocFrameCodec_InitHeader(&header, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_HELLO_REQ, COPROC_FRAME_FLAG_ACK_REQ, 7u, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocFrameCodec_Pack(&header, payload, buffer, sizeof(buffer), &packedLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocFrameCodec_Unpack(buffer, packedLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.seq, 7u);
    ASSERT_EQ_SIZE(payloadLength, sizeof(payload));
    ASSERT_TRUE(memcmp(frame.payload, payload, sizeof(payload)) == 0);

    buffer[packedLength - 1u] ^= 0xFFu;
    ASSERT_EQ_STATUS(CoprocFrameCodec_Unpack(buffer, packedLength, &frame, &payloadLength), COPROC_STATUS_INVALID_CRC);
}

static void test_tx_builder_vectors(void)
{
    CoprocTxMessage ackMessage;
    CoprocTxMessage nackMessage;
    CoprocTxMessage faultMessage;
    CoprocTxMessage helloRspMessage;
    CoprocFrame frame;
    size_t payloadLength = 0u;
    const uint8_t *tlvValue;
    size_t tlvLength = 0u;

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildAck(10u, 42u, &ackMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(ackMessage.wire, ackMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SYS_MSG_ACK);
    ASSERT_EQ_U32(frame.header.flags, COPROC_FRAME_FLAG_RESP);
    ASSERT_EQ_U32(frame.payload[0], 42u);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildNack(11u, 43u, COPROC_NACK_REASON_UNSUPPORTED_MSG, &nackMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(nackMessage.wire, nackMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SYS_MSG_NACK);
    ASSERT_EQ_U32(frame.header.flags, COPROC_FRAME_FLAG_RESP | COPROC_FRAME_FLAG_FINAL);
    ASSERT_EQ_U32(frame.payload[6], COPROC_NACK_REASON_UNSUPPORTED_MSG & 0xFFu);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildFault(12u, 44u, COPROC_FAULT_SOURCE_LINK, 0x1234u, 0x5678u, &faultMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(faultMessage.wire, faultMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SYS_MSG_FAULT);
    ASSERT_EQ_U32(frame.payload[4], COPROC_FAULT_SOURCE_LINK);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildHelloRsp(13u, &helloRspMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(helloRspMessage.wire, helloRspMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SYS_MSG_HELLO_RSP);
    ASSERT_TRUE(payloadLength >= COPROC_HELLO_BASE_PAYLOAD_LEN);
    ASSERT_EQ_U32(frame.payload[5], COPROC_CAPABILITY_MOTION | COPROC_CAPABILITY_TOUCH | COPROC_CAPABILITY_POWER);
    ASSERT_EQ_U32(frame.payload[6], COPROC_SENSOR_TOUCH);
    ASSERT_EQ_U32(frame.payload[8], COPROC_DEFAULT_STREAM_PROFILE_V1);
    tlvValue = Test_FindHelloTlv(&frame, COPROC_HELLO_TLV_GIT_BRANCH, &tlvLength);
    ASSERT_TRUE(tlvValue != NULL);
    ASSERT_EQ_SIZE(tlvLength, strlen(WATCHER_BUILD_GIT_BRANCH));
    ASSERT_TRUE(memcmp(tlvValue, WATCHER_BUILD_GIT_BRANCH, tlvLength) == 0);
    tlvValue = Test_FindHelloTlv(&frame, COPROC_HELLO_TLV_GIT_COMMIT, &tlvLength);
    ASSERT_TRUE(tlvValue != NULL);
    ASSERT_EQ_SIZE(tlvLength, strlen(WATCHER_BUILD_GIT_COMMIT));
    ASSERT_TRUE(memcmp(tlvValue, WATCHER_BUILD_GIT_COMMIT, tlvLength) == 0);
    tlvValue = Test_FindHelloTlv(&frame, COPROC_HELLO_TLV_GIT_DIRTY, &tlvLength);
    ASSERT_TRUE(tlvValue != NULL);
    ASSERT_EQ_SIZE(tlvLength, 1u);
    ASSERT_EQ_U32(tlvValue[0], WATCHER_BUILD_GIT_DIRTY != 0 ? 1u : 0u);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildMotionDone(14u, 88u, COPROC_MOTION_RESULT_SUCCESS, 600, 1100, 180u, &ackMessage),
                     COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(ackMessage.wire, ackMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgClass, COPROC_MSG_CLASS_MOTION);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_MOTION_MSG_MOTION_DONE);
    ASSERT_EQ_U32(payloadLength, COPROC_PAYLOAD_LEN_MOTION_DONE);
    ASSERT_EQ_U32(frame.payload[4], COPROC_MOTION_RESULT_SUCCESS);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildTouchEvent(15u, 0u, COPROC_TOUCH_EVENT_PRESS, 1234u, &nackMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(nackMessage.wire, nackMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgClass, COPROC_MSG_CLASS_SENSOR);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SENSOR_MSG_TOUCH_EVENT);
    ASSERT_EQ_U32(payloadLength, COPROC_PAYLOAD_LEN_TOUCH_EVENT);
    ASSERT_EQ_U32(frame.payload[1], COPROC_TOUCH_EVENT_PRESS);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildMagState(16u, 9000u, 330u, 99u, 0x03u, &faultMessage), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(faultMessage.wire, faultMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SENSOR_MSG_MAG_STATE);
    ASSERT_EQ_U32(payloadLength, COPROC_PAYLOAD_LEN_MAG_STATE);
    ASSERT_EQ_U32(frame.payload[4], 99u);

    ASSERT_EQ_STATUS(CoprocTxBuilder_BuildImuState(17u, -1200, 600, 3000, 980u, 125u, 0x02u, &helloRspMessage),
                     COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(helloRspMessage.wire, helloRspMessage.wireLength, &frame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(frame.header.msgId, COPROC_SENSOR_MSG_IMU_STATE);
    ASSERT_EQ_U32(payloadLength, COPROC_PAYLOAD_LEN_IMU_STATE);
    ASSERT_EQ_U32(frame.payload[10], 0x02u);
}

static void test_protocol_hello_req_ack_hello_rsp(void)
{
    CoprocProtocol protocol;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrameHeader header;
    uint8_t wire[COPROC_FRAME_MAX_WIRE_SIZE];
    size_t wireLength = 0u;
    CoprocFrame ackFrame;
    CoprocFrame helloRspFrame;
    size_t payloadLength = 0u;
    const uint8_t *tlvValue;
    size_t tlvLength = 0u;
    const CoprocProtocolStats *stats;

    CoprocProtocol_Init(&protocol);
    CoprocFrameCodec_InitHeader(&header, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_HELLO_REQ, COPROC_FRAME_FLAG_ACK_REQ, 42u, 0u);
    ASSERT_EQ_STATUS(Test_EncodeWireFrame(&header, NULL, wire, sizeof(wire), &wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, wire, wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_Process(&protocol, Test_TxCapture, &txCapture, Test_ObsCapture, &obsCapture), COPROC_STATUS_OK);

    ASSERT_EQ_SIZE(txCapture.count, 2u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[0], txCapture.length[0], &ackFrame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(ackFrame.header.msgId, COPROC_SYS_MSG_ACK);
    ASSERT_EQ_U32(ackFrame.payload[0], 42u);

    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[1], txCapture.length[1], &helloRspFrame, &payloadLength), COPROC_STATUS_OK);
    ASSERT_EQ_U32(helloRspFrame.header.msgId, COPROC_SYS_MSG_HELLO_RSP);
    ASSERT_TRUE(payloadLength >= COPROC_HELLO_BASE_PAYLOAD_LEN);
    ASSERT_EQ_U32(helloRspFrame.payload[5], COPROC_CAPABILITY_MOTION | COPROC_CAPABILITY_TOUCH | COPROC_CAPABILITY_POWER);
    ASSERT_EQ_U32(helloRspFrame.payload[6], COPROC_SENSOR_TOUCH);
    ASSERT_EQ_U32(helloRspFrame.payload[8], COPROC_DEFAULT_STREAM_PROFILE_V1);
    tlvValue = Test_FindHelloTlv(&helloRspFrame, COPROC_HELLO_TLV_GIT_BRANCH, &tlvLength);
    ASSERT_TRUE(tlvValue != NULL);
    ASSERT_EQ_SIZE(tlvLength, strlen(WATCHER_BUILD_GIT_BRANCH));

    ASSERT_TRUE(obsCapture.count >= 5u);
    ASSERT_EQ_U32(obsCapture.events[0].type, COPROC_OBS_EVENT_FRAME_CANDIDATE);
    ASSERT_EQ_U32(obsCapture.events[1].type, COPROC_OBS_EVENT_FRAME_DECODE_OK);
    ASSERT_EQ_U32(obsCapture.events[2].type, COPROC_OBS_EVENT_HELLO_REQ);
    ASSERT_EQ_U32(obsCapture.events[3].type, COPROC_OBS_EVENT_ACK);
    ASSERT_EQ_U32(obsCapture.events[4].type, COPROC_OBS_EVENT_HELLO_RSP);

    stats = CoprocProtocol_GetStats(&protocol);
    ASSERT_EQ_U32(stats->frameCandidateCount, 1u);
    ASSERT_EQ_U32(stats->frameDecodeOk, 1u);
}

static void test_protocol_decode_fail_stats(void)
{
    CoprocProtocol protocol;
    ObsCapture obsCapture = {0};
    const CoprocProtocolStats *stats;
    uint8_t invalidCobs[] = {0x03u, 0x11u, 0x00u};
    uint8_t validWire[COPROC_FRAME_MAX_WIRE_SIZE];
    CoprocFrameHeader header;
    size_t validWireLength = 0u;

    CoprocProtocol_Init(&protocol);
    ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, invalidCobs, sizeof(invalidCobs)), COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&header, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_HELLO_REQ, COPROC_FRAME_FLAG_ACK_REQ, 7u, 0u);
    ASSERT_EQ_STATUS(Test_EncodeWireFrame(&header, NULL, validWire, sizeof(validWire), &validWireLength), COPROC_STATUS_OK);
    validWire[validWireLength - 2u] ^= 0x7Fu;
    ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, validWire, validWireLength), COPROC_STATUS_OK);

    ASSERT_EQ_STATUS(CoprocProtocol_Process(&protocol, NULL, NULL, Test_ObsCapture, &obsCapture), COPROC_STATUS_OK);
    stats = CoprocProtocol_GetStats(&protocol);
    ASSERT_EQ_U32(stats->cobsFail, 1u);
    ASSERT_EQ_U32(stats->crcFail, 1u);
    ASSERT_EQ_U32(stats->frameDecodeFail, 2u);
}

static void test_protocol_dispatch_fail_on_tx_failure(void)
{
    CoprocProtocol protocol;
    TxFailureCapture txFailure = {.failOnCall = 1u, .callCount = 0u};
    ObsCapture obsCapture = {0};
    CoprocFrameHeader header;
    uint8_t wire[COPROC_FRAME_MAX_WIRE_SIZE];
    size_t wireLength = 0u;
    const CoprocProtocolStats *stats;

    CoprocProtocol_Init(&protocol);
    CoprocFrameCodec_InitHeader(&header, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_HELLO_REQ, COPROC_FRAME_FLAG_ACK_REQ, 99u, 0u);
    ASSERT_EQ_STATUS(Test_EncodeWireFrame(&header, NULL, wire, sizeof(wire), &wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, wire, wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_Process(&protocol, Test_TxFailOnCall, &txFailure, Test_ObsCapture, &obsCapture),
                     COPROC_STATUS_INVALID_STATE);

    ASSERT_EQ_U32(txFailure.callCount, 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_HELLO_REQ), 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_ACK), 0u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_DISPATCH_FAIL), 1u);
    ASSERT_EQ_STATUS(obsCapture.events[obsCapture.count - 1u].status, COPROC_STATUS_INVALID_STATE);

    stats = CoprocProtocol_GetStats(&protocol);
    ASSERT_EQ_U32(stats->frameDecodeOk, 1u);
    ASSERT_EQ_U32(stats->dispatchFailCount, 1u);
}

static void test_protocol_strict_sys_payload_lengths(void)
{
    struct TestVector
    {
        uint8_t msgId;
        uint16_t payloadLength;
    };
    static const struct TestVector vectors[] = {
        {COPROC_SYS_MSG_ACK, 5u},
        {COPROC_SYS_MSG_NACK, 7u},
        {COPROC_SYS_MSG_FAULT, 8u},
        {COPROC_SYS_MSG_HELLO_RSP, 8u},
    };

    for (size_t index = 0u; index < (sizeof(vectors) / sizeof(vectors[0])); ++index) {
        CoprocProtocol protocol;
        ObsCapture obsCapture = {0};
        CoprocFrameHeader header;
        uint8_t payload[COPROC_FRAME_MAX_PAYLOAD_SIZE] = {0};
        uint8_t wire[COPROC_FRAME_MAX_WIRE_SIZE];
        size_t wireLength = 0u;
        const CoprocProtocolStats *stats;

        CoprocProtocol_Init(&protocol);
        CoprocFrameCodec_InitHeader(&header,
                                    COPROC_MSG_CLASS_SYS,
                                    vectors[index].msgId,
                                    COPROC_FRAME_FLAG_RESP,
                                    (uint32_t)(index + 1u),
                                    vectors[index].payloadLength);
        ASSERT_EQ_STATUS(Test_EncodeWireFrame(&header, payload, wire, sizeof(wire), &wireLength), COPROC_STATUS_OK);
        ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, wire, wireLength), COPROC_STATUS_OK);
        ASSERT_EQ_STATUS(CoprocProtocol_Process(&protocol, Test_TxCapture, NULL, Test_ObsCapture, &obsCapture),
                         COPROC_STATUS_INVALID_SIZE);

        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_FRAME_DECODE_OK), 1u);
        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_DISPATCH_FAIL), 1u);
        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_ACK), 0u);
        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_NACK), 0u);
        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_FAULT), 0u);
        ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_HELLO_RSP), 0u);
        ASSERT_EQ_STATUS(obsCapture.events[obsCapture.count - 1u].status, COPROC_STATUS_INVALID_SIZE);

        stats = CoprocProtocol_GetStats(&protocol);
        ASSERT_EQ_U32(stats->frameDecodeOk, 1u);
        ASSERT_EQ_U32(stats->dispatchFailCount, 1u);
    }
}

static void test_protocol_accepts_extended_hello_rsp_metadata(void)
{
    CoprocProtocol protocol;
    ObsCapture obsCapture = {0};
    CoprocFrameHeader header;
    uint8_t payload[COPROC_FRAME_MAX_PAYLOAD_SIZE] = {
        COPROC_DEVICE_ROLE_STM32_COPROC,
        COPROC_FW_VERSION_MAJOR,
        COPROC_FW_VERSION_MINOR,
        COPROC_FW_VERSION_PATCH,
        COPROC_HW_VERSION,
        COPROC_CAPABILITY_MOTION,
        0u,
        COPROC_BOOT_REASON_POWER_ON,
        COPROC_DEFAULT_STREAM_PROFILE_V1,
    };
    uint8_t wire[COPROC_FRAME_MAX_WIRE_SIZE];
    size_t wireLength = 0u;
    size_t payloadLength = COPROC_HELLO_BASE_PAYLOAD_LEN;

    payload[payloadLength++] = COPROC_HELLO_TLV_GIT_BRANCH;
    payload[payloadLength++] = 5u;
    memcpy(&payload[payloadLength], "dev-x", 5u);
    payloadLength += 5u;

    CoprocProtocol_Init(&protocol);
    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_HELLO_RSP,
                                COPROC_FRAME_FLAG_RESP,
                                77u,
                                (uint16_t)payloadLength);
    ASSERT_EQ_STATUS(Test_EncodeWireFrame(&header, payload, wire, sizeof(wire), &wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_IngestBytes(&protocol, wire, wireLength), COPROC_STATUS_OK);
    ASSERT_EQ_STATUS(CoprocProtocol_Process(&protocol, Test_TxCapture, NULL, Test_ObsCapture, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_FRAME_DECODE_OK), 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_DISPATCH_FAIL), 0u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_HELLO_RSP), 1u);
}

static void test_runtime_servo_move_delayed_motion_done(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0x64u, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    runtime.config.servoFeedbackStateEnabled = 1u;
    runtime.config.servoFeedbackStateIntervalMs = 1000u;
    runtime.lastServoFeedbackStateTxMs = 1u;
    stub.feedbackAngle[2] = 30u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                41u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 90u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);

    stub.nowMs = 99u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
    ASSERT_TRUE(stub.lastServoAngle >= 60u);

    stub.nowMs = 100u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[1], txCapture.length[1], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 41u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_SUCCESS);
    ASSERT_EQ_U32((uint16_t)decoded.payload[5] | ((uint16_t)decoded.payload[6] << 8u), 600u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->motionDoneTxCount, 1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_servo_move_ease_in_out_profile(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0xDCu, 0x05u, 0x00u, 0x00u,
                                                      0x64u, 0x00u, 0x01u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                42u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);

    stub.nowMs = 25u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 99u);

    stub.nowMs = 75u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 141u);
}

static void test_runtime_touch_active_low_press_release(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;

    Test_InitRuntime(&runtime, &stub);
    stub.nowMs = 0u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    stub.touchRawState = 0u;
    stub.nowMs = 10u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    stub.nowMs = 39u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    stub.nowMs = 40u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[0], txCapture.length[0], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[1], COPROC_TOUCH_EVENT_PRESS);
    ASSERT_EQ_U32((uint32_t)decoded.payload[2] | ((uint32_t)decoded.payload[3] << 8u) |
                      ((uint32_t)decoded.payload[4] << 16u) | ((uint32_t)decoded.payload[5] << 24u),
                  40u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->touchEventTxCount, 1u);

    txCapture.count = 0u;
    stub.touchRawState = 1u;
    stub.nowMs = 50u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    stub.nowMs = 79u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    stub.nowMs = 80u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[0], txCapture.length[0], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[1], COPROC_TOUCH_EVENT_RELEASE);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->touchEventTxCount, 2u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_TOUCH_EVENT), 2u);
    ASSERT_EQ_U32(obsCapture.events[0].touchCode, COPROC_TOUCH_EVENT_PRESS);
    ASSERT_EQ_U32(obsCapture.events[1].touchCode, COPROC_TOUCH_EVENT_RELEASE);
}

static void test_runtime_periodic_servo_feedback_state(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;

    Test_InitRuntime(&runtime, &stub);
    runtime.config.readServoFeedbackFn = Test_RuntimeReadServoFeedback;
    runtime.config.readServoFeedbackCtx = &stub;
    runtime.config.servoFeedbackStateEnabled = 1u;
    runtime.config.servoFeedbackStateIntervalMs = 50u;
    stub.nowMs = 50u;

    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_STATE),
                  1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 2u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[0], txCapture.length[0], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(payloadLength, COPROC_PAYLOAD_LEN_MOTION_STATE);
    ASSERT_EQ_U32(decoded.payload[0], 50u);
    ASSERT_EQ_U32(decoded.payload[4], 0x03u);
    ASSERT_EQ_U32(decoded.payload[5], 0x84u);
    ASSERT_EQ_U32(decoded.payload[6], 0x03u);
    ASSERT_EQ_U32(decoded.payload[7], 0x84u);
    ASSERT_EQ_U32(decoded.payload[8], 0x03u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_MOTION_STATE), 1u);
}

static void test_runtime_servo_feedback_state_defaults_off(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    uint32_t nextSeq = 1u;

    Test_InitRuntime(&runtime, &stub);
    stub.nowMs = 50u;

    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_STATE),
                  0u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_servo_pwm_unlock_enables_feedback_state(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_PWM] = {0x03u, 0x03u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_PWM_UNLOCK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                302u,
                                COPROC_PAYLOAD_LEN_SERVO_PWM);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.servoReleaseCount, 2u);

    txCapture.count = 0u;
    obsCapture.count = 0u;
    stub.nowMs = 1u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_STATE),
                  1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 2u);
}

static void test_runtime_servo_pwm_unlock_reports_only_unlocked_axis(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_PWM] = {0x01u, 0x03u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_PWM_UNLOCK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                305u,
                                COPROC_PAYLOAD_LEN_SERVO_PWM);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    txCapture.count = 0u;
    obsCapture.count = 0u;
    stub.nowMs = 1u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_STATE),
                  1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[0], txCapture.length[0], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[4], 0x01u);
}

static void test_runtime_servo_pwm_lock_disables_feedback_state(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_PWM] = {0x03u, 0x03u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_PWM_UNLOCK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                303u,
                                COPROC_PAYLOAD_LEN_SERVO_PWM);
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_PWM_LOCK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                304u,
                                COPROC_PAYLOAD_LEN_SERVO_PWM);
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    txCapture.count = 0u;
    obsCapture.count = 0u;
    stub.feedbackReadCount = 0u;
    stub.nowMs = 100u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_STATE),
                  0u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_servo_jog_updates_until_stop(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t jogPayload[COPROC_PAYLOAD_LEN_SERVO_JOG] = {
        0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0xFAu, 0x00u, 0x02u, 0u, 180u, 90u, 150u
    };
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x02u};

    Test_InitRuntime(&runtime, &stub);
    runtime.config.servoFeedbackStateEnabled = 1u;
    runtime.config.servoFeedbackStateIntervalMs = 1000u;
    runtime.lastServoFeedbackStateTxMs = 1u;
    stub.feedbackAngle[2] = 30u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_JOG,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                101u,
                                COPROC_PAYLOAD_LEN_SERVO_JOG);
    memcpy(frame.payload, jogPayload, sizeof(jogPayload));

    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.servoSetCount, 0u);

    stub.nowMs = 100u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 96u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                102u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[2], txCapture.length[2], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 101u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_STOPPED);

    stub.nowMs = 200u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_servo_jog_refresh_does_not_interrupt_previous_jog(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t jogPayload[COPROC_PAYLOAD_LEN_SERVO_JOG] = {
        0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0xFAu, 0x00u, 0x02u, 0u, 180u, 90u, 150u
    };
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x02u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_JOG,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                101u,
                                COPROC_PAYLOAD_LEN_SERVO_JOG);
    memcpy(frame.payload, jogPayload, sizeof(jogPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    stub.nowMs = 60u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_JOG,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                102u,
                                COPROC_PAYLOAD_LEN_SERVO_JOG);
    memcpy(frame.payload, jogPayload, sizeof(jogPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                103u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 3u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[3], txCapture.length[3], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 102u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_STOPPED);
}

static void test_runtime_relay_target_overwrites_without_motion_done(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t relayPayload[COPROC_PAYLOAD_LEN_SERVO_RELAY_TARGET] = {
        0x03u, 0xB0u, 0x04u, 0x4Cu, 0x04u, 0x2Du, 0x00u, 0x2Cu, 0x01u, 0x05u
    };

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_RELAY_TARGET,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                201u,
                                COPROC_PAYLOAD_LEN_SERVO_RELAY_TARGET);
    memcpy(frame.payload, relayPayload, sizeof(relayPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);

    relayPayload[1] = 0x14u;
    relayPayload[2] = 0x05u;
    stub.nowMs = 10u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_RELAY_TARGET,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                202u,
                                COPROC_PAYLOAD_LEN_SERVO_RELAY_TARGET);
    memcpy(frame.payload, relayPayload, sizeof(relayPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);

    stub.nowMs = 30u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_TRUE(stub.servoSetCount > 0u);
    ASSERT_TRUE(stub.commandAngle[2] > 90u);
    ASSERT_TRUE(stub.commandAngle[2] < 130u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);

    stub.nowMs = 360u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_relay_target_stop_holds_without_motion_done(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t relayPayload[COPROC_PAYLOAD_LEN_SERVO_RELAY_TARGET] = {
        0x01u, 0xB0u, 0x04u, 0x00u, 0x00u, 0x2Du, 0x00u, 0x2Cu, 0x01u, 0x05u
    };
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x05u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_RELAY_TARGET,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                211u,
                                COPROC_PAYLOAD_LEN_SERVO_RELAY_TARGET);
    memcpy(frame.payload, relayPayload, sizeof(relayPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                212u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_direct_target_uses_deg_x10_callback(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_DIRECT_TARGET] = {0x01u, 0xEDu, 0x03u, 0x00u, 0x00u, 0x00u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_DIRECT_TARGET,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                93u,
                                COPROC_PAYLOAD_LEN_SERVO_DIRECT_TARGET);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoDegX10, 1005u);
}

static void test_runtime_servo_pwm_unlock_releases_axes(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_PWM] = {0x03u, 0x03u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_PWM_UNLOCK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                301u,
                                COPROC_PAYLOAD_LEN_SERVO_PWM);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.servoReleaseCount, 2u);
}

static void test_runtime_chunked_sequence_runs_segments(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t beginPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN] = {0x34u, 0x12u, 0x03u, 0x02u};
    uint8_t chunkPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_CHUNK_HEADER +
                         (2u * COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_SEGMENT)] = {
        0x34u, 0x12u, 0x00u, 0x02u,
        0x01u, 0xE8u, 0x03u, 0x00u, 0x00u, 0x32u, 0x00u, COPROC_MOTION_PROFILE_LINEAR,
        0x02u, 0x00u, 0x00u, 0xB0u, 0x04u, 0x32u, 0x00u, COPROC_MOTION_PROFILE_LINEAR
    };
    uint8_t endPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END] = {0x34u, 0x12u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_BEGIN,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                311u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN);
    memcpy(frame.payload, beginPayload, sizeof(beginPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_CHUNK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                312u,
                                sizeof(chunkPayload));
    memcpy(frame.payload, chunkPayload, sizeof(chunkPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_END,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                313u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END);
    memcpy(frame.payload, endPayload, sizeof(endPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 3u);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 90u);

    stub.nowMs = 60u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 3u);
    ASSERT_EQ_U32(stub.lastServoIndex, 1u);
    ASSERT_EQ_U32(stub.lastServoAngle, 96u);

    stub.nowMs = 120u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 4u);
    ASSERT_EQ_U32(stub.lastServoIndex, 1u);
    ASSERT_EQ_U32(stub.lastServoAngle, 120u);
}

static void test_runtime_chunked_sequence_catches_up_late_boundary(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t beginPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN] = {0x35u, 0x12u, 0x03u, 0x02u};
    uint8_t chunkPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_CHUNK_HEADER +
                         (2u * COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_SEGMENT)] = {
        0x35u, 0x12u, 0x00u, 0x02u,
        0x01u, 0x14u, 0x05u, 0x00u, 0x00u, 0xE8u, 0x03u, COPROC_MOTION_PROFILE_LINEAR,
        0x01u, 0x2Cu, 0x01u, 0x00u, 0x00u, 0xE8u, 0x03u, COPROC_MOTION_PROFILE_LINEAR
    };
    uint8_t endPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END] = {0x35u, 0x12u};

    Test_InitRuntime(&runtime, &stub);
    stub.feedbackAngle[2] = 30u;

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_BEGIN,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                321u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN);
    memcpy(frame.payload, beginPayload, sizeof(beginPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_CHUNK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                322u,
                                sizeof(chunkPayload));
    memcpy(frame.payload, chunkPayload, sizeof(chunkPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_END,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                323u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END);
    memcpy(frame.payload, endPayload, sizeof(endPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 90u);

    stub.feedbackAngle[2] = 30u;
    stub.nowMs = 1031u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 127u);
}

static void test_runtime_chunked_sequence_catches_up_multiple_boundaries(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t beginPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN] = {0x36u, 0x12u, 0x03u, 0x03u};
    uint8_t chunkPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_CHUNK_HEADER +
                         (3u * COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_SEGMENT)] = {
        0x36u, 0x12u, 0x00u, 0x03u,
        0x01u, 0x14u, 0x05u, 0x00u, 0x00u, 0x28u, 0x00u, COPROC_MOTION_PROFILE_LINEAR,
        0x01u, 0x2Cu, 0x01u, 0x00u, 0x00u, 0x28u, 0x00u, COPROC_MOTION_PROFILE_LINEAR,
        0x01u, 0x84u, 0x03u, 0x00u, 0x00u, 0x28u, 0x00u, COPROC_MOTION_PROFILE_LINEAR
    };
    uint8_t endPayload[COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END] = {0x36u, 0x12u};

    Test_InitRuntime(&runtime, &stub);
    stub.feedbackAngle[2] = 30u;

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_BEGIN,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                331u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_BEGIN);
    memcpy(frame.payload, beginPayload, sizeof(beginPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_CHUNK,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                332u,
                                sizeof(chunkPayload));
    memcpy(frame.payload, chunkPayload, sizeof(chunkPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_SEQUENCE_END,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                333u,
                                COPROC_PAYLOAD_LEN_SERVO_SEQUENCE_END);
    memcpy(frame.payload, endPayload, sizeof(endPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    stub.nowMs = 95u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 2u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
    ASSERT_EQ_U32(stub.lastServoIndex, 2u);
    ASSERT_EQ_U32(stub.lastServoAngle, 53u);
}

static void test_runtime_servo_move_rejects_invalid_motion_payload(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                55u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, payload, sizeof(payload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);

    txCapture.count = 0u;
    payload[5] = 0x64u;
    payload[7] = 0x7Fu;
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);
}

static void test_runtime_motion_queue_runs_after_active_done(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t moveX[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0x64u, 0x00u, 0x00u, 0x01u};
    uint8_t moveY[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x02u, 0x00u, 0x00u, 0x4Cu, 0x04u, 0x64u, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    stub.feedbackAngle[1] = 150u;
    stub.feedbackAngle[2] = 150u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                71u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, moveX, sizeof(moveX));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                72u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, moveY, sizeof(moveY));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
    ASSERT_EQ_U32(stub.servoSetCount, 1u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);

    stub.nowMs = 100u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_TRUE(stub.servoSetCount >= 3u);

    stub.nowMs = 200u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 2u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[3], txCapture.length[3], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 72u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_SUCCESS);
    ASSERT_EQ_U32((uint16_t)decoded.payload[7] | ((uint16_t)decoded.payload[8] << 8u), 1100u);
    ASSERT_EQ_U32(stub.feedbackReadCount, 0u);
}

static void test_runtime_motion_queue_full_nacks_new_move(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x84u, 0x03u, 0x00u, 0x00u, 0x64u, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    for (uint32_t seq = 91u; seq <= 95u; ++seq) {
        payload[1] = (uint8_t)((900u + ((seq - 91u) * 10u)) & 0xFFu);
        payload[2] = (uint8_t)((900u + ((seq - 91u) * 10u)) >> 8u);
        CoprocFrameCodec_InitHeader(&frame.header,
                                    COPROC_MSG_CLASS_MOTION,
                                    COPROC_MOTION_MSG_SERVO_MOVE,
                                    COPROC_FRAME_FLAG_ACK_REQ,
                                    seq,
                                    COPROC_PAYLOAD_LEN_SERVO_MOVE);
        memcpy(frame.payload, payload, sizeof(payload));
        ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                    Test_AllocSeq,
                                                    &nextSeq,
                                                    Test_TxCapture,
                                                    &txCapture,
                                                    Test_RuntimeDispatchObs,
                                                    &obsCapture,
                                                    &runtime),
                         COPROC_STATUS_OK);
    }

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 4u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->eventDropCount, 1u);
}

static void test_runtime_servo_stop_all_pending_clears_queue(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t moveX[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0x64u, 0x00u, 0x00u, 0x01u};
    uint8_t moveY[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x02u, 0x00u, 0x00u, 0x4Cu, 0x04u, 0x64u, 0x00u, 0x00u, 0x01u};
    uint8_t stopAll[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                81u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, moveX, sizeof(moveX));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                82u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, moveY, sizeof(moveY));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                83u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopAll, sizeof(stopAll));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 3u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);

    stub.nowMs = 500u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
}

static void test_runtime_servo_stop_active_and_inactive(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t movePayload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x03u, 0x58u, 0x02u, 0x4Cu, 0x04u, 0x64u, 0x00u, 0x00u, 0x01u};
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                61u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, movePayload, sizeof(movePayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);

    stub.nowMs = 40u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                62u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoStopCount, 2u);
    ASSERT_EQ_U32(stub.servoSetCount, 4u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[2], txCapture.length[2], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 61u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_STOPPED);

    txCapture.count = 0u;
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoStopCount, 2u);
    ASSERT_EQ_U32(stub.servoSetCount, 4u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);
}

static void test_runtime_reset_recovery_restarts_motion_cleanly(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t movePayload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x58u, 0x02u, 0x00u, 0x00u, 0x64u, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                81u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, movePayload, sizeof(movePayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);

    CoprocRuntime_Reset(&runtime);
    txCapture.count = 0u;
    obsCapture.count = 0u;
    stub.nowMs = 200u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 0u);

    frame.header.seq = 82u;
    stub.nowMs = 210u;
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    stub.nowMs = 310u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[1], txCapture.length[1], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 82u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_SUCCESS);
}

static void test_issue22_servo_move_and_stop_acceptance(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint32_t nextSeq = 1u;
    uint8_t movePayload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x03u, 0x58u, 0x02u, 0x4Cu, 0x04u, 0x64u, 0x00u, 0x00u, 0x01u};
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x01u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                91u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, movePayload, sizeof(movePayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.servoSetCount, 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);

    stub.nowMs = 100u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[1], txCapture.length[1], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 91u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_SUCCESS);

    txCapture.count = 0u;
    stub.nowMs = 110u;
    frame.header.seq = 92u;
    memcpy(frame.payload, movePayload, sizeof(movePayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    stub.nowMs = 130u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                93u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_STATUS(Test_DecodeWireMessage(txCapture.message[2], txCapture.length[2], &decoded, &payloadLength),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(decoded.payload[0], 92u);
    ASSERT_EQ_U32(decoded.payload[4], COPROC_MOTION_RESULT_STOPPED);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_MOTION_DONE) >= 2u, 1u);
}

static void test_issue28_available_bench_matrix_without_sensors(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t movePayload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x01u, 0x84u, 0x03u, 0x00u, 0x00u, 0x1Eu, 0x00u, 0x00u, 0x01u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_SYS,
                                COPROC_SYS_MSG_HELLO_REQ,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                101u,
                                COPROC_PAYLOAD_LEN_HELLO_REQ);
    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_HELLO_RSP), 1u);

    txCapture.count = 0u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_SENSOR,
                                COPROC_SENSOR_MSG_TOUCH_EVENT,
                                0u,
                                102u,
                                0u);
    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);

    txCapture.count = 0u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                103u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    memcpy(frame.payload, movePayload, sizeof(movePayload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    stub.nowMs = 30u;
    ASSERT_EQ_STATUS(CoprocRuntime_Poll(&runtime, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture,
                                        Test_RuntimeDispatchObs, &obsCapture),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
}

static void test_runtime_power_5v_enable_disable(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t payload[1] = {4u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_POWER,
                                COPROC_POWER_MSG_5V_ENABLE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                61u,
                                COPROC_PAYLOAD_LEN_POWER_5V_ENABLE);
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.powerSetCount, 1u);
    ASSERT_EQ_U32(stub.lastPowerEnabled, 1u);
    ASSERT_EQ_U32(stub.lastPowerSourceTag, 4u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_POWER_5V_ENABLE), 1u);
    ASSERT_EQ_U32(obsCapture.events[1].powerEnabled, 1u);

    payload[0] = 7u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_POWER,
                                COPROC_POWER_MSG_5V_DISABLE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                62u,
                                COPROC_PAYLOAD_LEN_POWER_5V_DISABLE);
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocRuntime_ProcessFrame(&frame,
                                                Test_AllocSeq,
                                                &nextSeq,
                                                Test_TxCapture,
                                                &txCapture,
                                                Test_RuntimeDispatchObs,
                                                &obsCapture,
                                                &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(stub.powerSetCount, 2u);
    ASSERT_EQ_U32(stub.lastPowerEnabled, 0u);
    ASSERT_EQ_U32(stub.lastPowerSourceTag, 7u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_POWER_5V_DISABLE), 1u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->powerCommandRxCount, 2u);
}

static void test_runtime_led_and_sensor_disabled_nack(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t ledPayload[COPROC_PAYLOAD_LEN_LED_SET_RGB] = {10u, 20u, 30u, 4u, 0u};

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_SET_RGB,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                55u,
                                COPROC_PAYLOAD_LEN_LED_SET_RGB);
    memcpy(frame.payload, ledPayload, sizeof(ledPayload));
    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);

    txCapture.count = 0u;
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_SENSOR,
                                COPROC_SENSOR_MSG_TOUCH_EVENT,
                                0u,
                                56u,
                                0u);
    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);
    ASSERT_EQ_U32(stub.ledRgbCount, 0u);
}

static void test_runtime_led_set_rgb_updates_side_and_bottom(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t ledPayload[COPROC_PAYLOAD_LEN_LED_SET_RGB] = {0u, 255u, 0u, 4u, 0u};

    Test_InitRuntimeWithLed(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_SET_RGB,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                88u,
                                COPROC_PAYLOAD_LEN_LED_SET_RGB);
    memcpy(frame.payload, ledPayload, sizeof(ledPayload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_LED, COPROC_LED_MSG_LED_DONE), 1u);
    ASSERT_EQ_U32(stub.ledCountSetCount, 1u);
    ASSERT_EQ_U32(stub.lastLedCount, 4u);
    ASSERT_EQ_U32(stub.ledRgbCount, 1u);
    ASSERT_EQ_U32(stub.lastRed, 0u);
    ASSERT_EQ_U32(stub.lastGreen, 255u);
    ASSERT_EQ_U32(stub.lastBlue, 0u);
    ASSERT_EQ_U32(stub.bottomLedCountSetCount, 1u);
    ASSERT_EQ_U32(stub.lastBottomLedCount, 40u);
    ASSERT_EQ_U32(stub.bottomLedRgbCount, 1u);
    ASSERT_EQ_U32(stub.lastBottomRed, 0u);
    ASSERT_EQ_U32(stub.lastBottomGreen, 255u);
    ASSERT_EQ_U32(stub.lastBottomBlue, 0u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->ledCommandRxCount, 1u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->ledDoneTxCount, 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_SET_RGB), 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_DONE), 1u);
}

static void test_runtime_led_set_rgb_targets_side_only(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t ledPayload[COPROC_PAYLOAD_LEN_LED_SET_RGB] = {12u, 34u, 56u, 4u, 1u};

    Test_InitRuntimeWithLed(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_SET_RGB,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                90u,
                                COPROC_PAYLOAD_LEN_LED_SET_RGB);
    memcpy(frame.payload, ledPayload, sizeof(ledPayload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.ledCountSetCount, 1u);
    ASSERT_EQ_U32(stub.lastLedCount, 4u);
    ASSERT_EQ_U32(stub.ledRgbCount, 1u);
    ASSERT_EQ_U32(stub.lastRed, 12u);
    ASSERT_EQ_U32(stub.lastGreen, 34u);
    ASSERT_EQ_U32(stub.lastBlue, 56u);
    ASSERT_EQ_U32(stub.bottomLedCountSetCount, 0u);
    ASSERT_EQ_U32(stub.bottomLedRgbCount, 0u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_SET_RGB), 1u);
}

static void test_runtime_led_set_rgb_targets_bottom_only(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t ledPayload[COPROC_PAYLOAD_LEN_LED_SET_RGB] = {78u, 90u, 123u, 4u, 2u};

    Test_InitRuntimeWithLed(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_SET_RGB,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                91u,
                                COPROC_PAYLOAD_LEN_LED_SET_RGB);
    memcpy(frame.payload, ledPayload, sizeof(ledPayload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(stub.ledCountSetCount, 0u);
    ASSERT_EQ_U32(stub.ledRgbCount, 0u);
    ASSERT_EQ_U32(stub.bottomLedCountSetCount, 1u);
    ASSERT_EQ_U32(stub.lastBottomLedCount, 40u);
    ASSERT_EQ_U32(stub.bottomLedRgbCount, 1u);
    ASSERT_EQ_U32(stub.lastBottomRed, 78u);
    ASSERT_EQ_U32(stub.lastBottomGreen, 90u);
    ASSERT_EQ_U32(stub.lastBottomBlue, 123u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_SET_RGB), 1u);
}

static void test_runtime_led_breathe_updates_side_and_bottom(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;
    uint8_t ledPayload[COPROC_PAYLOAD_LEN_LED_BREATHE] = {0u, 255u, 0u, 8u, 22u, 0u};

    Test_InitRuntimeWithLed(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                COPROC_LED_MSG_BREATHE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                89u,
                                COPROC_PAYLOAD_LEN_LED_BREATHE);
    memcpy(frame.payload, ledPayload, sizeof(ledPayload));

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);

    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_LED, COPROC_LED_MSG_LED_DONE), 1u);
    ASSERT_EQ_U32(stub.ledBreatheCount, 1u);
    ASSERT_EQ_U32(stub.bottomLedBreatheCount, 1u);
    ASSERT_EQ_U32(stub.lastRed, 0u);
    ASSERT_EQ_U32(stub.lastGreen, 255u);
    ASSERT_EQ_U32(stub.lastBlue, 0u);
    ASSERT_EQ_U32(stub.lastBottomRed, 0u);
    ASSERT_EQ_U32(stub.lastBottomGreen, 255u);
    ASSERT_EQ_U32(stub.lastBottomBlue, 0u);
    ASSERT_EQ_U32(stub.lastStep, 8u);
    ASSERT_EQ_U32(stub.lastIntervalMs, 22u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->ledCommandRxCount, 1u);
    ASSERT_EQ_U32(CoprocRuntime_GetStats(&runtime)->ledDoneTxCount, 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_BREATHE), 1u);
    ASSERT_EQ_U32(Test_CountObsType(&obsCapture, COPROC_OBS_EVENT_LED_DONE), 1u);
}

static void test_runtime_unsupported_led_message_nack(void)
{
    RuntimeStub stub = {0};
    CoprocRuntime runtime;
    TxCapture txCapture = {0};
    ObsCapture obsCapture = {0};
    CoprocFrame frame;
    uint32_t nextSeq = 1u;

    Test_InitRuntime(&runtime, &stub);
    CoprocFrameCodec_InitHeader(&frame.header,
                                COPROC_MSG_CLASS_LED,
                                0x7Fu,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                77u,
                                0u);

    ASSERT_EQ_STATUS(CoprocDispatch_ProcessFrame(&frame,
                                                 Test_AllocSeq,
                                                 &nextSeq,
                                                 Test_TxCapture,
                                                 &txCapture,
                                                 Test_RuntimeDispatchObs,
                                                 &obsCapture,
                                                 CoprocRuntime_ProcessFrame,
                                                 &runtime),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);
}

static void test_stress_sim_move_done_and_stop(void)
{
    uint32_t nowMs = 0u;
    CoprocStressSim sim;
    TxCapture txCapture = {0};
    CoprocFrameHeader header;
    CoprocFrame frame;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x03u, 0x58u, 0x02u, 0x4Cu, 0x04u, 0xB4u, 0x00u, 0x00u, 0x01u};
    uint8_t stopPayload[COPROC_PAYLOAD_LEN_SERVO_STOP] = {0x00u, 0x01u};
    uint32_t nextSeq = 1u;

    CoprocStressSim_Init(&sim, Test_NowMs, &nowMs);

    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                41u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    frame.header = header;
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocStressSim_ProcessFrame(&frame, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL, &sim),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 1u);

    nowMs = 180u;
    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                42u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    frame.header = header;
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocStressSim_ProcessFrame(&frame, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL, &sim),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 2u);

    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                43u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    frame.header = header;
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocStressSim_ProcessFrame(&frame, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL, &sim),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_ACK), 3u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_MOTION, COPROC_MOTION_MSG_MOTION_DONE), 2u);

    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_STOP,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                44u,
                                COPROC_PAYLOAD_LEN_SERVO_STOP);
    frame.header = header;
    memcpy(frame.payload, stopPayload, sizeof(stopPayload));
    ASSERT_EQ_STATUS(CoprocStressSim_ProcessFrame(&frame, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL, &sim),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SYS, COPROC_SYS_MSG_NACK), 1u);
    ASSERT_EQ_U32(CoprocStressSim_GetStats(&sim)->servoMoveRxCount, 2u);
    ASSERT_EQ_U32(CoprocStressSim_GetStats(&sim)->servoStopRxCount, 2u);
}

static void test_stress_sim_sensor_rates(void)
{
    uint32_t nowMs = 0u;
    CoprocStressSim sim;
    TxCapture txCapture = {0};
    CoprocFrameHeader header;
    CoprocFrame frame;
    CoprocFrame decoded;
    size_t payloadLength = 0u;
    uint8_t touchCodes[2] = {0};
    size_t touchCodeCount = 0u;
    uint8_t payload[COPROC_PAYLOAD_LEN_SERVO_MOVE] = {0x03u, 0x58u, 0x02u, 0x4Cu, 0x04u, 0xB4u, 0x00u, 0x00u, 0x01u};
    uint32_t nextSeq = 1u;

    CoprocStressSim_Init(&sim, Test_NowMs, &nowMs);

    nowMs = 1050u;
    ASSERT_EQ_STATUS(CoprocStressSim_Poll(&sim, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_IMU_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_MAG_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    CoprocFrameCodec_InitHeader(&header,
                                COPROC_MSG_CLASS_MOTION,
                                COPROC_MOTION_MSG_SERVO_MOVE,
                                COPROC_FRAME_FLAG_ACK_REQ,
                                41u,
                                COPROC_PAYLOAD_LEN_SERVO_MOVE);
    frame.header = header;
    memcpy(frame.payload, payload, sizeof(payload));
    ASSERT_EQ_STATUS(CoprocStressSim_ProcessFrame(&frame, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL, &sim),
                     COPROC_STATUS_OK);

    ASSERT_EQ_STATUS(CoprocStressSim_Poll(&sim, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_IMU_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_MAG_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    nowMs = 1100u;
    ASSERT_EQ_STATUS(CoprocStressSim_Poll(&sim, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_IMU_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_MAG_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    nowMs = 1550u;
    ASSERT_EQ_STATUS(CoprocStressSim_Poll(&sim, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_IMU_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_MAG_STATE), 1u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 0u);

    nowMs = 2100u;
    ASSERT_EQ_STATUS(CoprocStressSim_Poll(&sim, Test_AllocSeq, &nextSeq, Test_TxCapture, &txCapture, NULL, NULL),
                     COPROC_STATUS_OK);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_IMU_STATE), 0u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_MAG_STATE), 2u);
    ASSERT_EQ_U32(Test_CountDecodedFramesByType(&txCapture, COPROC_MSG_CLASS_SENSOR, COPROC_SENSOR_MSG_TOUCH_EVENT), 2u);
    ASSERT_EQ_U32(CoprocStressSim_GetStats(&sim)->magTxCount, 2u);
    ASSERT_EQ_U32(CoprocStressSim_GetStats(&sim)->touchTxCount, 2u);

    for (size_t index = 0u; index < txCapture.count; ++index) {
        if (Test_DecodeWireMessage(txCapture.message[index], txCapture.length[index], &decoded, &payloadLength) !=
            COPROC_STATUS_OK) {
            continue;
        }
        if (decoded.header.msgClass == COPROC_MSG_CLASS_SENSOR &&
            decoded.header.msgId == COPROC_SENSOR_MSG_TOUCH_EVENT &&
            touchCodeCount < (sizeof(touchCodes) / sizeof(touchCodes[0]))) {
            touchCodes[touchCodeCount++] = decoded.payload[1];
        }
    }
    ASSERT_EQ_U32(touchCodeCount, 2u);
    ASSERT_EQ_U32(touchCodes[0], COPROC_TOUCH_EVENT_PRESS);
    ASSERT_EQ_U32(touchCodes[1], COPROC_TOUCH_EVENT_RELEASE);
}

static void run_test(void (*fn)(void), const char *name)
{
    int failuresBefore = s_testFailures;

    fn();
    if (failuresBefore == s_testFailures) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
    }
}

int main(void)
{
    run_test(test_ring_buffer_write_read_reset, "ring_buffer_write_read_reset");
    run_test(test_ring_buffer_overflow, "ring_buffer_overflow");
    run_test(test_framing_delimiter_and_truncate, "framing_delimiter_and_truncate");
    run_test(test_cobs_round_trip_and_invalid, "cobs_round_trip_and_invalid");
    run_test(test_crc_vector, "crc_vector");
    run_test(test_frame_pack_unpack_and_crc_fail, "frame_pack_unpack_and_crc_fail");
    run_test(test_tx_builder_vectors, "tx_builder_vectors");
    run_test(test_protocol_hello_req_ack_hello_rsp, "protocol_hello_req_ack_hello_rsp");
    run_test(test_protocol_decode_fail_stats, "protocol_decode_fail_stats");
    run_test(test_protocol_dispatch_fail_on_tx_failure, "protocol_dispatch_fail_on_tx_failure");
    run_test(test_protocol_strict_sys_payload_lengths, "protocol_strict_sys_payload_lengths");
    run_test(test_protocol_accepts_extended_hello_rsp_metadata, "protocol_accepts_extended_hello_rsp_metadata");
    run_test(test_runtime_servo_move_delayed_motion_done, "runtime_servo_move_delayed_motion_done");
    run_test(test_runtime_servo_move_ease_in_out_profile, "runtime_servo_move_ease_in_out_profile");
    run_test(test_runtime_touch_active_low_press_release, "runtime_touch_active_low_press_release");
    run_test(test_runtime_servo_feedback_state_defaults_off, "runtime_servo_feedback_state_defaults_off");
    run_test(test_runtime_periodic_servo_feedback_state, "runtime_periodic_servo_feedback_state");
    run_test(test_runtime_servo_pwm_unlock_enables_feedback_state, "runtime_servo_pwm_unlock_enables_feedback_state");
    run_test(test_runtime_servo_pwm_unlock_reports_only_unlocked_axis, "runtime_servo_pwm_unlock_reports_only_unlocked_axis");
    run_test(test_runtime_servo_pwm_lock_disables_feedback_state, "runtime_servo_pwm_lock_disables_feedback_state");
    run_test(test_runtime_servo_jog_updates_until_stop, "runtime_servo_jog_updates_until_stop");
    run_test(test_runtime_servo_jog_refresh_does_not_interrupt_previous_jog, "runtime_servo_jog_refresh_does_not_interrupt_previous_jog");
    run_test(test_runtime_relay_target_overwrites_without_motion_done, "runtime_relay_target_overwrites_without_motion_done");
    run_test(test_runtime_relay_target_stop_holds_without_motion_done, "runtime_relay_target_stop_holds_without_motion_done");
    run_test(test_runtime_direct_target_uses_deg_x10_callback, "runtime_direct_target_uses_deg_x10_callback");
    run_test(test_runtime_servo_pwm_unlock_releases_axes, "runtime_servo_pwm_unlock_releases_axes");
    run_test(test_runtime_chunked_sequence_runs_segments, "runtime_chunked_sequence_runs_segments");
    run_test(test_runtime_chunked_sequence_catches_up_late_boundary,
             "runtime_chunked_sequence_catches_up_late_boundary");
    run_test(test_runtime_chunked_sequence_catches_up_multiple_boundaries,
             "runtime_chunked_sequence_catches_up_multiple_boundaries");
    run_test(test_runtime_servo_move_rejects_invalid_motion_payload, "runtime_servo_move_rejects_invalid_motion_payload");
    run_test(test_runtime_motion_queue_runs_after_active_done, "runtime_motion_queue_runs_after_active_done");
    run_test(test_runtime_motion_queue_full_nacks_new_move, "runtime_motion_queue_full_nacks_new_move");
    run_test(test_runtime_servo_stop_all_pending_clears_queue, "runtime_servo_stop_all_pending_clears_queue");
    run_test(test_runtime_servo_stop_active_and_inactive, "runtime_servo_stop_active_and_inactive");
    run_test(test_runtime_reset_recovery_restarts_motion_cleanly, "runtime_reset_recovery_restarts_motion_cleanly");
    run_test(test_issue22_servo_move_and_stop_acceptance, "issue22_servo_move_and_stop_acceptance");
    run_test(test_issue28_available_bench_matrix_without_sensors, "issue28_available_bench_matrix_without_sensors");
    run_test(test_runtime_power_5v_enable_disable, "runtime_power_5v_enable_disable");
    run_test(test_runtime_led_and_sensor_disabled_nack, "runtime_led_and_sensor_disabled_nack");
    run_test(test_runtime_led_set_rgb_updates_side_and_bottom, "runtime_led_set_rgb_updates_side_and_bottom");
    run_test(test_runtime_led_set_rgb_targets_side_only, "runtime_led_set_rgb_targets_side_only");
    run_test(test_runtime_led_set_rgb_targets_bottom_only, "runtime_led_set_rgb_targets_bottom_only");
    run_test(test_runtime_led_breathe_updates_side_and_bottom, "runtime_led_breathe_updates_side_and_bottom");
    run_test(test_runtime_unsupported_led_message_nack, "runtime_unsupported_led_message_nack");
    run_test(test_stress_sim_move_done_and_stop, "stress_sim_move_done_and_stop");
    run_test(test_stress_sim_sensor_rates, "stress_sim_sensor_rates");

    if (s_testFailures != 0) {
        fprintf(stderr, "coproc_host_tests: %d failure(s)\n", s_testFailures);
        return EXIT_FAILURE;
    }

    printf("coproc_host_tests: all checks passed\n");
    return EXIT_SUCCESS;
}
