#include "coproc_runtime.h"

#include <string.h>

#define COPROC_RUNTIME_DEFAULT_TOUCH_DEBOUNCE_MS 30u
#define COPROC_RUNTIME_AXIS_X 0x01u
#define COPROC_RUNTIME_AXIS_Y 0x02u
#define COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10 900
#define COPROC_RUNTIME_SERVO_FOR_AXIS_X 2u
#define COPROC_RUNTIME_SERVO_FOR_AXIS_Y 1u
#define COPROC_RUNTIME_MAX_SERVO_DEG_X10 1800
#define COPROC_RUNTIME_LED_TARGET_ALL 0u
#define COPROC_RUNTIME_LED_TARGET_SIDE 1u
#define COPROC_RUNTIME_LED_TARGET_BOTTOM 2u

static uint8_t CoprocRuntime_ReadServoFeedbackDegX10(CoprocRuntime *runtime,
                                                     uint8_t servoIndex,
                                                     int16_t *degX10);

static uint16_t CoprocRuntime_ReadU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8u));
}

static int16_t CoprocRuntime_ReadI16Le(const uint8_t *src)
{
    return (int16_t)CoprocRuntime_ReadU16Le(src);
}

static uint32_t CoprocRuntime_NowMs(CoprocRuntime *runtime)
{
    if (runtime == NULL || runtime->config.nowMsFn == NULL) {
        return 0u;
    }

    return runtime->config.nowMsFn(runtime->config.nowMsCtx);
}

static uint8_t CoprocRuntime_NextTouchQueueIndex(uint8_t index)
{
    index++;
    if (index >= COPROC_RUNTIME_TOUCH_QUEUE_DEPTH) {
        index = 0u;
    }

    return index;
}

static uint8_t CoprocRuntime_NextMotionQueueIndex(uint8_t index)
{
    index++;
    if (index >= COPROC_RUNTIME_MOTION_QUEUE_DEPTH) {
        index = 0u;
    }

    return index;
}

static uint8_t CoprocRuntime_EnqueueMotionCommand(CoprocRuntime *runtime, const CoprocRuntimeMotionCommand *command)
{
    uint8_t nextHead;

    if (runtime == NULL || command == NULL) {
        return 0u;
    }

    nextHead = CoprocRuntime_NextMotionQueueIndex(runtime->motionQueueHead);
    if (nextHead == runtime->motionQueueTail) {
        runtime->stats.eventDropCount++;
        return 0u;
    }

    runtime->motionQueue[runtime->motionQueueHead] = *command;
    runtime->motionQueueHead = nextHead;
    return 1u;
}

static uint8_t CoprocRuntime_DequeueMotionCommand(CoprocRuntime *runtime, CoprocRuntimeMotionCommand *command)
{
    if (runtime == NULL || command == NULL || runtime->motionQueueTail == runtime->motionQueueHead) {
        return 0u;
    }

    *command = runtime->motionQueue[runtime->motionQueueTail];
    runtime->motionQueueTail = CoprocRuntime_NextMotionQueueIndex(runtime->motionQueueTail);
    return 1u;
}

static void CoprocRuntime_ClearMotionQueue(CoprocRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }

    runtime->motionQueueHead = 0u;
    runtime->motionQueueTail = 0u;
}

static uint8_t CoprocRuntime_EnqueueTouchEvent(CoprocRuntime *runtime, uint8_t code, uint32_t timestampMs)
{
    uint8_t nextHead;

    if (runtime == NULL) {
        return 0u;
    }

    nextHead = CoprocRuntime_NextTouchQueueIndex(runtime->touchQueueHead);
    if (nextHead == runtime->touchQueueTail) {
        runtime->stats.eventDropCount++;
        return 0u;
    }

    runtime->touchQueue[runtime->touchQueueHead].code = code;
    runtime->touchQueue[runtime->touchQueueHead].timestampMs = timestampMs;
    runtime->touchQueueHead = nextHead;
    runtime->stats.touchEventQueuedCount++;
    return 1u;
}

static uint8_t CoprocRuntime_DequeueTouchEvent(CoprocRuntime *runtime, CoprocRuntimeTouchEvent *event)
{
    if (runtime == NULL || event == NULL || runtime->touchQueueTail == runtime->touchQueueHead) {
        return 0u;
    }

    *event = runtime->touchQueue[runtime->touchQueueTail];
    runtime->touchQueueTail = CoprocRuntime_NextTouchQueueIndex(runtime->touchQueueTail);
    return 1u;
}

