#include "coproc_protocol.h"

#include "coproc_cobs.h"

#include <string.h>

typedef struct
{
    CoprocProtocolObsFn obsFn;
    void *obsCtx;
} CoprocProtocolDispatchObsCtx;

static void CoprocProtocol_EmitObs(CoprocProtocolObsFn obsFn,
                                   void *obsCtx,
                                   CoprocProtocolObsEventType type,
                                   const CoprocFrameHeader *header,
                                   size_t candidateLength,
                                   CoprocStatus status,
                                   uint32_t refSeq,
                                   uint16_t reasonCode,
                                   uint8_t faultSource,
                                   uint16_t faultCode)
{
    CoprocProtocolObsEvent event;

    if (obsFn == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = type;
    if (header != NULL) {
        event.header = *header;
    }
    event.candidateLength = candidateLength;
    event.status = status;
    event.refSeq = refSeq;
    event.reasonCode = reasonCode;
    event.faultSource = faultSource;
    event.faultCode = faultCode;
    obsFn(obsCtx, &event);
}

uint32_t CoprocProtocol_AllocNextTxSeq(CoprocProtocol *protocol)
{
    uint32_t seq;

    if (protocol == NULL) {
        return 0u;
    }

    seq = protocol->nextTxSeq;
    if (seq == 0u) {
        seq = 1u;
    }
    protocol->nextTxSeq = seq + 1u;
    return seq;
}

static uint32_t CoprocProtocol_AllocTxSeq(void *ctx)
{
    CoprocProtocol *protocol = (CoprocProtocol *)ctx;

    return CoprocProtocol_AllocNextTxSeq(protocol);
}

static void CoprocProtocol_OnDispatchEvent(void *ctx, const CoprocDispatchEvent *event)
{
    CoprocProtocolDispatchObsCtx *obsCtx = (CoprocProtocolDispatchObsCtx *)ctx;
    CoprocProtocolObsEventType eventType = COPROC_OBS_EVENT_NONE;

    if (obsCtx == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
        case COPROC_DISPATCH_EVENT_HELLO_REQ:
            eventType = COPROC_OBS_EVENT_HELLO_REQ;
            break;
        case COPROC_DISPATCH_EVENT_ACK:
            eventType = COPROC_OBS_EVENT_ACK;
            break;
        case COPROC_DISPATCH_EVENT_NACK:
            eventType = COPROC_OBS_EVENT_NACK;
            break;
        case COPROC_DISPATCH_EVENT_FAULT:
            eventType = COPROC_OBS_EVENT_FAULT;
            break;
        case COPROC_DISPATCH_EVENT_HELLO_RSP:
            eventType = COPROC_OBS_EVENT_HELLO_RSP;
            break;
        case COPROC_DISPATCH_EVENT_SERVO_MOVE:
            eventType = COPROC_OBS_EVENT_SERVO_MOVE;
            break;
        case COPROC_DISPATCH_EVENT_SERVO_STOP:
            eventType = COPROC_OBS_EVENT_SERVO_STOP;
            break;
        case COPROC_DISPATCH_EVENT_MOTION_DONE:
            eventType = COPROC_OBS_EVENT_MOTION_DONE;
            break;
        case COPROC_DISPATCH_EVENT_LED_SET_RGB:
            eventType = COPROC_OBS_EVENT_LED_SET_RGB;
            break;
        case COPROC_DISPATCH_EVENT_LED_BREATHE:
            eventType = COPROC_OBS_EVENT_LED_BREATHE;
            break;
        case COPROC_DISPATCH_EVENT_LED_OFF:
            eventType = COPROC_OBS_EVENT_LED_OFF;
            break;
        case COPROC_DISPATCH_EVENT_LED_DONE:
            eventType = COPROC_OBS_EVENT_LED_DONE;
            break;
        case COPROC_DISPATCH_EVENT_TOUCH_EVENT:
            eventType = COPROC_OBS_EVENT_TOUCH_EVENT;
            break;
        case COPROC_DISPATCH_EVENT_MAG_STATE:
            eventType = COPROC_OBS_EVENT_MAG_STATE;
            break;
        case COPROC_DISPATCH_EVENT_IMU_STATE:
            eventType = COPROC_OBS_EVENT_IMU_STATE;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_ENABLE:
            eventType = COPROC_OBS_EVENT_POWER_5V_ENABLE;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_DISABLE:
            eventType = COPROC_OBS_EVENT_POWER_5V_DISABLE;
            break;
        default:
            break;
    }

    if (eventType == COPROC_OBS_EVENT_NONE) {
        return;
    }

    if (obsCtx->obsFn != NULL) {
        CoprocProtocolObsEvent obsEvent = {0};

        obsEvent.type = eventType;
        obsEvent.header = event->header;
        obsEvent.status = COPROC_STATUS_OK;
        obsEvent.refSeq = event->refSeq;
        obsEvent.reasonCode = event->reasonCode;
        obsEvent.faultSource = event->faultSource;
        obsEvent.faultCode = event->faultCode;
        obsEvent.axisMask = event->axisMask;
        obsEvent.xValue = event->xValue;
        obsEvent.yValue = event->yValue;
        obsEvent.zValue = event->zValue;
        obsEvent.durationMs = event->durationMs;
        obsEvent.motionProfile = event->motionProfile;
        obsEvent.sourceTag = event->sourceTag;
        obsEvent.stopScope = event->stopScope;
        obsEvent.motionResult = event->motionResult;
        obsEvent.execTimeMs = event->execTimeMs;
        obsEvent.red = event->red;
        obsEvent.green = event->green;
        obsEvent.blue = event->blue;
        obsEvent.activeCount = event->activeCount;
        obsEvent.ledStep = event->ledStep;
        obsEvent.ledIntervalMs = event->ledIntervalMs;
        obsEvent.ledResult = event->ledResult;
        obsEvent.touchId = event->touchId;
        obsEvent.touchCode = event->touchCode;
        obsEvent.timestampMs = event->timestampMs;
        obsEvent.quality = event->quality;
        obsEvent.statusBits = event->statusBits;
        obsEvent.auxValue = event->auxValue;
        obsEvent.scalarValue = event->scalarValue;
        obsEvent.powerEnabled = event->powerEnabled;
        obsCtx->obsFn(obsCtx->obsCtx, &obsEvent);
    }
}

void CoprocProtocol_Init(CoprocProtocol *protocol)
{
    if (protocol == NULL) {
        return;
    }

    memset(protocol, 0, sizeof(*protocol));
    CoprocRingBuffer_Init(&protocol->ring, protocol->ringStorage, sizeof(protocol->ringStorage));
    CoprocFraming_Init(&protocol->framing);
    protocol->nextTxSeq = 1u;
}

void CoprocProtocol_Reset(CoprocProtocol *protocol)
{
    if (protocol == NULL) {
        return;
    }

    CoprocRingBuffer_Reset(&protocol->ring);
    CoprocFraming_Reset(&protocol->framing);
    memset(&protocol->stats, 0, sizeof(protocol->stats));
    protocol->nextTxSeq = 1u;
}

CoprocStatus CoprocProtocol_IngestBytes(CoprocProtocol *protocol, const uint8_t *data, size_t length)
{
    if (protocol == NULL || (length > 0u && data == NULL)) {
        return COPROC_STATUS_INVALID_ARG;
    }

    protocol->stats.rxBytesTotal += (uint32_t)length;
    if (CoprocRingBuffer_FreeSpace(&protocol->ring) < length) {
        if (protocol->framing.length > 0u || CoprocRingBuffer_Count(&protocol->ring) > 0u) {
            protocol->stats.frameDropCount++;
        }
        protocol->stats.ringOverflowCount++;
        CoprocRingBuffer_Reset(&protocol->ring);
        CoprocFraming_Reset(&protocol->framing);
        return COPROC_STATUS_NO_MEM;
    }

    return CoprocRingBuffer_Write(&protocol->ring, data, length);
}

CoprocStatus CoprocProtocol_Process(CoprocProtocol *protocol,
                                    CoprocProtocolTxWriteFn txWriteFn,
                                    void *txWriteCtx,
                                    CoprocProtocolObsFn obsFn,
                                    void *obsCtx)
{
    uint8_t byte = 0u;
    CoprocStatus status;
    uint8_t processedAny = 0u;

    if (protocol == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    while ((status = CoprocRingBuffer_ReadByte(&protocol->ring, &byte)) == COPROC_STATUS_OK) {
        size_t candidateLength = 0u;
        CoprocFramingResult framingResult = CoprocFraming_ConsumeByte(&protocol->framing, byte, &candidateLength);

        processedAny = 1u;
        if (framingResult == COPROC_FRAMING_RESULT_FRAME_DROPPED) {
            protocol->stats.frameDropCount++;
            continue;
        }

        if (framingResult == COPROC_FRAMING_RESULT_CANDIDATE_READY) {
            const uint8_t *candidate = CoprocFraming_GetCandidate(&protocol->framing);
            uint8_t raw[COPROC_FRAME_MAX_RAW_SIZE];
            size_t rawLength = 0u;
            CoprocFrame frame;
            size_t payloadLength = 0u;
            CoprocProtocolDispatchObsCtx dispatchObsCtx;
            CoprocStatus dispatchStatus;

            protocol->stats.frameCandidateCount++;
            CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_FRAME_CANDIDATE, NULL, candidateLength,
                                   COPROC_STATUS_OK, 0u, 0u, 0u, 0u);

            status = CoprocCobs_Decode(candidate, candidateLength, raw, sizeof(raw), &rawLength);
            if (status != COPROC_STATUS_OK) {
                protocol->stats.frameDecodeFail++;
                protocol->stats.cobsFail++;
                CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_COBS_FAIL, NULL, candidateLength,
                                       status, 0u, 0u, 0u, 0u);
                CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_FRAME_DECODE_FAIL, NULL, candidateLength,
                                       status, 0u, 0u, 0u, 0u);
                continue;
            }

            status = CoprocFrameCodec_Unpack(raw, rawLength, &frame, &payloadLength);
            if (status != COPROC_STATUS_OK) {
                protocol->stats.frameDecodeFail++;
                if (status == COPROC_STATUS_INVALID_CRC) {
                    protocol->stats.crcFail++;
                    CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_CRC_FAIL, NULL, candidateLength,
                                           status, 0u, 0u, 0u, 0u);
                }
                CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_FRAME_DECODE_FAIL, NULL, candidateLength,
                                       status, 0u, 0u, 0u, 0u);
                continue;
            }

            frame.header.payloadLength = (uint16_t)payloadLength;
            protocol->stats.frameDecodeOk++;
            CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_FRAME_DECODE_OK, &frame.header, candidateLength,
                                   COPROC_STATUS_OK, 0u, 0u, 0u, 0u);

            dispatchObsCtx.obsFn = obsFn;
            dispatchObsCtx.obsCtx = obsCtx;
            dispatchStatus = CoprocDispatch_ProcessFrame(&frame,
                                                         CoprocProtocol_AllocTxSeq,
                                                         protocol,
                                                         txWriteFn,
                                                         txWriteCtx,
                                                         CoprocProtocol_OnDispatchEvent,
                                                         &dispatchObsCtx,
                                                         protocol->dispatchExtensionFn,
                                                         protocol->dispatchExtensionCtx);
            if (dispatchStatus != COPROC_STATUS_OK) {
                protocol->stats.dispatchFailCount++;
                CoprocProtocol_EmitObs(obsFn, obsCtx, COPROC_OBS_EVENT_DISPATCH_FAIL, &frame.header, candidateLength,
                                       dispatchStatus, 0u, 0u, 0u, 0u);
                return dispatchStatus;
            }
        }
    }

    return (processedAny != 0u) ? COPROC_STATUS_OK : COPROC_STATUS_EMPTY;
}

const CoprocProtocolStats *CoprocProtocol_GetStats(const CoprocProtocol *protocol)
{
    return (protocol != NULL) ? &protocol->stats : NULL;
}

void CoprocProtocol_SetDispatchExtension(CoprocProtocol *protocol,
                                         CoprocDispatchExtensionFn extensionFn,
                                         void *extensionCtx)
{
    if (protocol == NULL) {
        return;
    }

    protocol->dispatchExtensionFn = extensionFn;
    protocol->dispatchExtensionCtx = extensionCtx;
}
