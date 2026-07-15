#ifndef USER_PROTOCOL_COPROC_PROTOCOL_H
#define USER_PROTOCOL_COPROC_PROTOCOL_H

#include "coproc_dispatch.h"
#include "coproc_framing.h"
#include "coproc_ring_buffer.h"

#define COPROC_RING_BUFFER_SIZE 1024u

typedef struct
{
    uint32_t rxBytesTotal;
    uint32_t frameCandidateCount;
    uint32_t frameDropCount;
    uint32_t ringOverflowCount;
    uint32_t frameDecodeOk;
    uint32_t frameDecodeFail;
    uint32_t crcFail;
    uint32_t cobsFail;
    uint32_t dispatchFailCount;
} CoprocProtocolStats;

typedef enum
{
    COPROC_OBS_EVENT_NONE = 0,
    COPROC_OBS_EVENT_FRAME_CANDIDATE,
    COPROC_OBS_EVENT_FRAME_DECODE_OK,
    COPROC_OBS_EVENT_FRAME_DECODE_FAIL,
    COPROC_OBS_EVENT_CRC_FAIL,
    COPROC_OBS_EVENT_COBS_FAIL,
    COPROC_OBS_EVENT_DISPATCH_FAIL,
    COPROC_OBS_EVENT_HELLO_REQ,
    COPROC_OBS_EVENT_ACK,
    COPROC_OBS_EVENT_NACK,
    COPROC_OBS_EVENT_FAULT,
    COPROC_OBS_EVENT_HELLO_RSP,
    COPROC_OBS_EVENT_SERVO_MOVE,
    COPROC_OBS_EVENT_SERVO_STOP,
    COPROC_OBS_EVENT_MOTION_DONE,
    COPROC_OBS_EVENT_MOTION_STATE,
    COPROC_OBS_EVENT_LED_SET_RGB,
    COPROC_OBS_EVENT_LED_BREATHE,
    COPROC_OBS_EVENT_LED_OFF,
    COPROC_OBS_EVENT_LED_DONE,
    COPROC_OBS_EVENT_TOUCH_EVENT,
    COPROC_OBS_EVENT_MAG_STATE,
    COPROC_OBS_EVENT_IMU_STATE,
    COPROC_OBS_EVENT_POWER_5V_ENABLE,
    COPROC_OBS_EVENT_POWER_5V_DISABLE
} CoprocProtocolObsEventType;

typedef struct
{
    CoprocProtocolObsEventType type;
    CoprocFrameHeader header;
    size_t candidateLength;
    CoprocStatus status;
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
} CoprocProtocolObsEvent;

typedef void (*CoprocProtocolObsFn)(void *ctx, const CoprocProtocolObsEvent *event);
typedef CoprocStatus (*CoprocProtocolTxWriteFn)(void *ctx, const uint8_t *data, size_t length);

typedef struct
{
    CoprocRingBuffer ring;
    CoprocFraming framing;
    CoprocProtocolStats stats;
    uint32_t nextTxSeq;
    CoprocDispatchExtensionFn dispatchExtensionFn;
    void *dispatchExtensionCtx;
    uint8_t ringStorage[COPROC_RING_BUFFER_SIZE];
} CoprocProtocol;

void CoprocProtocol_Init(CoprocProtocol *protocol);
void CoprocProtocol_Reset(CoprocProtocol *protocol);
CoprocStatus CoprocProtocol_IngestBytes(CoprocProtocol *protocol, const uint8_t *data, size_t length);
CoprocStatus CoprocProtocol_Process(CoprocProtocol *protocol,
                                    CoprocProtocolTxWriteFn txWriteFn,
                                    void *txWriteCtx,
                                    CoprocProtocolObsFn obsFn,
                                    void *obsCtx);
const CoprocProtocolStats *CoprocProtocol_GetStats(const CoprocProtocol *protocol);
void CoprocProtocol_SetDispatchExtension(CoprocProtocol *protocol,
                                         CoprocDispatchExtensionFn extensionFn,
                                         void *extensionCtx);
uint32_t CoprocProtocol_AllocNextTxSeq(CoprocProtocol *protocol);

#endif /* USER_PROTOCOL_COPROC_PROTOCOL_H */