static CoprocStatus CoprocRuntime_SendMessage(CoprocRuntime *runtime,
                                              const CoprocTxMessage *message,
                                              CoprocDispatchTxWriteFn txWriteFn,
                                              void *txWriteCtx)
{
    CoprocStatus status;

    if (message == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    status = txWriteFn(txWriteCtx, message->wire, message->wireLength);
    if (status != COPROC_STATUS_OK && runtime != NULL) {
        runtime->stats.txFailCount++;
    }
    return status;
}

static void CoprocRuntime_EmitEvent(CoprocDispatchEventFn eventFn,
                                    void *eventCtx,
                                    CoprocDispatchEventType type,
                                    const CoprocFrameHeader *header,
                                    uint32_t refSeq,
                                    uint16_t reasonCode,
                                    uint8_t axisMask,
                                    int16_t xValue,
                                    int16_t yValue,
                                    uint16_t durationMs,
                                    uint8_t motionProfile,
                                    uint8_t sourceTag,
                                    uint8_t stopScope,
                                    uint8_t motionResult,
                                    uint16_t execTimeMs,
                                    uint8_t red,
                                    uint8_t green,
                                    uint8_t blue,
                                    uint8_t activeCount,
                                    uint8_t ledStep,
                                    uint8_t ledIntervalMs,
                                    uint8_t ledResult,
                                    uint8_t touchCode,
                                    uint32_t timestampMs)
{
    CoprocDispatchEvent event;

    if (eventFn == NULL || header == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.header = *header;
    event.refSeq = refSeq;
    event.reasonCode = reasonCode;
    event.axisMask = axisMask;
    event.xValue = xValue;
    event.yValue = yValue;
    event.durationMs = durationMs;
    event.motionProfile = motionProfile;
    event.sourceTag = sourceTag;
    event.stopScope = stopScope;
    event.motionResult = motionResult;
    event.execTimeMs = execTimeMs;
    event.red = red;
    event.green = green;
    event.blue = blue;
    event.activeCount = activeCount;
    event.ledStep = ledStep;
    event.ledIntervalMs = ledIntervalMs;
    event.ledResult = ledResult;
    event.touchId = 0u;
    event.touchCode = touchCode;
    event.timestampMs = timestampMs;
    event.powerEnabled = (type == COPROC_DISPATCH_EVENT_POWER_5V_ENABLE) ? 1u : 0u;
    eventFn(eventCtx, &event);
}

static CoprocStatus CoprocRuntime_SendAck(CoprocRuntime *runtime,
                                          const CoprocFrame *frame,
                                          CoprocDispatchAllocSeqFn allocSeqFn,
                                          void *allocSeqCtx,
                                          CoprocDispatchTxWriteFn txWriteFn,
                                          void *txWriteCtx,
                                          CoprocDispatchEventFn eventFn,
                                          void *eventCtx)
{
    CoprocTxMessage message;
    CoprocStatus status;

    if (frame == NULL || allocSeqFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    status = CoprocTxBuilder_BuildAck(allocSeqFn(allocSeqCtx), frame->header.seq, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocRuntime_SendMessage(runtime, &message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        CoprocRuntime_EmitEvent(eventFn,
                                eventCtx,
                                COPROC_DISPATCH_EVENT_ACK,
                                &message.frame.header,
                                frame->header.seq,
                                0u,
                                0u,
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
                                0u,
                                0u,
                                0u);
    }
    return status;
}

static CoprocStatus CoprocRuntime_SendNack(CoprocRuntime *runtime,
                                           const CoprocFrame *frame,
                                           uint16_t reasonCode,
                                           CoprocDispatchAllocSeqFn allocSeqFn,
                                           void *allocSeqCtx,
                                           CoprocDispatchTxWriteFn txWriteFn,
                                           void *txWriteCtx,
                                           CoprocDispatchEventFn eventFn,
                                           void *eventCtx)
{
    CoprocTxMessage message;
    CoprocStatus status;

    if (frame == NULL || allocSeqFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    status = CoprocTxBuilder_BuildNack(allocSeqFn(allocSeqCtx), frame->header.seq, reasonCode, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocRuntime_SendMessage(runtime, &message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        CoprocRuntime_EmitEvent(eventFn,
                                eventCtx,
                                COPROC_DISPATCH_EVENT_NACK,
                                &message.frame.header,
                                frame->header.seq,
                                reasonCode,
                                0u,
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
                                0u,
                                0u,
                                0u);
    }
    return status;
}

static CoprocStatus CoprocRuntime_SendMotionDone(CoprocRuntime *runtime,
                                                 uint8_t resultCode,
                                                 uint16_t execTimeMs,
                                                 CoprocDispatchAllocSeqFn allocSeqFn,
                                                 void *allocSeqCtx,
                                                 CoprocDispatchTxWriteFn txWriteFn,
                                                 void *txWriteCtx,
                                                 CoprocDispatchEventFn eventFn,
                                                 void *eventCtx)
{
    CoprocTxMessage message;
    CoprocStatus status;
    int16_t finalXDegX10;
    int16_t finalYDegX10;

    if (runtime == NULL || allocSeqFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    finalXDegX10 = runtime->motionFinalXDegX10;
    finalYDegX10 = runtime->motionFinalYDegX10;
    if ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_X) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_X, &finalXDegX10);
    }
    if ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_Y) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_Y, &finalYDegX10);
    }

    status = CoprocTxBuilder_BuildMotionDone(allocSeqFn(allocSeqCtx),
                                             runtime->motionRefSeq,
                                             resultCode,
                                             finalXDegX10,
                                             finalYDegX10,
                                             execTimeMs,
                                             &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocRuntime_SendMessage(runtime, &message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        runtime->stats.motionDoneTxCount++;
        CoprocRuntime_EmitEvent(eventFn,
                                eventCtx,
                                COPROC_DISPATCH_EVENT_MOTION_DONE,
                                &message.frame.header,
                                runtime->motionRefSeq,
                                0u,
                                0u,
                                finalXDegX10,
                                finalYDegX10,
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
                                0u,
                                0u,
                                0u);
    }
    return status;
}

static void CoprocRuntime_EmitLedEvent(CoprocDispatchEventFn eventFn,
                                       void *eventCtx,
                                       CoprocDispatchEventType type,
                                       const CoprocFrameHeader *header,
                                       uint32_t refSeq,
                                       uint8_t red,
                                       uint8_t green,
                                       uint8_t blue,
                                       uint8_t activeCount,
                                       uint8_t ledStep,
                                       uint8_t ledIntervalMs,
                                       uint8_t ledResult,
                                       uint8_t sourceTag)
{
    CoprocRuntime_EmitEvent(eventFn,
                            eventCtx,
                            type,
                            header,
                            refSeq,
                            0u,
                            0u,
                            0,
                            0,
                            0u,
                            0u,
                            sourceTag,
                            0u,
                            0u,
                            0u,
                            red,
                            green,
                            blue,
                            activeCount,
                            ledStep,
                            ledIntervalMs,
                            ledResult,
                            0u,
                            0u);
}

static CoprocStatus CoprocRuntime_SendLedDone(CoprocRuntime *runtime,
                                              const CoprocFrame *frame,
                                              uint8_t resultCode,
                                              CoprocDispatchAllocSeqFn allocSeqFn,
                                              void *allocSeqCtx,
                                              CoprocDispatchTxWriteFn txWriteFn,
                                              void *txWriteCtx,
                                              CoprocDispatchEventFn eventFn,
                                              void *eventCtx)
{
    CoprocTxMessage message;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL || allocSeqFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    status = CoprocTxBuilder_BuildLedDone(allocSeqFn(allocSeqCtx), frame->header.seq, resultCode, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocRuntime_SendMessage(runtime, &message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        runtime->stats.ledDoneTxCount++;
        CoprocRuntime_EmitLedEvent(eventFn,
                                   eventCtx,
                                   COPROC_DISPATCH_EVENT_LED_DONE,
                                   &message.frame.header,
                                   frame->header.seq,
                                   0u,
                                   0u,
                                   0u,
                                   0u,
                                   0u,
                                   0u,
                                   resultCode,
                                   0u);
    }
    return status;
}

static uint8_t CoprocRuntime_DegX10ToAngle(int16_t degX10, uint8_t *angle)
{
    if (angle == NULL || degX10 < 0 || degX10 > 1800) {
        return 0u;
    }

    *angle = (uint8_t)(((uint16_t)degX10 + 5u) / 10u);
    return 1u;
}

static int16_t CoprocRuntime_AngleToDegX10(uint8_t angle)
{
    return (int16_t)((uint16_t)angle * 10u);
}

static uint8_t CoprocRuntime_ReadServoFeedbackDegX10(CoprocRuntime *runtime,
                                                     uint8_t servoIndex,
                                                     int16_t *degX10)
{
    uint8_t commandAngle = 0u;
    uint16_t feedbackRaw = 0u;
    uint16_t feedbackMv = 0u;
    uint8_t feedbackAngle = 0u;

    (void)feedbackRaw;
    (void)feedbackMv;
    if (runtime == NULL || degX10 == NULL || runtime->config.readServoFeedbackFn == NULL) {
        return 0u;
    }

    if (runtime->config.readServoFeedbackFn(runtime->config.readServoFeedbackCtx,
                                            servoIndex,
                                            &commandAngle,
                                            &feedbackRaw,
                                            &feedbackMv,
                                            &feedbackAngle) == 0u) {
        runtime->stats.feedbackFailCount++;
        return 0u;
    }

    (void)commandAngle;
    *degX10 = CoprocRuntime_AngleToDegX10(feedbackAngle);
    return 1u;
}

static uint8_t CoprocRuntime_ApplyServoDegX10(CoprocRuntime *runtime, uint8_t servoIndex, int16_t degX10)
{
    uint8_t angle;
    uint16_t pulseUs = 0u;

    if (runtime == NULL || runtime->config.setServoAngleFn == NULL ||
        CoprocRuntime_DegX10ToAngle(degX10, &angle) == 0u) {
        return 0u;
    }

    return runtime->config.setServoAngleFn(runtime->config.setServoAngleCtx, servoIndex, angle, &pulseUs);
}

static uint8_t CoprocRuntime_IsMotionProfileSupported(uint8_t motionProfile)
{
    return (motionProfile == COPROC_MOTION_PROFILE_LINEAR ||
            motionProfile == COPROC_MOTION_PROFILE_EASE_IN_OUT)
               ? 1u
               : 0u;
}

static int64_t CoprocRuntime_DivideRoundedI64(int64_t numerator, int64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }

    if (numerator >= 0) {
        return (numerator + (denominator / 2)) / denominator;
    }

    return (numerator - (denominator / 2)) / denominator;
}

static int16_t CoprocRuntime_InterpolateDegX10(int16_t startDegX10,
                                               int16_t targetDegX10,
                                               uint32_t elapsedMs,
                                               uint16_t durationMs,
                                               uint8_t motionProfile)
{
    int32_t delta = (int32_t)targetDegX10 - (int32_t)startDegX10;

    if (elapsedMs >= durationMs) {
        return targetDegX10;
    }

    if (motionProfile == COPROC_MOTION_PROFILE_EASE_IN_OUT) {
        uint64_t elapsed = elapsedMs;
        uint64_t duration = durationMs;
        uint64_t weightNumerator = elapsed * elapsed * ((3u * duration) - (2u * elapsed));
        uint64_t weightDenominator = duration * duration * duration;
        int64_t step = CoprocRuntime_DivideRoundedI64((int64_t)delta * (int64_t)weightNumerator,
                                                      (int64_t)weightDenominator);

        return (int16_t)((int32_t)startDegX10 + (int32_t)step);
    }

    return (int16_t)((int32_t)startDegX10 + ((delta * (int32_t)elapsedMs) / (int32_t)durationMs));
}

static int16_t CoprocRuntime_ClampDegX10(int32_t value, int16_t minValue, int16_t maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return (int16_t)value;
}

static uint8_t CoprocRuntime_ApplyMotionAt(CoprocRuntime *runtime, uint32_t nowMs)
{
    uint32_t elapsedMs;
    int16_t xDegX10;
    int16_t yDegX10;

    if (runtime == NULL || runtime->motionActive == 0u) {
        return 1u;
    }

    elapsedMs = nowMs - runtime->motionStartedAtMs;
    xDegX10 = CoprocRuntime_InterpolateDegX10(runtime->motionStartXDegX10,
                                               runtime->motionFinalXDegX10,
                                               elapsedMs,
                                               runtime->motionDurationMs,
                                               runtime->motionProfile);
    yDegX10 = CoprocRuntime_InterpolateDegX10(runtime->motionStartYDegX10,
                                               runtime->motionFinalYDegX10,
                                               elapsedMs,
                                               runtime->motionDurationMs,
                                               runtime->motionProfile);

    if (((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_X) != 0u &&
         CoprocRuntime_ApplyServoDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_X, xDegX10) == 0u) ||
        ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_Y) != 0u &&
         CoprocRuntime_ApplyServoDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_Y, yDegX10) == 0u)) {
        return 0u;
    }

    return 1u;
}

