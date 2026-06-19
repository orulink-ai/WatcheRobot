#ifndef USER_PROTOCOL_COPROC_DISPATCH_H
#define USER_PROTOCOL_COPROC_DISPATCH_H

#include "coproc_tx_builder.h"

typedef enum
{
    COPROC_DISPATCH_EVENT_NONE = 0,
    COPROC_DISPATCH_EVENT_HELLO_REQ,
    COPROC_DISPATCH_EVENT_ACK,
    COPROC_DISPATCH_EVENT_NACK,
    COPROC_DISPATCH_EVENT_FAULT,
    COPROC_DISPATCH_EVENT_HELLO_RSP,
    COPROC_DISPATCH_EVENT_SERVO_MOVE,
    COPROC_DISPATCH_EVENT_SERVO_STOP,
    COPROC_DISPATCH_EVENT_MOTION_DONE,
    COPROC_DISPATCH_EVENT_MOTION_STATE,
    COPROC_DISPATCH_EVENT_LED_SET_RGB,
    COPROC_DISPATCH_EVENT_LED_BREATHE,
    COPROC_DISPATCH_EVENT_LED_OFF,
    COPROC_DISPATCH_EVENT_LED_DONE,
    COPROC_DISPATCH_EVENT_TOUCH_EVENT,
    COPROC_DISPATCH_EVENT_MAG_STATE,
    COPROC_DISPATCH_EVENT_IMU_STATE,
    COPROC_DISPATCH_EVENT_POWER_5V_ENABLE,
    COPROC_DISPATCH_EVENT_POWER_5V_DISABLE
} CoprocDispatchEventType;

typedef struct
{
    CoprocDispatchEventType type;
    CoprocFrameHeader header;
    uint32_t refSeq;
    uint16_t reasonCode;
    uint8_t faultSource;
    uint16_t faultCode;
    uint8_t axisMask;
    int16_t xValue;
    int16_t yValue;
    int16_t zValue;
    uint16_t durationMs;
    uint8_t motionProfile;
    uint8_t sourceTag;
    uint8_t stopScope;
    uint8_t motionResult;
    uint16_t execTimeMs;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t activeCount;
    uint8_t ledStep;
    uint8_t ledIntervalMs;
    uint8_t ledResult;
    uint8_t touchId;
    uint8_t touchCode;
    uint32_t timestampMs;
    uint8_t quality;
    uint8_t statusBits;
    uint16_t auxValue;
    uint16_t scalarValue;
    uint8_t powerEnabled;
} CoprocDispatchEvent;

typedef uint32_t (*CoprocDispatchAllocSeqFn)(void *ctx);
typedef CoprocStatus (*CoprocDispatchTxWriteFn)(void *ctx, const uint8_t *data, size_t length);
typedef void (*CoprocDispatchEventFn)(void *ctx, const CoprocDispatchEvent *event);
typedef CoprocStatus (*CoprocDispatchExtensionFn)(const CoprocFrame *frame,
                                                  CoprocDispatchAllocSeqFn allocSeqFn,
                                                  void *allocSeqCtx,
                                                  CoprocDispatchTxWriteFn txWriteFn,
                                                  void *txWriteCtx,
                                                  CoprocDispatchEventFn eventFn,
                                                  void *eventCtx,
                                                  void *extensionCtx);

CoprocStatus CoprocDispatch_ProcessFrame(const CoprocFrame *frame,
                                         CoprocDispatchAllocSeqFn allocSeqFn,
                                         void *allocSeqCtx,
                                         CoprocDispatchTxWriteFn txWriteFn,
                                         void *txWriteCtx,
                                         CoprocDispatchEventFn eventFn,
                                         void *eventCtx,
                                         CoprocDispatchExtensionFn extensionFn,
                                         void *extensionCtx);

#endif /* USER_PROTOCOL_COPROC_DISPATCH_H */
