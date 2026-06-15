#include "coproc_stress_sim.h"

#include <string.h>

typedef struct
{
    CoprocDispatchEventFn fn;
    void *ctx;
} CoprocStressSimEventSink;

static CoprocStatus CoprocStressSim_FlushDueMotion(CoprocStressSim *sim,
                                                   uint32_t nowMs,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   const CoprocStressSimEventSink *eventSink);

static uint16_t CoprocStressSim_ReadU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8u));
}

static int16_t CoprocStressSim_ReadI16Le(const uint8_t *src)
{
    return (int16_t)CoprocStressSim_ReadU16Le(src);
}

static uint32_t CoprocStressSim_NowMs(CoprocStressSim *sim)
{
    if (sim == NULL || sim->nowFn == NULL) {
        return 0u;
    }

    return sim->nowFn(sim->nowCtx);
}

static void CoprocStressSim_EmitEvent(const CoprocStressSimEventSink *sink,
                                      CoprocDispatchEventType type,
                                      const CoprocFrameHeader *header,
                                      uint32_t refSeq,
                                      uint16_t reasonCode,
                                      uint8_t axisMask,
                                      int16_t xValue,
                                      int16_t yValue,
                                      int16_t zValue,
                                      uint16_t durationMs,
                                      uint8_t motionProfile,
                                      uint8_t sourceTag,
                                      uint8_t stopScope,
                                      uint8_t motionResult,
                                      uint16_t execTimeMs,
                                      uint8_t touchId,
                                      uint8_t touchCode,
                                      uint32_t timestampMs,
                                      uint8_t quality,
                                      uint8_t statusBits,
                                      uint16_t auxValue,
                                      uint16_t scalarValue)
{
    CoprocDispatchEvent event = {0};

    if (sink == NULL || sink->fn == NULL || header == NULL) {
        return;
    }

    event.type = type;
    event.header = *header;
    event.refSeq = refSeq;
    event.reasonCode = reasonCode;
    event.axisMask = axisMask;
    event.xValue = xValue;
    event.yValue = yValue;
    event.zValue = zValue;
    event.durationMs = durationMs;
    event.motionProfile = motionProfile;
    event.sourceTag = sourceTag;
    event.stopScope = stopScope;
    event.motionResult = motionResult;
    event.execTimeMs = execTimeMs;
    event.touchId = touchId;
    event.touchCode = touchCode;
    event.timestampMs = timestampMs;
    event.quality = quality;
    event.statusBits = statusBits;
    event.auxValue = auxValue;
    event.scalarValue = scalarValue;
    sink->fn(sink->ctx, &event);
}