static uint8_t CoprocRuntime_ApplyJogAt(CoprocRuntime *runtime, uint32_t nowMs)
{
    uint32_t elapsedMs;
    int32_t xDegX10;
    int32_t yDegX10;

    if (runtime == NULL || runtime->jogActive == 0u) {
        return 1u;
    }

    elapsedMs = nowMs - runtime->jogLastUpdateMs;
    if (elapsedMs == 0u) {
        return 1u;
    }

    xDegX10 = runtime->motionFinalXDegX10;
    yDegX10 = runtime->motionFinalYDegX10;
    if ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_X) != 0u) {
        xDegX10 += ((int32_t)runtime->jogVelocityXDegX10PerSec * (int32_t)elapsedMs) / 1000;
        runtime->motionFinalXDegX10 = CoprocRuntime_ClampDegX10(xDegX10,
                                                                runtime->jogMinXDegX10,
                                                                runtime->jogMaxXDegX10);
        if (CoprocRuntime_ApplyServoDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_X, runtime->motionFinalXDegX10) == 0u) {
            return 0u;
        }
    }
    if ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_Y) != 0u) {
        yDegX10 += ((int32_t)runtime->jogVelocityYDegX10PerSec * (int32_t)elapsedMs) / 1000;
        runtime->motionFinalYDegX10 = CoprocRuntime_ClampDegX10(yDegX10,
                                                                runtime->jogMinYDegX10,
                                                                runtime->jogMaxYDegX10);
        if (CoprocRuntime_ApplyServoDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_Y, runtime->motionFinalYDegX10) == 0u) {
            return 0u;
        }
    }

    runtime->jogLastUpdateMs = nowMs;
    return 1u;
}

static uint8_t CoprocRuntime_StartMotionCommand(CoprocRuntime *runtime, const CoprocRuntimeMotionCommand *command)
{
    uint32_t nowMs;
    int16_t startXDegX10;
    int16_t startYDegX10;

    if (runtime == NULL || command == NULL) {
        return 0u;
    }

    startXDegX10 = runtime->motionFinalXDegX10;
    startYDegX10 = runtime->motionFinalYDegX10;
    if ((command->axisMask & COPROC_RUNTIME_AXIS_X) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_X, &startXDegX10);
    }
    if ((command->axisMask & COPROC_RUNTIME_AXIS_Y) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_Y, &startYDegX10);
    }

    nowMs = CoprocRuntime_NowMs(runtime);
    runtime->motionActive = 1u;
    runtime->motionAxisMask = command->axisMask;
    runtime->motionRefSeq = command->refSeq;
    runtime->motionStartXDegX10 = startXDegX10;
    runtime->motionStartYDegX10 = startYDegX10;
    runtime->motionFinalXDegX10 = ((command->axisMask & COPROC_RUNTIME_AXIS_X) != 0u) ? command->targetXDegX10
                                                                                      : runtime->motionFinalXDegX10;
    runtime->motionFinalYDegX10 = ((command->axisMask & COPROC_RUNTIME_AXIS_Y) != 0u) ? command->targetYDegX10
                                                                                       : runtime->motionFinalYDegX10;
    runtime->motionDurationMs = command->durationMs;
    runtime->motionProfile = command->motionProfile;
    runtime->motionStartedAtMs = nowMs;
    runtime->motionCompleteAtMs = nowMs + command->durationMs;
    if (CoprocRuntime_ApplyMotionAt(runtime, nowMs) == 0u) {
        runtime->motionActive = 0u;
        return 0u;
    }

    return 1u;
}

