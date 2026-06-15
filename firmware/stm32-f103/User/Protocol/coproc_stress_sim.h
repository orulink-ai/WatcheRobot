#ifndef USER_PROTOCOL_COPROC_STRESS_SIM_H
#define USER_PROTOCOL_COPROC_STRESS_SIM_H

#include "coproc_dispatch.h"

typedef uint32_t (*CoprocStressSimNowFn)(void *ctx);

typedef struct
{
    uint32_t servoMoveRxCount;
    uint32_t servoStopRxCount;
    uint32_t motionDoneTxCount;
    uint32_t touchTxCount;
    uint32_t magTxCount;
} CoprocStressSimStats;

typedef struct
{
    uint8_t active;
    uint32_t refSeq;
    int16_t xDegX10;
    int16_t yDegX10;
    uint16_t durationMs;
    uint32_t startedAtMs;
    uint32_t completeAtMs;
} CoprocStressSimMotionState;

typedef struct
{
    CoprocStressSimNowFn nowFn;
    void *nowCtx;
    CoprocStressSimStats stats;
    CoprocStressSimMotionState motion;
    uint32_t nextMagAtMs;
    uint32_t nextTouchPressAtMs;
    uint32_t nextTouchReleaseAtMs;
    uint8_t touchActive;
    uint8_t streamingEnabled;
    uint16_t magSampleIndex;
} CoprocStressSim;

void CoprocStressSim_Init(CoprocStressSim *sim, CoprocStressSimNowFn nowFn, void *nowCtx);
void CoprocStressSim_Reset(CoprocStressSim *sim);
CoprocStatus CoprocStressSim_ProcessFrame(const CoprocFrame *frame,
                                          CoprocDispatchAllocSeqFn allocSeqFn,
                                          void *allocSeqCtx,
                                          CoprocDispatchTxWriteFn txWriteFn,
                                          void *txWriteCtx,
                                          CoprocDispatchEventFn eventFn,
                                          void *eventCtx,
                                          void *extensionCtx);
CoprocStatus CoprocStressSim_Poll(CoprocStressSim *sim,
                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                  void *allocSeqCtx,
                                  CoprocDispatchTxWriteFn txWriteFn,
                                  void *txWriteCtx,
                                  CoprocDispatchEventFn eventFn,
                                  void *eventCtx);
const CoprocStressSimStats *CoprocStressSim_GetStats(const CoprocStressSim *sim);

#endif /* USER_PROTOCOL_COPROC_STRESS_SIM_H */