static CoprocStatus CoprocStressSim_SendMessage(const CoprocTxMessage *message,
                                                CoprocDispatchTxWriteFn txWriteFn,
                                                void *txWriteCtx)
{
    if (message == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    return txWriteFn(txWriteCtx, message->wire, message->wireLength);
}

static CoprocStatus CoprocStressSim_SendNack(const CoprocFrame *frame,
                                             uint16_t reasonCode,
                                             CoprocDispatchAllocSeqFn allocSeqFn,
                                             void *allocSeqCtx,
                                             CoprocDispatchTxWriteFn txWriteFn,
                                             void *txWriteCtx,
                                             const CoprocStressSimEventSink *eventSink)
{
    CoprocTxMessage message;
    CoprocStatus status;
    uint32_t seq;

    if (frame == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildNack(seq, frame->header.seq, reasonCode, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocStressSim_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        CoprocStressSim_EmitEvent(eventSink,
                                  COPROC_DISPATCH_EVENT_NACK,
                                  &message.frame.header,
                                  frame->header.seq,
                                  reasonCode,
                                  0u,
                                  0,
                                  0,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u);
    }
    return status;
}

static CoprocStatus CoprocStressSim_SendAck(const CoprocFrame *frame,
                                            CoprocDispatchAllocSeqFn allocSeqFn,
                                            void *allocSeqCtx,
                                            CoprocDispatchTxWriteFn txWriteFn,
                                            void *txWriteCtx,
                                            const CoprocStressSimEventSink *eventSink)
{
    CoprocTxMessage message;
    CoprocStatus status;
    uint32_t seq;

    if (frame == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildAck(seq, frame->header.seq, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocStressSim_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        CoprocStressSim_EmitEvent(eventSink,
                                  COPROC_DISPATCH_EVENT_ACK,
                                  &message.frame.header,
                                  frame->header.seq,
                                  0u,
                                  0u,
                                  0,
                                  0,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u);
    }
    return status;
}

static CoprocStatus CoprocStressSim_SendMotionDone(CoprocStressSim *sim,
                                                   uint8_t resultCode,
                                                   uint32_t refSeq,
                                                   int16_t finalXDegX10,
                                                   int16_t finalYDegX10,
                                                   uint16_t execTimeMs,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   const CoprocStressSimEventSink *eventSink)
{
    CoprocTxMessage message;
    CoprocStatus status;
    uint32_t seq;

    if (sim == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildMotionDone(seq, refSeq, resultCode, finalXDegX10, finalYDegX10, execTimeMs, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocStressSim_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        sim->stats.motionDoneTxCount++;
        CoprocStressSim_EmitEvent(eventSink,
                                  COPROC_DISPATCH_EVENT_MOTION_DONE,
                                  &message.frame.header,
                                  refSeq,
                                  0u,
                                  0u,
                                  finalXDegX10,
                                  finalYDegX10,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  resultCode,
                                  execTimeMs,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u);
    }
    return status;
}

static CoprocStatus CoprocStressSim_SendTouchEvent(CoprocStressSim *sim,
                                                   uint8_t touchId,
                                                   uint8_t eventCode,
                                                   uint32_t timestampMs,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   const CoprocStressSimEventSink *eventSink)
{
    CoprocTxMessage message;
    CoprocStatus status;
    uint32_t seq;

    if (sim == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildTouchEvent(seq, touchId, eventCode, timestampMs, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocStressSim_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        sim->stats.touchTxCount++;
        CoprocStressSim_EmitEvent(eventSink,
                                  COPROC_DISPATCH_EVENT_TOUCH_EVENT,
                                  &message.frame.header,
                                  0u,
                                  0u,
                                  0u,
                                  0,
                                  0,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  touchId,
                                  eventCode,
                                  timestampMs,
                                  0u,
                                  0u,
                                  0u,
                                  0u);
    }
    return status;
}

static CoprocStatus CoprocStressSim_SendMagState(CoprocStressSim *sim,
                                                 uint16_t headingDegX100,
                                                 uint16_t fieldNormUt,
                                                 uint8_t quality,
                                                 uint8_t statusBits,
                                                 CoprocDispatchAllocSeqFn allocSeqFn,
                                                 void *allocSeqCtx,
                                                 CoprocDispatchTxWriteFn txWriteFn,
                                                 void *txWriteCtx,
                                                 const CoprocStressSimEventSink *eventSink)
{
    CoprocTxMessage message;
    CoprocStatus status;
    uint32_t seq;

    if (sim == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildMagState(seq, headingDegX100, fieldNormUt, quality, statusBits, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocStressSim_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        sim->stats.magTxCount++;
        CoprocStressSim_EmitEvent(eventSink,
                                  COPROC_DISPATCH_EVENT_MAG_STATE,
                                  &message.frame.header,
                                  0u,
                                  0u,
                                  0u,
                                  (int16_t)headingDegX100,
                                  0,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  0u,
                                  quality,
                                  statusBits,
                                  0u,
                                  fieldNormUt);
    }
    return status;
}

static CoprocStatus CoprocStressSim_FlushDueMotion(CoprocStressSim *sim,
                                                   uint32_t nowMs,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   const CoprocStressSimEventSink *eventSink)
{
    CoprocStatus status;

    if (sim == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (sim->motion.active == 0u || nowMs < sim->motion.completeAtMs) {
        return COPROC_STATUS_OK;
    }

    status = CoprocStressSim_SendMotionDone(sim,
                                            COPROC_MOTION_RESULT_SUCCESS,
                                            sim->motion.refSeq,
                                            sim->motion.xDegX10,
                                            sim->motion.yDegX10,
                                            sim->motion.durationMs,
                                            allocSeqFn,
                                            allocSeqCtx,
                                            txWriteFn,
                                            txWriteCtx,
                                            eventSink);
    if (status == COPROC_STATUS_OK) {
        sim->motion.active = 0u;
    }

    return status;
}

void CoprocStressSim_Init(CoprocStressSim *sim, CoprocStressSimNowFn nowFn, void *nowCtx)
{
    if (sim == NULL) {
        return;
    }

    memset(sim, 0, sizeof(*sim));
    sim->nowFn = nowFn;
    sim->nowCtx = nowCtx;
    CoprocStressSim_Reset(sim);
}

void CoprocStressSim_Reset(CoprocStressSim *sim)
{
    uint32_t nowMs;
    CoprocStressSimNowFn nowFn;
    void *nowCtx;

    if (sim == NULL) {
        return;
    }

    nowFn = sim->nowFn;
    nowCtx = sim->nowCtx;
    memset(sim, 0, sizeof(*sim));
    sim->nowFn = nowFn;
    sim->nowCtx = nowCtx;

    nowMs = CoprocStressSim_NowMs(sim);
    sim->nextMagAtMs = nowMs + 500u;
    sim->nextTouchPressAtMs = nowMs + 1000u;
    sim->nextTouchReleaseAtMs = 0u;
}

CoprocStatus CoprocStressSim_ProcessFrame(const CoprocFrame *frame,
                                          CoprocDispatchAllocSeqFn allocSeqFn,
                                          void *allocSeqCtx,
                                          CoprocDispatchTxWriteFn txWriteFn,
                                          void *txWriteCtx,
                                          CoprocDispatchEventFn eventFn,
                                          void *eventCtx,
                                          void *extensionCtx)
{
    CoprocStressSim *sim = (CoprocStressSim *)extensionCtx;
    CoprocStressSimEventSink eventSink = {.fn = eventFn, .ctx = eventCtx};
    uint32_t nowMs;
    CoprocStatus status;

    if (frame == NULL || sim == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (frame->header.msgClass != COPROC_MSG_CLASS_MOTION) {
        return COPROC_STATUS_UNSUPPORTED;
    }

    nowMs = CoprocStressSim_NowMs(sim);
    switch (frame->header.msgId) {
        case COPROC_MOTION_MSG_SERVO_MOVE:
            status = CoprocStressSim_FlushDueMotion(sim,
                                                    nowMs,
                                                    allocSeqFn,
                                                    allocSeqCtx,
                                                    txWriteFn,
                                                    txWriteCtx,
                                                    &eventSink);
            if (status != COPROC_STATUS_OK) {
                return status;
            }
            if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_SERVO_MOVE) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_PAYLOAD,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }
            if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_STATE,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }
            if (frame->payload[0] == 0u ||
                (frame->payload[0] & ~0x03u) != 0u ||
                frame->payload[5] == 0u ||
                frame->payload[7] != 0u ||
                frame->payload[8] > 4u) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_PAYLOAD,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }

            status = CoprocStressSim_SendAck(frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, &eventSink);
            if (status != COPROC_STATUS_OK) {
                return status;
            }

            if (sim->streamingEnabled == 0u) {
                sim->nextMagAtMs = nowMs + 500u;
                sim->nextTouchPressAtMs = nowMs + 1000u;
                sim->nextTouchReleaseAtMs = 0u;
                sim->touchActive = 0u;
            }
            sim->motion.active = 1u;
            sim->streamingEnabled = 1u;
            sim->motion.refSeq = frame->header.seq;
            sim->motion.xDegX10 = CoprocStressSim_ReadI16Le(&frame->payload[1]);
            sim->motion.yDegX10 = CoprocStressSim_ReadI16Le(&frame->payload[3]);
            sim->motion.durationMs = CoprocStressSim_ReadU16Le(&frame->payload[5]);
            sim->motion.startedAtMs = nowMs;
            sim->motion.completeAtMs = nowMs + sim->motion.durationMs;
            sim->stats.servoMoveRxCount++;
            CoprocStressSim_EmitEvent(&eventSink,
                                      COPROC_DISPATCH_EVENT_SERVO_MOVE,
                                      &frame->header,
                                      frame->header.seq,
                                      0u,
                                      frame->payload[0],
                                      sim->motion.xDegX10,
                                      sim->motion.yDegX10,
                                      0,
                                      sim->motion.durationMs,
                                      frame->payload[7],
                                      frame->payload[8],
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u);
            return COPROC_STATUS_OK;
        case COPROC_MOTION_MSG_SERVO_STOP:
            if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_SERVO_STOP) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_PAYLOAD,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }
            if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_STATE,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }
            if (frame->payload[0] > 1u || frame->payload[1] > 4u) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_PAYLOAD,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }

            sim->stats.servoStopRxCount++;
            CoprocStressSim_EmitEvent(&eventSink,
                                      COPROC_DISPATCH_EVENT_SERVO_STOP,
                                      &frame->header,
                                      frame->header.seq,
                                      0u,
                                      0u,
                                      0,
                                      0,
                                      0,
                                      0u,
                                      0u,
                                      frame->payload[1],
                                      frame->payload[0],
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u,
                                      0u);

            if (sim->motion.active == 0u) {
                return CoprocStressSim_SendNack(frame,
                                                COPROC_NACK_REASON_INVALID_STATE,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
            }

            status = CoprocStressSim_SendAck(frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, &eventSink);
            if (status != COPROC_STATUS_OK) {
                return status;
            }

            status = CoprocStressSim_SendMotionDone(sim,
                                                    COPROC_MOTION_RESULT_STOPPED,
                                                    sim->motion.refSeq,
                                                    sim->motion.xDegX10,
                                                    sim->motion.yDegX10,
                                                    (uint16_t)(nowMs - sim->motion.startedAtMs),
                                                    allocSeqFn,
                                                    allocSeqCtx,
                                                    txWriteFn,
                                                    txWriteCtx,
                                                    &eventSink);
            if (status == COPROC_STATUS_OK) {
                sim->motion.active = 0u;
            }
            return status;
        default:
            return CoprocStressSim_SendNack(frame,
                                            COPROC_NACK_REASON_UNSUPPORTED_MSG,
                                            allocSeqFn,
                                            allocSeqCtx,
                                            txWriteFn,
                                            txWriteCtx,
                                            &eventSink);
    }
}

CoprocStatus CoprocStressSim_Poll(CoprocStressSim *sim,
                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                  void *allocSeqCtx,
                                  CoprocDispatchTxWriteFn txWriteFn,
                                  void *txWriteCtx,
                                  CoprocDispatchEventFn eventFn,
                                  void *eventCtx)
{
    CoprocStressSimEventSink eventSink = {.fn = eventFn, .ctx = eventCtx};
    uint32_t nowMs;
    CoprocStatus status;

    if (sim == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    nowMs = CoprocStressSim_NowMs(sim);
    status = CoprocStressSim_FlushDueMotion(sim,
                                            nowMs,
                                            allocSeqFn,
                                            allocSeqCtx,
                                            txWriteFn,
                                            txWriteCtx,
                                            &eventSink);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    if (sim->streamingEnabled == 0u) {
        return COPROC_STATUS_OK;
    }

    while (nowMs >= sim->nextMagAtMs) {
        status = CoprocStressSim_SendMagState(sim,
                                              (uint16_t)(9000u + ((sim->magSampleIndex * 125u) % 18000u)),
                                              (uint16_t)(320u + (sim->magSampleIndex % 30u)),
                                              (uint8_t)(80u + (sim->magSampleIndex % 20u)),
                                              (uint8_t)(sim->magSampleIndex & 0x01u),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              &eventSink);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        sim->magSampleIndex++;
        sim->nextMagAtMs += 500u;
    }

    while (nowMs >= sim->nextTouchPressAtMs) {
        status = CoprocStressSim_SendTouchEvent(sim,
                                                0u,
                                                COPROC_TOUCH_EVENT_PRESS,
                                                sim->nextTouchPressAtMs,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        sim->touchActive = 1u;
        sim->nextTouchReleaseAtMs = sim->nextTouchPressAtMs + 50u;
        sim->nextTouchPressAtMs += 1000u;
    }

    while (sim->touchActive != 0u && nowMs >= sim->nextTouchReleaseAtMs) {
        status = CoprocStressSim_SendTouchEvent(sim,
                                                0u,
                                                COPROC_TOUCH_EVENT_RELEASE,
                                                sim->nextTouchReleaseAtMs,
                                                allocSeqFn,
                                                allocSeqCtx,
                                                txWriteFn,
                                                txWriteCtx,
                                                &eventSink);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        sim->touchActive = 0u;
        sim->nextTouchReleaseAtMs = 0u;
    }

    return COPROC_STATUS_OK;
}

const CoprocStressSimStats *CoprocStressSim_GetStats(const CoprocStressSim *sim)
{
    return (sim != NULL) ? &sim->stats : NULL;
}