static CoprocStatus CoprocRuntime_StartNextQueuedMotion(CoprocRuntime *runtime,
                                                        CoprocDispatchAllocSeqFn allocSeqFn,
                                                        void *allocSeqCtx,
                                                        CoprocDispatchTxWriteFn txWriteFn,
                                                        void *txWriteCtx,
                                                        CoprocDispatchEventFn eventFn,
                                                        void *eventCtx)
{
    CoprocRuntimeMotionCommand command;
    uint32_t nowMs;

    if (runtime == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (CoprocRuntime_DequeueMotionCommand(runtime, &command) == 0u) {
        return COPROC_STATUS_OK;
    }

    if (CoprocRuntime_StartMotionCommand(runtime, &command) != 0u) {
        return COPROC_STATUS_OK;
    }

    nowMs = CoprocRuntime_NowMs(runtime);
    runtime->motionActive = 0u;
    runtime->motionAxisMask = command.axisMask;
    runtime->motionRefSeq = command.refSeq;
    runtime->motionFinalXDegX10 = ((command.axisMask & COPROC_RUNTIME_AXIS_X) != 0u) ? command.targetXDegX10
                                                                                     : runtime->motionFinalXDegX10;
    runtime->motionFinalYDegX10 = ((command.axisMask & COPROC_RUNTIME_AXIS_Y) != 0u) ? command.targetYDegX10
                                                                                      : runtime->motionFinalYDegX10;
    runtime->motionDurationMs = command.durationMs;
    runtime->motionProfile = command.motionProfile;
    runtime->motionStartedAtMs = nowMs;
    return CoprocRuntime_SendMotionDone(runtime,
                                        COPROC_MOTION_RESULT_FAULT,
                                        0u,
                                        allocSeqFn,
                                        allocSeqCtx,
                                        txWriteFn,
                                        txWriteCtx,
                                        eventFn,
                                        eventCtx);
}

static CoprocStatus CoprocRuntime_HandleServoMove(CoprocRuntime *runtime,
                                                  const CoprocFrame *frame,
                                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                                  void *allocSeqCtx,
                                                  CoprocDispatchTxWriteFn txWriteFn,
                                                  void *txWriteCtx,
                                                  CoprocDispatchEventFn eventFn,
                                                  void *eventCtx)
{
    uint8_t axisMask;
    int16_t xDegX10;
    int16_t yDegX10;
    uint16_t durationMs;
    uint8_t xAngle = 0u;
    uint8_t yAngle = 0u;
    CoprocRuntimeMotionCommand command;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_SERVO_MOVE) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (runtime->config.setServoAngleFn == NULL) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_CAPABILITY_MISSING, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    axisMask = frame->payload[0];
    xDegX10 = CoprocRuntime_ReadI16Le(&frame->payload[1]);
    yDegX10 = CoprocRuntime_ReadI16Le(&frame->payload[3]);
    durationMs = CoprocRuntime_ReadU16Le(&frame->payload[5]);

    if (axisMask == 0u || (axisMask & ~(COPROC_RUNTIME_AXIS_X | COPROC_RUNTIME_AXIS_Y)) != 0u ||
        durationMs == 0u || CoprocRuntime_IsMotionProfileSupported(frame->payload[7]) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (((axisMask & COPROC_RUNTIME_AXIS_X) != 0u && CoprocRuntime_DegX10ToAngle(xDegX10, &xAngle) == 0u) ||
        ((axisMask & COPROC_RUNTIME_AXIS_Y) != 0u && CoprocRuntime_DegX10ToAngle(yDegX10, &yAngle) == 0u)) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_OUT_OF_RANGE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    (void)xAngle;
    (void)yAngle;
    command.axisMask = axisMask;
    command.refSeq = frame->header.seq;
    command.targetXDegX10 = xDegX10;
    command.targetYDegX10 = yDegX10;
    command.durationMs = durationMs;
    command.motionProfile = frame->payload[7];
    command.sourceTag = frame->payload[8];

    if (runtime->motionActive == 0u && CoprocRuntime_StartMotionCommand(runtime, &command) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INTERNAL_ERROR, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (runtime->motionActive != 0u && runtime->motionRefSeq != frame->header.seq) {
        if (CoprocRuntime_EnqueueMotionCommand(runtime, &command) == 0u) {
            return CoprocRuntime_SendNack(runtime,
                                          frame,
                                          COPROC_NACK_REASON_INVALID_STATE,
                                          allocSeqFn,
                                          allocSeqCtx,
                                          txWriteFn,
                                          txWriteCtx,
                                          eventFn,
                                          eventCtx);
        }
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        if (runtime->motionRefSeq == frame->header.seq) {
            runtime->motionActive = 0u;
        }
        return status;
    }
    runtime->stats.servoMoveRxCount++;
    CoprocRuntime_EmitEvent(eventFn,
                            eventCtx,
                            COPROC_DISPATCH_EVENT_SERVO_MOVE,
                            &frame->header,
                            frame->header.seq,
                            0u,
                            axisMask,
                            xDegX10,
                            yDegX10,
                            durationMs,
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
                            0u,
                            0u,
                            0u);
    return COPROC_STATUS_OK;
}

static CoprocStatus CoprocRuntime_HandleServoJog(CoprocRuntime *runtime,
                                                 const CoprocFrame *frame,
                                                 CoprocDispatchAllocSeqFn allocSeqFn,
                                                 void *allocSeqCtx,
                                                 CoprocDispatchTxWriteFn txWriteFn,
                                                 void *txWriteCtx,
                                                 CoprocDispatchEventFn eventFn,
                                                 void *eventCtx)
{
    uint8_t axisMask;
    int16_t xVelocityDegX10PerSec;
    int16_t yVelocityDegX10PerSec;
    uint16_t timeoutMs;
    uint8_t xMinDeg;
    uint8_t xMaxDeg;
    uint8_t yMinDeg;
    uint8_t yMaxDeg;
    uint32_t nowMs;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }
    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_SERVO_JOG) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u || runtime->config.setServoAngleFn == NULL) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    axisMask = frame->payload[0];
    xVelocityDegX10PerSec = CoprocRuntime_ReadI16Le(&frame->payload[1]);
    yVelocityDegX10PerSec = CoprocRuntime_ReadI16Le(&frame->payload[3]);
    timeoutMs = CoprocRuntime_ReadU16Le(&frame->payload[5]);
    xMinDeg = frame->payload[8];
    xMaxDeg = frame->payload[9];
    yMinDeg = frame->payload[10];
    yMaxDeg = frame->payload[11];

    if (axisMask == 0u || (axisMask & ~(COPROC_RUNTIME_AXIS_X | COPROC_RUNTIME_AXIS_Y)) != 0u ||
        timeoutMs == 0u ||
        xMinDeg > xMaxDeg || yMinDeg > yMaxDeg ||
        ((uint16_t)xMaxDeg * 10u) > COPROC_RUNTIME_MAX_SERVO_DEG_X10 ||
        ((uint16_t)yMaxDeg * 10u) > COPROC_RUNTIME_MAX_SERVO_DEG_X10 ||
        ((axisMask & COPROC_RUNTIME_AXIS_X) != 0u && xVelocityDegX10PerSec == 0) ||
        ((axisMask & COPROC_RUNTIME_AXIS_Y) != 0u && yVelocityDegX10PerSec == 0)) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    nowMs = CoprocRuntime_NowMs(runtime);
    if (runtime->jogActive != 0u && CoprocRuntime_ApplyJogAt(runtime, nowMs) == 0u) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_FAULT,
                                              (uint16_t)(nowMs - runtime->motionStartedAtMs),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        runtime->jogActive = 0u;
        return status;
    }
    if (runtime->motionActive != 0u) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_INTERRUPTED,
                                              (uint16_t)(nowMs - runtime->motionStartedAtMs),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
    }

    CoprocRuntime_ClearMotionQueue(runtime);
    runtime->motionActive = 0u;
    runtime->jogActive = 1u;
    runtime->motionAxisMask = axisMask;
    runtime->motionRefSeq = frame->header.seq;
    runtime->motionStartedAtMs = nowMs;
    runtime->motionDurationMs = timeoutMs;
    runtime->motionCompleteAtMs = nowMs + timeoutMs;
    runtime->jogLastUpdateMs = nowMs;
    runtime->jogTimeoutAtMs = nowMs + timeoutMs;
    runtime->jogVelocityXDegX10PerSec = ((axisMask & COPROC_RUNTIME_AXIS_X) != 0u) ? xVelocityDegX10PerSec : 0;
    runtime->jogVelocityYDegX10PerSec = ((axisMask & COPROC_RUNTIME_AXIS_Y) != 0u) ? yVelocityDegX10PerSec : 0;
    runtime->jogMinXDegX10 = (int16_t)((uint16_t)xMinDeg * 10u);
    runtime->jogMaxXDegX10 = (int16_t)((uint16_t)xMaxDeg * 10u);
    runtime->jogMinYDegX10 = (int16_t)((uint16_t)yMinDeg * 10u);
    runtime->jogMaxYDegX10 = (int16_t)((uint16_t)yMaxDeg * 10u);
    runtime->motionStartXDegX10 = runtime->motionFinalXDegX10;
    runtime->motionStartYDegX10 = runtime->motionFinalYDegX10;
    if ((axisMask & COPROC_RUNTIME_AXIS_X) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_X,
                                                    &runtime->motionStartXDegX10);
        runtime->motionFinalXDegX10 = runtime->motionStartXDegX10;
    }
    if ((axisMask & COPROC_RUNTIME_AXIS_Y) != 0u) {
        (void)CoprocRuntime_ReadServoFeedbackDegX10(runtime, COPROC_RUNTIME_SERVO_FOR_AXIS_Y,
                                                    &runtime->motionStartYDegX10);
        runtime->motionFinalYDegX10 = runtime->motionStartYDegX10;
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        runtime->jogActive = 0u;
        return status;
    }

    CoprocRuntime_EmitEvent(eventFn,
                            eventCtx,
                            COPROC_DISPATCH_EVENT_SERVO_MOVE,
                            &frame->header,
                            frame->header.seq,
                            0u,
                            axisMask,
                            xVelocityDegX10PerSec,
                            yVelocityDegX10PerSec,
                            timeoutMs,
                            0u,
                            frame->payload[7],
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
    return COPROC_STATUS_OK;
}

