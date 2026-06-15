#ifndef USER_PROTOCOL_COPROC_RUNTIME_H
#define USER_PROTOCOL_COPROC_RUNTIME_H

#include "coproc_dispatch.h"

typedef uint32_t (*CoprocRuntimeNowMsFn)(void *ctx);
typedef uint8_t (*CoprocRuntimeReadTouchFn)(void *ctx);
typedef uint8_t (*CoprocRuntimeSetServoAngleFn)(void *ctx, uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs);
typedef uint8_t (*CoprocRuntimeStopServoFn)(void *ctx, uint8_t servoIndex);
typedef uint8_t (*CoprocRuntimeReadServoFeedbackFn)(void *ctx,
                                                    uint8_t servoIndex,
                                                    uint8_t *commandAngle,
                                                    uint16_t *feedbackRaw,
                                                    uint16_t *feedbackMv,
                                                    uint8_t *feedbackAngle);
typedef uint8_t (*CoprocRuntimeSetLedCountFn)(void *ctx, uint16_t ledCount);
typedef uint16_t (*CoprocRuntimeGetLedMaxCountFn)(void *ctx);
typedef uint8_t (*CoprocRuntimeSetLedRgbFn)(void *ctx, uint8_t red, uint8_t green, uint8_t blue);
typedef uint8_t (*CoprocRuntimeStartLedBreatheFn)(void *ctx,
                                                  uint8_t red,
                                                  uint8_t green,
                                                  uint8_t blue,
                                                  uint8_t step,
                                                  uint8_t intervalMs);
typedef void (*CoprocRuntimeStopLedEffectFn)(void *ctx);
typedef uint8_t (*CoprocRuntimeSetPower5VFn)(void *ctx, uint8_t enabled, uint8_t sourceTag);

typedef struct
{
    CoprocRuntimeNowMsFn nowMsFn;
    void *nowMsCtx;
    CoprocRuntimeReadTouchFn readTouchFn;
    void *readTouchCtx;
    CoprocRuntimeSetServoAngleFn setServoAngleFn;
    void *setServoAngleCtx;
    CoprocRuntimeStopServoFn stopServoFn;
    void *stopServoCtx;
    CoprocRuntimeReadServoFeedbackFn readServoFeedbackFn;
    void *readServoFeedbackCtx;
    CoprocRuntimeSetLedCountFn setLedCountFn;
    void *setLedCountCtx;
    CoprocRuntimeGetLedMaxCountFn getLedMaxCountFn;
    void *getLedMaxCountCtx;
    CoprocRuntimeSetLedRgbFn setLedRgbFn;
    void *setLedRgbCtx;
    CoprocRuntimeStartLedBreatheFn startLedBreatheFn;
    void *startLedBreatheCtx;
    CoprocRuntimeStopLedEffectFn stopLedEffectFn;
    void *stopLedEffectCtx;
    CoprocRuntimeSetLedCountFn setBottomLedCountFn;
    void *setBottomLedCountCtx;
    CoprocRuntimeGetLedMaxCountFn getBottomLedMaxCountFn;
    void *getBottomLedMaxCountCtx;
    CoprocRuntimeSetLedRgbFn setBottomLedRgbFn;
    void *setBottomLedRgbCtx;
    CoprocRuntimeStartLedBreatheFn startBottomLedBreatheFn;
    void *startBottomLedBreatheCtx;
    CoprocRuntimeStopLedEffectFn stopBottomLedEffectFn;
    void *stopBottomLedEffectCtx;
    CoprocRuntimeSetPower5VFn setPower5VFn;
    void *setPower5VCtx;
    uint32_t touchDebounceMs;
} CoprocRuntimeConfig;

typedef struct
{
    uint32_t servoMoveRxCount;
    uint32_t servoStopRxCount;
    uint32_t motionDoneTxCount;
    uint32_t ledCommandRxCount;
    uint32_t ledDoneTxCount;
    uint32_t touchEventQueuedCount;
    uint32_t touchEventTxCount;
    uint32_t powerCommandRxCount;
    uint32_t eventDropCount;
    uint32_t txFailCount;
    uint32_t feedbackFailCount;
} CoprocRuntimeStats;

typedef struct
{
    uint8_t code;
    uint32_t timestampMs;
} CoprocRuntimeTouchEvent;

#define COPROC_RUNTIME_TOUCH_QUEUE_DEPTH 8u
#define COPROC_RUNTIME_MOTION_QUEUE_DEPTH 4u

typedef struct
{
    uint8_t axisMask;
    uint32_t refSeq;
    int16_t targetXDegX10;
    int16_t targetYDegX10;
    uint16_t durationMs;
    uint8_t motionProfile;
    uint8_t sourceTag;
} CoprocRuntimeMotionCommand;

typedef struct
{
    CoprocRuntimeConfig config;
    CoprocRuntimeStats stats;
    uint8_t motionActive;
    uint8_t motionAxisMask;
    uint32_t motionRefSeq;
    int16_t motionStartXDegX10;
    int16_t motionStartYDegX10;
    int16_t motionFinalXDegX10;
    int16_t motionFinalYDegX10;
    uint16_t motionDurationMs;
    uint8_t motionProfile;
    uint32_t motionStartedAtMs;
    uint32_t motionCompleteAtMs;
    uint8_t jogActive;
    int16_t jogVelocityXDegX10PerSec;
    int16_t jogVelocityYDegX10PerSec;
    uint32_t jogLastUpdateMs;
    uint32_t jogTimeoutAtMs;
    int16_t jogMinXDegX10;
    int16_t jogMaxXDegX10;
    int16_t jogMinYDegX10;
    int16_t jogMaxYDegX10;
    uint8_t touchInitialized;
    uint8_t touchStableState;
    uint8_t touchCandidateState;
    uint32_t touchCandidateSinceMs;
    CoprocRuntimeMotionCommand motionQueue[COPROC_RUNTIME_MOTION_QUEUE_DEPTH];
    uint8_t motionQueueHead;
    uint8_t motionQueueTail;
    CoprocRuntimeTouchEvent touchQueue[COPROC_RUNTIME_TOUCH_QUEUE_DEPTH];
    uint8_t touchQueueHead;
    uint8_t touchQueueTail;
} CoprocRuntime;

void CoprocRuntime_Init(CoprocRuntime *runtime, const CoprocRuntimeConfig *config);
void CoprocRuntime_Reset(CoprocRuntime *runtime);
CoprocStatus CoprocRuntime_ProcessFrame(const CoprocFrame *frame,
                                        CoprocDispatchAllocSeqFn allocSeqFn,
                                        void *allocSeqCtx,
                                        CoprocDispatchTxWriteFn txWriteFn,
                                        void *txWriteCtx,
                                        CoprocDispatchEventFn eventFn,
                                        void *eventCtx,
                                        void *extensionCtx);
CoprocStatus CoprocRuntime_Poll(CoprocRuntime *runtime,
                                CoprocDispatchAllocSeqFn allocSeqFn,
                                void *allocSeqCtx,
                                CoprocDispatchTxWriteFn txWriteFn,
                                void *txWriteCtx,
                                CoprocDispatchEventFn eventFn,
                                void *eventCtx);
const CoprocRuntimeStats *CoprocRuntime_GetStats(const CoprocRuntime *runtime);

#endif /* USER_PROTOCOL_COPROC_RUNTIME_H */