static CoprocStatus CoprocRuntime_HandleServoStop(CoprocRuntime *runtime,
                                                  const CoprocFrame *frame,
                                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                                  void *allocSeqCtx,
                                                  CoprocDispatchTxWriteFn txWriteFn,
                                                  void *txWriteCtx,
                                                  CoprocDispatchEventFn eventFn,
                                                  void *eventCtx)
{
    CoprocStatus status;
    uint8_t stopScope;
    uint32_t nowMs;
    uint16_t execTimeMs;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_SERVO_STOP) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    stopScope = frame->payload[0];
    if (stopScope > 1u || frame->payload[1] > 4u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    nowMs = CoprocRuntime_NowMs(runtime);
    if ((runtime->jogActive != 0u && CoprocRuntime_ApplyJogAt(runtime, nowMs) == 0u) ||
        (runtime->motionActive != 0u && CoprocRuntime_ApplyMotionAt(runtime, nowMs) == 0u)) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INTERNAL_ERROR, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((runtime->motionActive != 0u || runtime->jogActive != 0u) && runtime->config.stopServoFn != NULL) {
        if (((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_X) != 0u &&
             runtime->config.stopServoFn(runtime->config.stopServoCtx, COPROC_RUNTIME_SERVO_FOR_AXIS_X) == 0u) ||
            ((runtime->motionAxisMask & COPROC_RUNTIME_AXIS_Y) != 0u &&
             runtime->config.stopServoFn(runtime->config.stopServoCtx, COPROC_RUNTIME_SERVO_FOR_AXIS_Y) == 0u)) {
            return CoprocRuntime_SendNack(runtime,
                                          frame,
                                          COPROC_NACK_REASON_INTERNAL_ERROR,
                                          allocSeqFn,
                                          allocSeqCtx,
                                          txWriteFn,
                                          txWriteCtx,
                                          eventFn,
                                          eventCtx);
        }
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    if (stopScope == 1u) {
        CoprocRuntime_ClearMotionQueue(runtime);
    }

    runtime->stats.servoStopRxCount++;
    CoprocRuntime_EmitEvent(eventFn,
                            eventCtx,
                            COPROC_DISPATCH_EVENT_SERVO_STOP,
                            &frame->header,
                            frame->header.seq,
                            0u,
                            0u,
                            0,
                            0,
                            0u,
                            0u,
                            frame->payload[1],
                            stopScope,
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

    if (runtime->motionActive != 0u || runtime->jogActive != 0u) {
        execTimeMs = (uint16_t)(nowMs - runtime->motionStartedAtMs);
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_STOPPED,
                                              execTimeMs,
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        runtime->motionActive = 0u;
        runtime->jogActive = 0u;
    }

    if (stopScope == 0u) {
        return CoprocRuntime_StartNextQueuedMotion(runtime, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    return COPROC_STATUS_OK;
}

static uint8_t CoprocRuntime_SetSideLedCount(CoprocRuntime *runtime, uint8_t activeCount)
{
    uint16_t maxCount;

    if (runtime == NULL || runtime->config.setLedCountFn == NULL) {
        return 1u;
    }

    if (activeCount == 0u) {
        return 0u;
    }

    if (runtime->config.getLedMaxCountFn != NULL) {
        maxCount = runtime->config.getLedMaxCountFn(runtime->config.getLedMaxCountCtx);
        if (maxCount == 0u || (uint16_t)activeCount > maxCount) {
            return 0u;
        }
    }

    return runtime->config.setLedCountFn(runtime->config.setLedCountCtx, activeCount);
}

static uint8_t CoprocRuntime_SetBottomLedCountToMax(CoprocRuntime *runtime)
{
    uint16_t maxCount;

    if (runtime == NULL || runtime->config.setBottomLedCountFn == NULL) {
        return 1u;
    }

    if (runtime->config.getBottomLedMaxCountFn == NULL) {
        return 1u;
    }

    maxCount = runtime->config.getBottomLedMaxCountFn(runtime->config.getBottomLedMaxCountCtx);
    if (maxCount == 0u) {
        return 0u;
    }

    return runtime->config.setBottomLedCountFn(runtime->config.setBottomLedCountCtx, maxCount);
}

static uint8_t CoprocRuntime_LedTargetIncludesSide(uint8_t target)
{
    return (target == COPROC_RUNTIME_LED_TARGET_ALL || target == COPROC_RUNTIME_LED_TARGET_SIDE) ? 1u : 0u;
}

static uint8_t CoprocRuntime_LedTargetIncludesBottom(uint8_t target)
{
    return (target == COPROC_RUNTIME_LED_TARGET_ALL || target == COPROC_RUNTIME_LED_TARGET_BOTTOM) ? 1u : 0u;
}

static uint8_t CoprocRuntime_IsValidLedTarget(uint8_t target)
{
    return (target == COPROC_RUNTIME_LED_TARGET_ALL || target == COPROC_RUNTIME_LED_TARGET_SIDE ||
            target == COPROC_RUNTIME_LED_TARGET_BOTTOM)
               ? 1u
               : 0u;
}

static uint8_t CoprocRuntime_SetLedRgbTargets(CoprocRuntime *runtime,
                                              uint8_t target,
                                              uint8_t activeCount,
                                              uint8_t red,
                                              uint8_t green,
                                              uint8_t blue)
{
    if (runtime == NULL || CoprocRuntime_IsValidLedTarget(target) == 0u) {
        return 0u;
    }

    if (CoprocRuntime_LedTargetIncludesSide(target) != 0u) {
        if (runtime->config.setLedRgbFn == NULL ||
            CoprocRuntime_SetSideLedCount(runtime, activeCount) == 0u ||
            runtime->config.setLedRgbFn(runtime->config.setLedRgbCtx, red, green, blue) == 0u) {
            return 0u;
        }
    }

    if (CoprocRuntime_LedTargetIncludesBottom(target) != 0u) {
        if (runtime->config.setBottomLedRgbFn == NULL ||
            CoprocRuntime_SetBottomLedCountToMax(runtime) == 0u ||
            runtime->config.setBottomLedRgbFn(runtime->config.setBottomLedRgbCtx, red, green, blue) == 0u) {
            return 0u;
        }
    }

    return 1u;
}

static uint8_t CoprocRuntime_StartLedBreatheTargets(CoprocRuntime *runtime,
                                                    uint8_t target,
                                                    uint8_t red,
                                                    uint8_t green,
                                                    uint8_t blue,
                                                    uint8_t step,
                                                    uint8_t intervalMs)
{
    if (runtime == NULL || CoprocRuntime_IsValidLedTarget(target) == 0u) {
        return 0u;
    }

    if (CoprocRuntime_LedTargetIncludesSide(target) != 0u) {
        if (runtime->config.startLedBreatheFn == NULL ||
            runtime->config.startLedBreatheFn(runtime->config.startLedBreatheCtx,
                                             red,
                                             green,
                                             blue,
                                             step,
                                             intervalMs) == 0u) {
            return 0u;
        }
    }

    if (CoprocRuntime_LedTargetIncludesBottom(target) != 0u) {
        if (runtime->config.startBottomLedBreatheFn == NULL ||
            runtime->config.startBottomLedBreatheFn(runtime->config.startBottomLedBreatheCtx,
                                                   red,
                                                   green,
                                                   blue,
                                                   step,
                                                   intervalMs) == 0u) {
            return 0u;
        }
    }

    return 1u;
}

static uint8_t CoprocRuntime_StopLedTargets(CoprocRuntime *runtime, uint8_t target)
{
    if (runtime == NULL || CoprocRuntime_IsValidLedTarget(target) == 0u) {
        return 0u;
    }

    if (CoprocRuntime_LedTargetIncludesSide(target) != 0u) {
        if (runtime->config.stopLedEffectFn != NULL) {
            runtime->config.stopLedEffectFn(runtime->config.stopLedEffectCtx);
        }
        if (runtime->config.setLedRgbFn == NULL ||
            runtime->config.setLedRgbFn(runtime->config.setLedRgbCtx, 0u, 0u, 0u) == 0u) {
            return 0u;
        }
    }

    if (CoprocRuntime_LedTargetIncludesBottom(target) != 0u) {
        if (runtime->config.stopBottomLedEffectFn != NULL) {
            runtime->config.stopBottomLedEffectFn(runtime->config.stopBottomLedEffectCtx);
        }
        if (runtime->config.setBottomLedRgbFn == NULL ||
            runtime->config.setBottomLedRgbFn(runtime->config.setBottomLedRgbCtx, 0u, 0u, 0u) == 0u) {
            return 0u;
        }
    }

    return 1u;
}

static CoprocStatus CoprocRuntime_HandleLedSetRgb(CoprocRuntime *runtime,
                                                  const CoprocFrame *frame,
                                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                                  void *allocSeqCtx,
                                                  CoprocDispatchTxWriteFn txWriteFn,
                                                  void *txWriteCtx,
                                                  CoprocDispatchEventFn eventFn,
                                                  void *eventCtx)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t activeCount;
    uint8_t target;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }
    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_LED_SET_RGB) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (runtime->config.setLedRgbFn == NULL) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_CAPABILITY_MISSING, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    red = frame->payload[0];
    green = frame->payload[1];
    blue = frame->payload[2];
    activeCount = frame->payload[3];
    target = frame->payload[4];
    if (CoprocRuntime_SetLedRgbTargets(runtime, target, activeCount, red, green, blue) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_OUT_OF_RANGE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    runtime->stats.ledCommandRxCount++;
    CoprocRuntime_EmitLedEvent(eventFn,
                               eventCtx,
                               COPROC_DISPATCH_EVENT_LED_SET_RGB,
                               &frame->header,
                               frame->header.seq,
                               red,
                               green,
                               blue,
                               activeCount,
                               0u,
                               0u,
                               COPROC_LED_RESULT_SUCCESS,
                               target);
    return CoprocRuntime_SendLedDone(runtime, frame, COPROC_LED_RESULT_SUCCESS, allocSeqFn, allocSeqCtx, txWriteFn,
                                     txWriteCtx, eventFn, eventCtx);
}

static CoprocStatus CoprocRuntime_HandleLedBreathe(CoprocRuntime *runtime,
                                                   const CoprocFrame *frame,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   CoprocDispatchEventFn eventFn,
                                                   void *eventCtx)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t step;
    uint8_t intervalMs;
    uint8_t target;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }
    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_LED_BREATHE) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (runtime->config.startLedBreatheFn == NULL) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_CAPABILITY_MISSING, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    red = frame->payload[0];
    green = frame->payload[1];
    blue = frame->payload[2];
    step = frame->payload[3];
    intervalMs = frame->payload[4];
    target = frame->payload[5];
    if (step == 0u || intervalMs == 0u ||
        CoprocRuntime_StartLedBreatheTargets(runtime, target, red, green, blue, step, intervalMs) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    runtime->stats.ledCommandRxCount++;
    CoprocRuntime_EmitLedEvent(eventFn,
                               eventCtx,
                               COPROC_DISPATCH_EVENT_LED_BREATHE,
                               &frame->header,
                               frame->header.seq,
                               red,
                               green,
                               blue,
                               0u,
                               step,
                               intervalMs,
                               COPROC_LED_RESULT_SUCCESS,
                               target);
    return CoprocRuntime_SendLedDone(runtime, frame, COPROC_LED_RESULT_SUCCESS, allocSeqFn, allocSeqCtx, txWriteFn,
                                     txWriteCtx, eventFn, eventCtx);
}

static CoprocStatus CoprocRuntime_HandleLedOff(CoprocRuntime *runtime,
                                               const CoprocFrame *frame,
                                               CoprocDispatchAllocSeqFn allocSeqFn,
                                               void *allocSeqCtx,
                                               CoprocDispatchTxWriteFn txWriteFn,
                                               void *txWriteCtx,
                                               CoprocDispatchEventFn eventFn,
                                               void *eventCtx)
{
    CoprocStatus status;
    uint8_t target = COPROC_RUNTIME_LED_TARGET_ALL;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }
    if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_LED_OFF && frame->header.payloadLength != 1u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
    if (frame->header.payloadLength == 1u) {
        target = frame->payload[0];
    }
    if (CoprocRuntime_IsValidLedTarget(target) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_CAPABILITY_MISSING, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    if (CoprocRuntime_StopLedTargets(runtime, target) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INTERNAL_ERROR, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        return status;
    }
    runtime->stats.ledCommandRxCount++;
    CoprocRuntime_EmitLedEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_LED_OFF, &frame->header, frame->header.seq,
                               0u, 0u, 0u, 0u, 0u, 0u, COPROC_LED_RESULT_SUCCESS, target);
    return CoprocRuntime_SendLedDone(runtime, frame, COPROC_LED_RESULT_SUCCESS, allocSeqFn, allocSeqCtx, txWriteFn,
                                     txWriteCtx, eventFn, eventCtx);
}

static CoprocStatus CoprocRuntime_HandleLedCommand(CoprocRuntime *runtime,
                                                   const CoprocFrame *frame,
                                                   CoprocDispatchAllocSeqFn allocSeqFn,
                                                   void *allocSeqCtx,
                                                   CoprocDispatchTxWriteFn txWriteFn,
                                                   void *txWriteCtx,
                                                   CoprocDispatchEventFn eventFn,
                                                   void *eventCtx)
{
    if (frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    switch (frame->header.msgId) {
        case COPROC_LED_MSG_SET_RGB:
            return CoprocRuntime_HandleLedSetRgb(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                                 eventFn, eventCtx);
        case COPROC_LED_MSG_BREATHE:
            return CoprocRuntime_HandleLedBreathe(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                                 eventFn, eventCtx);
        case COPROC_LED_MSG_OFF:
            return CoprocRuntime_HandleLedOff(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                             eventFn, eventCtx);
        default:
            return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_UNSUPPORTED_MSG, allocSeqFn, allocSeqCtx,
                                          txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
}

static CoprocStatus CoprocRuntime_HandlePowerCommand(CoprocRuntime *runtime,
                                                     const CoprocFrame *frame,
                                                     CoprocDispatchAllocSeqFn allocSeqFn,
                                                     void *allocSeqCtx,
                                                     CoprocDispatchTxWriteFn txWriteFn,
                                                     void *txWriteCtx,
                                                     CoprocDispatchEventFn eventFn,
                                                     void *eventCtx)
{
    uint8_t enabled;
    uint8_t sourceTag;
    CoprocStatus status;

    if (runtime == NULL || frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    switch (frame->header.msgId) {
        case COPROC_POWER_MSG_5V_ENABLE:
            if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_POWER_5V_ENABLE) {
                return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                              txWriteFn, txWriteCtx, eventFn, eventCtx);
            }
            enabled = 1u;
            break;
        case COPROC_POWER_MSG_5V_DISABLE:
            if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_POWER_5V_DISABLE) {
                return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                              txWriteFn, txWriteCtx, eventFn, eventCtx);
            }
            enabled = 0u;
            break;
        default:
            return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_UNSUPPORTED_MSG, allocSeqFn, allocSeqCtx,
                                          txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    if (runtime->config.setPower5VFn == NULL) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_CAPABILITY_MISSING, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    sourceTag = frame->payload[0];
    if (runtime->config.setPower5VFn(runtime->config.setPower5VCtx, enabled, sourceTag) == 0u) {
        return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_INTERNAL_ERROR, allocSeqFn, allocSeqCtx,
                                      txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    status = CoprocRuntime_SendAck(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    runtime->stats.powerCommandRxCount++;
    CoprocRuntime_EmitEvent(eventFn,
                            eventCtx,
                            (enabled != 0u) ? COPROC_DISPATCH_EVENT_POWER_5V_ENABLE : COPROC_DISPATCH_EVENT_POWER_5V_DISABLE,
                            &frame->header,
                            frame->header.seq,
                            0u,
                            0u,
                            0,
                            0,
                            0u,
                            0u,
                            sourceTag,
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
    return COPROC_STATUS_OK;
}

void CoprocRuntime_Init(CoprocRuntime *runtime, const CoprocRuntimeConfig *config)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    if (config != NULL) {
        runtime->config = *config;
    }
    if (runtime->config.touchDebounceMs == 0u) {
        runtime->config.touchDebounceMs = COPROC_RUNTIME_DEFAULT_TOUCH_DEBOUNCE_MS;
    }
    runtime->motionStartXDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionStartYDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionFinalXDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionFinalYDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionProfile = COPROC_MOTION_PROFILE_LINEAR;
}

void CoprocRuntime_Reset(CoprocRuntime *runtime)
{
    CoprocRuntimeConfig config;

    if (runtime == NULL) {
        return;
    }

    config = runtime->config;
    memset(runtime, 0, sizeof(*runtime));
    runtime->config = config;
    runtime->motionStartXDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionStartYDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionFinalXDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionFinalYDegX10 = COPROC_RUNTIME_DEFAULT_SERVO_DEG_X10;
    runtime->motionProfile = COPROC_MOTION_PROFILE_LINEAR;
}

CoprocStatus CoprocRuntime_ProcessFrame(const CoprocFrame *frame,
                                        CoprocDispatchAllocSeqFn allocSeqFn,
                                        void *allocSeqCtx,
                                        CoprocDispatchTxWriteFn txWriteFn,
                                        void *txWriteCtx,
                                        CoprocDispatchEventFn eventFn,
                                        void *eventCtx,
                                        void *extensionCtx)
{
    CoprocRuntime *runtime = (CoprocRuntime *)extensionCtx;

    if (frame == NULL || runtime == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (frame->header.msgClass == COPROC_MSG_CLASS_MOTION) {
        switch (frame->header.msgId) {
            case COPROC_MOTION_MSG_SERVO_MOVE:
                return CoprocRuntime_HandleServoMove(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                                     eventFn, eventCtx);
            case COPROC_MOTION_MSG_SERVO_JOG:
                return CoprocRuntime_HandleServoJog(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                                    eventFn, eventCtx);
            case COPROC_MOTION_MSG_SERVO_STOP:
                return CoprocRuntime_HandleServoStop(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                                     eventFn, eventCtx);
            default:
                return CoprocRuntime_SendNack(runtime, frame, COPROC_NACK_REASON_UNSUPPORTED_MSG, allocSeqFn, allocSeqCtx,
                                              txWriteFn, txWriteCtx, eventFn, eventCtx);
        }
    }

    if (frame->header.msgClass == COPROC_MSG_CLASS_POWER) {
        return CoprocRuntime_HandlePowerCommand(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                               eventFn, eventCtx);
    }

    if (frame->header.msgClass == COPROC_MSG_CLASS_LED) {
        return CoprocRuntime_HandleLedCommand(runtime, frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx,
                                             eventFn, eventCtx);
    }

    return COPROC_STATUS_UNSUPPORTED;
}

CoprocStatus CoprocRuntime_Poll(CoprocRuntime *runtime,
                                CoprocDispatchAllocSeqFn allocSeqFn,
                                void *allocSeqCtx,
                                CoprocDispatchTxWriteFn txWriteFn,
                                void *txWriteCtx,
                                CoprocDispatchEventFn eventFn,
                                void *eventCtx)
{
    CoprocStatus status;
    uint32_t nowMs;
    CoprocRuntimeTouchEvent touchEvent;

    if (runtime == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    nowMs = CoprocRuntime_NowMs(runtime);
    if (runtime->jogActive != 0u && CoprocRuntime_ApplyJogAt(runtime, nowMs) == 0u) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_FAULT,
                                              (uint16_t)(nowMs - runtime->motionStartedAtMs),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        runtime->jogActive = 0u;
        return status;
    }
    if (runtime->jogActive != 0u && nowMs >= runtime->jogTimeoutAtMs) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_STOPPED,
                                              (uint16_t)(nowMs - runtime->motionStartedAtMs),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        runtime->jogActive = 0u;
        if (status != COPROC_STATUS_OK) {
            return status;
        }
    }
    if (runtime->motionActive != 0u && CoprocRuntime_ApplyMotionAt(runtime, nowMs) == 0u) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_FAULT,
                                              (uint16_t)(nowMs - runtime->motionStartedAtMs),
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        runtime->motionActive = 0u;
        return status;
    }
    if (runtime->motionActive != 0u && nowMs >= runtime->motionCompleteAtMs) {
        status = CoprocRuntime_SendMotionDone(runtime,
                                              COPROC_MOTION_RESULT_SUCCESS,
                                              runtime->motionDurationMs,
                                              allocSeqFn,
                                              allocSeqCtx,
                                              txWriteFn,
                                              txWriteCtx,
                                              eventFn,
                                              eventCtx);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        runtime->motionActive = 0u;
        status = CoprocRuntime_StartNextQueuedMotion(runtime, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
    }

    if (runtime->config.readTouchFn != NULL) {
        uint8_t rawState = (runtime->config.readTouchFn(runtime->config.readTouchCtx) != 0u) ? 1u : 0u;

        if (runtime->touchInitialized == 0u) {
            runtime->touchInitialized = 1u;
            runtime->touchStableState = rawState;
            runtime->touchCandidateState = rawState;
            runtime->touchCandidateSinceMs = nowMs;
        } else if (rawState != runtime->touchCandidateState) {
            runtime->touchCandidateState = rawState;
            runtime->touchCandidateSinceMs = nowMs;
        } else if (runtime->touchCandidateState != runtime->touchStableState &&
                   (nowMs - runtime->touchCandidateSinceMs) >= runtime->config.touchDebounceMs) {
            runtime->touchStableState = runtime->touchCandidateState;
            (void)CoprocRuntime_EnqueueTouchEvent(runtime,
                                                  (runtime->touchStableState != 0u) ? COPROC_TOUCH_EVENT_PRESS
                                                                                   : COPROC_TOUCH_EVENT_RELEASE,
                                                  nowMs);
        }
    }

    while (CoprocRuntime_DequeueTouchEvent(runtime, &touchEvent) != 0u) {
        CoprocTxMessage message;

        if (allocSeqFn == NULL) {
            return COPROC_STATUS_INVALID_ARG;
        }
        status = CoprocTxBuilder_BuildTouchEvent(allocSeqFn(allocSeqCtx), 0u, touchEvent.code, touchEvent.timestampMs, &message);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        status = CoprocRuntime_SendMessage(runtime, &message, txWriteFn, txWriteCtx);
        if (status != COPROC_STATUS_OK) {
            return status;
        }
        runtime->stats.touchEventTxCount++;
        CoprocRuntime_EmitEvent(eventFn,
                                eventCtx,
                                COPROC_DISPATCH_EVENT_TOUCH_EVENT,
                                &message.frame.header,
                                0u,
                                0u,
                                0u,
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
                                0u,
                                touchEvent.code,
                                touchEvent.timestampMs);
    }

    return COPROC_STATUS_OK;
}

const CoprocRuntimeStats *CoprocRuntime_GetStats(const CoprocRuntime *runtime)
{
    return (runtime != NULL) ? &runtime->stats : NULL;
}
