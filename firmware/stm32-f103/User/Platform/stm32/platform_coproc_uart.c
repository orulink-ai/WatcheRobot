#include "platform_coproc_uart.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#if !defined(WATCHER_STRESS_BUILD)
#include "app.h"
#endif
#include "coproc_protocol.h"
#include "coproc_runtime.h"
#if defined(WATCHER_STRESS_BUILD)
#include "coproc_stress_sim.h"
#endif
#include "platform_time.h"
#include "platform_uart.h"
#if !defined(WATCHER_STRESS_BUILD) && (APP_TOUCH_ENABLE != 0U)
#include "touch_sensor.h"
#endif
#include "usart.h"

typedef struct
{
    volatile uint8_t rxQueueOverflowPending;
    volatile uint8_t bootPending;
    volatile uint8_t armPending;
    volatile uint8_t rxPending;
    volatile uint8_t uartErrorPending;
    volatile uint8_t rearmFailedPending;
    volatile uint16_t lastSize;
    volatile uint32_t rxEventCount;
    volatile uint32_t uartErrorCode;
    volatile uint32_t dmaRearmErrorCount;
    volatile uint32_t rxQueueOverflowCount;
    uint32_t lastStatsLogTickMs;
    uint32_t lastLoggedRxEventCount;
    uint32_t lastLoggedDmaRearmErrorCount;
    uint32_t lastLoggedRxQueueOverflowCount;
    uint8_t preview[APP_UART2_RX_PREVIEW_BYTES];
    uint8_t previewLen;
    uint8_t previewHasNonZero;
    CoprocProtocolStats lastLoggedStats;
    CoprocProtocol protocol;
#if !defined(WATCHER_STRESS_BUILD)
    CoprocRuntime runtime;
#endif
#if defined(WATCHER_STRESS_BUILD)
    CoprocStressSim stressSim;
    CoprocStressSimStats lastLoggedStressStats;
    uint32_t lastStressStatsLogTickMs;
#endif
} Platform_CoprocUart_StateTypeDef;

#define PLATFORM_COPROC_UART_RX_QUEUE_DEPTH 4U

typedef struct
{
    uint16_t length;
    uint8_t data[APP_UART2_RX_BUFFER_SIZE];
} Platform_CoprocUart_RxChunkTypeDef;

static uint8_t s_uart2RxBuffer[APP_UART2_RX_BUFFER_SIZE];
static uint8_t s_uart2ProcessBuffer[APP_UART2_RX_BUFFER_SIZE];
static Platform_CoprocUart_RxChunkTypeDef s_uart2RxQueue[PLATFORM_COPROC_UART_RX_QUEUE_DEPTH];
static volatile uint8_t s_uart2RxQueueHead;
static volatile uint8_t s_uart2RxQueueTail;
static Platform_CoprocUart_StateTypeDef s_uart2State;
static void Platform_CoprocUart_LogProtocolObs(void *ctx, const CoprocProtocolObsEvent *event);

#if !defined(WATCHER_STRESS_BUILD)
static uint32_t Platform_CoprocUart_AllocSeq(void *ctx)
{
    return CoprocProtocol_AllocNextTxSeq((CoprocProtocol *)ctx);
}

static uint32_t Platform_CoprocUart_NowMs(void *ctx)
{
    (void)ctx;

    return Platform_Time_GetTickMs();
}

#if (APP_TOUCH_ENABLE != 0U)
static uint8_t Platform_CoprocUart_ReadTouch(void *ctx)
{
    (void)ctx;

    return (TouchSensor_Read() == 0U) ? 1U : 0U;
}
#endif

static uint8_t Platform_CoprocUart_SetServoAngle(void *ctx, uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs)
{
    uint8_t ok;

    (void)ctx;

    ok = App_SetServoAngle(servoIndex, angle, pulseUs);
#if (APP_COPROC_SERVO_APPLY_LOG_ENABLE != 0U)
    Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=servo_apply servo=%u angle=%u pulse_us=%u result=%u\r\n",
                         (unsigned long)Platform_Time_GetTickMs(),
                         (unsigned)servoIndex,
                         (unsigned)angle,
                         (pulseUs != NULL) ? (unsigned)*pulseUs : 0U,
                         (unsigned)ok);
#endif
    return ok;
}

static uint8_t Platform_CoprocUart_SetServoDegX10(void *ctx, uint8_t servoIndex, int16_t degX10, uint16_t *pulseUs)
{
    uint8_t ok;

    (void)ctx;

    ok = App_SetServoDegX10(servoIndex, degX10, pulseUs);
    return ok;
}

static uint8_t Platform_CoprocUart_StopServo(void *ctx, uint8_t servoIndex)
{
    (void)ctx;

    return App_StopServoMotion(servoIndex);
}

static uint8_t Platform_CoprocUart_ReleaseServo(void *ctx, uint8_t servoIndex)
{
    (void)ctx;

    return App_ReleaseServo(servoIndex);
}

#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
static uint8_t Platform_CoprocUart_ReadServoFeedback(void *ctx,
                                                     uint8_t servoIndex,
                                                     uint8_t *commandAngle,
                                                     uint16_t *feedbackRaw,
                                                     uint16_t *feedbackMv,
                                                     uint8_t *feedbackAngle)
{
    App_ServoFeedbackSnapshotTypeDef snapshot;

    (void)ctx;
    if (commandAngle == NULL || feedbackRaw == NULL || feedbackMv == NULL || feedbackAngle == NULL ||
        App_GetServoFeedbackSnapshot(servoIndex, &snapshot) == 0U || snapshot.feedbackValid == 0U) {
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
        Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=servo_feedback servo=%u valid=0\r\n",
                             (unsigned long)Platform_Time_GetTickMs(),
                             (unsigned)servoIndex);
#endif
        return 0U;
    }

    *commandAngle = snapshot.commandAngle;
    *feedbackRaw = snapshot.feedbackRaw;
    *feedbackMv = snapshot.feedbackMv;
    *feedbackAngle = snapshot.feedbackAngle;
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
    Platform_Uart_Printf(
        "STM32_OBS tick_ms=%lu evt=servo_feedback servo=%u command_angle=%u feedback_raw=%u feedback_mv=%u feedback_angle=%u valid=1\r\n",
        (unsigned long)Platform_Time_GetTickMs(),
        (unsigned)servoIndex,
        (unsigned)snapshot.commandAngle,
        (unsigned)snapshot.feedbackRaw,
        (unsigned)snapshot.feedbackMv,
        (unsigned)snapshot.feedbackAngle);
#endif
    return 1U;
}
#endif

#if (APP_LED_ENABLE != 0U)
static uint8_t Platform_CoprocUart_SetLedCount(void *ctx, uint16_t ledCount)
{
    (void)ctx;

    return App_SetWs2812LedCount(ledCount);
}

static uint16_t Platform_CoprocUart_GetLedMaxCount(void *ctx)
{
    (void)ctx;

    return App_GetWs2812MaxLedCount();
}

static uint8_t Platform_CoprocUart_SetLedRgb(void *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    (void)ctx;

    return App_SetWs2812Rgb(red, green, blue);
}

static uint8_t Platform_CoprocUart_SetBottomLedCount(void *ctx, uint16_t ledCount)
{
    (void)ctx;

    return App_SetBottomWs2812LedCount(ledCount);
}

static uint16_t Platform_CoprocUart_GetBottomLedMaxCount(void *ctx)
{
    (void)ctx;

    return App_GetBottomWs2812MaxLedCount();
}

static uint8_t Platform_CoprocUart_SetBottomLedRgb(void *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    (void)ctx;

    return App_SetBottomWs2812Rgb(red, green, blue);
}

static uint8_t Platform_CoprocUart_StartLedBreathe(void *ctx,
                                                   uint8_t red,
                                                   uint8_t green,
                                                   uint8_t blue,
                                                   uint8_t step,
                                                   uint8_t intervalMs)
{
    (void)ctx;

    return App_StartWs2812BreatheTimed(red, green, blue, step, intervalMs);
}

static uint8_t Platform_CoprocUart_StartBottomLedBreathe(void *ctx,
                                                         uint8_t red,
                                                         uint8_t green,
                                                         uint8_t blue,
                                                         uint8_t step,
                                                         uint8_t intervalMs)
{
    (void)ctx;

    return App_StartBottomWs2812BreatheTimed(red, green, blue, step, intervalMs);
}

static void Platform_CoprocUart_StopLedEffect(void *ctx)
{
    (void)ctx;

    App_StopWs2812Effect();
}

static void Platform_CoprocUart_StopBottomLedEffect(void *ctx)
{
    (void)ctx;

    App_StopBottomWs2812Effect();
}
#endif

static uint8_t Platform_CoprocUart_SetPower5V(void *ctx, uint8_t enabled, uint8_t sourceTag)
{
    (void)ctx;

    return App_SetPower5V(enabled, sourceTag);
}

static void Platform_CoprocUart_LogRuntimeDispatchEvent(void *ctx, const CoprocDispatchEvent *event)
{
    CoprocProtocolObsEvent obsEvent = {0};

    (void)ctx;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case COPROC_DISPATCH_EVENT_MOTION_DONE:
            obsEvent.type = COPROC_OBS_EVENT_MOTION_DONE;
            break;
        case COPROC_DISPATCH_EVENT_LED_DONE:
            obsEvent.type = COPROC_OBS_EVENT_LED_DONE;
            break;
        case COPROC_DISPATCH_EVENT_TOUCH_EVENT:
            obsEvent.type = COPROC_OBS_EVENT_TOUCH_EVENT;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_ENABLE:
            obsEvent.type = COPROC_OBS_EVENT_POWER_5V_ENABLE;
            break;
        case COPROC_DISPATCH_EVENT_POWER_5V_DISABLE:
            obsEvent.type = COPROC_OBS_EVENT_POWER_5V_DISABLE;
            break;
        default:
            return;
    }

    obsEvent.header = event->header;
    obsEvent.status = COPROC_STATUS_OK;
    obsEvent.refSeq = event->refSeq;
    obsEvent.motionResult = event->motionResult;
    obsEvent.xValue = event->xValue;
    obsEvent.yValue = event->yValue;
    obsEvent.execTimeMs = event->execTimeMs;
    obsEvent.ledResult = event->ledResult;
    obsEvent.touchId = event->touchId;
    obsEvent.touchCode = event->touchCode;
    obsEvent.timestampMs = event->timestampMs;
    obsEvent.sourceTag = event->sourceTag;
    obsEvent.powerEnabled = event->powerEnabled;
#if (APP_COPROC_OBS_LOG_ENABLE == 0U)
    if (obsEvent.type == COPROC_OBS_EVENT_TOUCH_EVENT) {
        Platform_Uart_Printf(
            "STM32_OBS tick_ms=%lu evt=touch_event touch_id=%u code=%u timestamp_ms=%lu\r\n",
            (unsigned long)Platform_Time_GetTickMs(),
            (unsigned)obsEvent.touchId,
            (unsigned)obsEvent.touchCode,
            (unsigned long)obsEvent.timestampMs
        );
    }
#endif
    Platform_CoprocUart_LogProtocolObs(NULL, &obsEvent);
}
#endif

#if defined(WATCHER_STRESS_BUILD)
static uint32_t Platform_CoprocUart_AllocSeq(void *ctx)
{
    return CoprocProtocol_AllocNextTxSeq((CoprocProtocol *)ctx);
}

static uint32_t Platform_CoprocUart_NowMs(void *ctx)
{
    (void)ctx;

    return Platform_Time_GetTickMs();
}

static void Platform_CoprocUart_LogStressDispatchEvent(void *ctx, const CoprocDispatchEvent *event)
{
    CoprocProtocolObsEvent obsEvent = {0};

    (void)ctx;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case COPROC_DISPATCH_EVENT_ACK:
            obsEvent.type = COPROC_OBS_EVENT_ACK;
            break;
        case COPROC_DISPATCH_EVENT_NACK:
            obsEvent.type = COPROC_OBS_EVENT_NACK;
            break;
        case COPROC_DISPATCH_EVENT_MOTION_DONE:
            obsEvent.type = COPROC_OBS_EVENT_MOTION_DONE;
            break;
        case COPROC_DISPATCH_EVENT_TOUCH_EVENT:
            obsEvent.type = COPROC_OBS_EVENT_TOUCH_EVENT;
            break;
        case COPROC_DISPATCH_EVENT_MAG_STATE:
            obsEvent.type = COPROC_OBS_EVENT_MAG_STATE;
            break;
        case COPROC_DISPATCH_EVENT_IMU_STATE:
            obsEvent.type = COPROC_OBS_EVENT_IMU_STATE;
            break;
        default:
            return;
    }

    obsEvent.header = event->header;
    obsEvent.status = COPROC_STATUS_OK;
    obsEvent.refSeq = event->refSeq;
    obsEvent.reasonCode = event->reasonCode;
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
    obsEvent.touchId = event->touchId;
    obsEvent.touchCode = event->touchCode;
    obsEvent.timestampMs = event->timestampMs;
    obsEvent.quality = event->quality;
    obsEvent.statusBits = event->statusBits;
    obsEvent.auxValue = event->auxValue;
    obsEvent.scalarValue = event->scalarValue;
    Platform_CoprocUart_LogProtocolObs(NULL, &obsEvent);
}
#endif

static void Platform_CoprocUart_ClearPendingFlags(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return;
    }

    /* On STM32F1 these macros clear FE/NE/ORE/IDLE via the required SR/DR read sequence. */
    __HAL_UART_CLEAR_PEFLAG(huart);
    __HAL_UART_CLEAR_IDLEFLAG(huart);
}

static uint8_t Platform_CoprocUart_NextRxQueueIndex(uint8_t index)
{
    index++;
    if (index >= PLATFORM_COPROC_UART_RX_QUEUE_DEPTH) {
        index = 0U;
    }

    return index;
}

static uint8_t Platform_CoprocUart_EnqueueRxChunkFromIsr(const uint8_t *data, uint16_t length)
{
    uint8_t head = s_uart2RxQueueHead;
    uint8_t nextHead = Platform_CoprocUart_NextRxQueueIndex(head);

    if (data == NULL || length == 0U || length > APP_UART2_RX_BUFFER_SIZE) {
        return 0U;
    }

    if (nextHead == s_uart2RxQueueTail) {
        s_uart2State.rxQueueOverflowCount++;
        s_uart2State.rxQueueOverflowPending = 1U;
        return 0U;
    }

    memcpy(s_uart2RxQueue[head].data, data, length);
    s_uart2RxQueue[head].length = length;
    s_uart2RxQueueHead = nextHead;
    return 1U;
}

static uint16_t Platform_CoprocUart_DequeueRxChunk(uint8_t *data, size_t dataLength)
{
    uint8_t tail;
    uint16_t length;

    if (data == NULL || dataLength == 0U) {
        return 0U;
    }

    tail = s_uart2RxQueueTail;
    if (tail == s_uart2RxQueueHead) {
        return 0U;
    }

    length = s_uart2RxQueue[tail].length;
    if (length > dataLength) {
        length = (uint16_t)dataLength;
    }

    memcpy(data, s_uart2RxQueue[tail].data, length);
    s_uart2RxQueueTail = Platform_CoprocUart_NextRxQueueIndex(tail);
    return length;
}

static CoprocStatus Platform_CoprocUart_TxWrite(void *ctx, const uint8_t *data, size_t length)
{
    (void)ctx;

    if (data == NULL || length == 0u) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)length, HAL_MAX_DELAY) != HAL_OK) {
        return COPROC_STATUS_INVALID_STATE;
    }

    return COPROC_STATUS_OK;
}

#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
static void Platform_CoprocUart_BuildPreviewHex(char *buffer, size_t bufferSize)
{
    size_t writeIndex = 0U;
    uint8_t index;

    if ((buffer == NULL) || (bufferSize == 0U)) {
        return;
    }

    buffer[0] = '\0';
    for (index = 0U; index < s_uart2State.previewLen; ++index) {
        int written = snprintf(&buffer[writeIndex], bufferSize - writeIndex, "%02X", s_uart2State.preview[index]);
        if ((written <= 0) || ((size_t)written >= (bufferSize - writeIndex))) {
            break;
        }
        writeIndex += (size_t)written;
    }
}
#endif

static void Platform_CoprocUart_LogBasicObs(const char *evt)
{
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
    char previewHex[(APP_UART2_RX_PREVIEW_BYTES * 2U) + 1U];

    Platform_CoprocUart_BuildPreviewHex(previewHex, sizeof(previewHex));
    Platform_Uart_Printf(
        "STM32_OBS tick_ms=%lu evt=%s rx_event_count=%lu last_rx_size=%u preview_hex=%s preview_nonzero=%u dma_rearm_error_count=%lu\r\n",
        (unsigned long)Platform_Time_GetTickMs(),
        (evt != NULL) ? evt : "unknown",
        (unsigned long)s_uart2State.rxEventCount,
        (unsigned)s_uart2State.lastSize,
        (previewHex[0] != '\0') ? previewHex : "-",
        (unsigned)s_uart2State.previewHasNonZero,
        (unsigned long)s_uart2State.dmaRearmErrorCount
    );
#else
    (void)evt;
#endif
}

static void Platform_CoprocUart_LogProtocolObs(void *ctx, const CoprocProtocolObsEvent *event)
{
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
    (void)ctx;

    if (event == NULL) {
        return;
    }

#if defined(WATCHER_STRESS_BUILD)
    switch (event->type) {
        case COPROC_OBS_EVENT_FRAME_CANDIDATE:
        case COPROC_OBS_EVENT_FRAME_DECODE_OK:
        case COPROC_OBS_EVENT_ACK:
        case COPROC_OBS_EVENT_SERVO_MOVE:
        case COPROC_OBS_EVENT_SERVO_STOP:
        case COPROC_OBS_EVENT_MOTION_DONE:
        case COPROC_OBS_EVENT_TOUCH_EVENT:
        case COPROC_OBS_EVENT_LED_SET_RGB:
        case COPROC_OBS_EVENT_LED_BREATHE:
        case COPROC_OBS_EVENT_LED_OFF:
        case COPROC_OBS_EVENT_LED_DONE:
        case COPROC_OBS_EVENT_MAG_STATE:
        case COPROC_OBS_EVENT_IMU_STATE:
        case COPROC_OBS_EVENT_POWER_5V_ENABLE:
        case COPROC_OBS_EVENT_POWER_5V_DISABLE:
            return;
        default:
            break;
    }
#endif

    switch (event->type) {
        case COPROC_OBS_EVENT_FRAME_CANDIDATE:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=frame_candidate candidate_len=%u\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned)event->candidateLength);
            break;
        case COPROC_OBS_EVENT_FRAME_DECODE_OK:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=frame_decode_ok msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength
            );
            break;
        case COPROC_OBS_EVENT_FRAME_DECODE_FAIL:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=frame_decode_fail candidate_len=%u status=%s\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned)event->candidateLength,
                                 CoprocStatus_ToString(event->status));
            break;
        case COPROC_OBS_EVENT_CRC_FAIL:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=crc_fail candidate_len=%u status=%s\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned)event->candidateLength,
                                 CoprocStatus_ToString(event->status));
            break;
        case COPROC_OBS_EVENT_COBS_FAIL:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=cobs_fail candidate_len=%u status=%s\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned)event->candidateLength,
                                 CoprocStatus_ToString(event->status));
            break;
        case COPROC_OBS_EVENT_DISPATCH_FAIL:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=dispatch_fail msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u status=%s\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength,
                CoprocStatus_ToString(event->status)
            );
            break;
        case COPROC_OBS_EVENT_HELLO_REQ:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=hello_req msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength
            );
            break;
        case COPROC_OBS_EVENT_ACK:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=ack msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u ref_seq=%lu\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength,
                (unsigned long)event->refSeq
            );
            break;
        case COPROC_OBS_EVENT_NACK:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=nack msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u ref_seq=%lu reason_code=0x%04X\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength,
                (unsigned long)event->refSeq,
                (unsigned)event->reasonCode
            );
            break;
        case COPROC_OBS_EVENT_FAULT:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=fault msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u ref_seq=%lu fault_source=%u fault_code=0x%04X\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength,
                (unsigned long)event->refSeq,
                (unsigned)event->faultSource,
                (unsigned)event->faultCode
            );
            break;
        case COPROC_OBS_EVENT_HELLO_RSP:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=hello_rsp msg_class=%u msg_id=%u flags=0x%02X seq=%lu payload_len=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->header.msgClass,
                (unsigned)event->header.msgId,
                (unsigned)event->header.flags,
                (unsigned long)event->header.seq,
                (unsigned)event->header.payloadLength
            );
            break;
        case COPROC_OBS_EVENT_SERVO_MOVE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=servo_move seq=%lu axis_mask=0x%02X x_deg_x10=%d y_deg_x10=%d duration_ms=%u motion_profile=%u source_tag=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->axisMask,
                (int)event->xValue,
                (int)event->yValue,
                (unsigned)event->durationMs,
                (unsigned)event->motionProfile,
                (unsigned)event->sourceTag
            );
            break;
        case COPROC_OBS_EVENT_SERVO_STOP:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=servo_stop seq=%lu stop_scope=%u source_tag=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->stopScope,
                (unsigned)event->sourceTag
            );
            break;
        case COPROC_OBS_EVENT_MOTION_DONE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=motion_done seq=%lu result=%u final_x_deg_x10=%d final_y_deg_x10=%d exec_time_ms=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->motionResult,
                (int)event->xValue,
                (int)event->yValue,
                (unsigned)event->execTimeMs
            );
            break;
        case COPROC_OBS_EVENT_LED_SET_RGB:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=led_set_rgb seq=%lu rgb=%u,%u,%u active_count=%u result=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->red,
                (unsigned)event->green,
                (unsigned)event->blue,
                (unsigned)event->activeCount,
                (unsigned)event->ledResult
            );
            break;
        case COPROC_OBS_EVENT_LED_BREATHE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=led_breathe seq=%lu rgb=%u,%u,%u step=%u interval_ms=%u active_count=%u result=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->red,
                (unsigned)event->green,
                (unsigned)event->blue,
                (unsigned)event->ledStep,
                (unsigned)event->ledIntervalMs,
                (unsigned)event->activeCount,
                (unsigned)event->ledResult
            );
            break;
        case COPROC_OBS_EVENT_LED_OFF:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=led_off seq=%lu result=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->ledResult
            );
            break;
        case COPROC_OBS_EVENT_LED_DONE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=led_done seq=%lu result=%u\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned long)event->refSeq,
                (unsigned)event->ledResult
            );
            break;
        case COPROC_OBS_EVENT_TOUCH_EVENT:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=touch_event touch_id=%u code=%u timestamp_ms=%lu\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->touchId,
                (unsigned)event->touchCode,
                (unsigned long)event->timestampMs
            );
            break;
        case COPROC_OBS_EVENT_MAG_STATE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=mag_state heading_deg_x100=%u field_norm_uT=%u quality=%u status=0x%02X\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (unsigned)event->xValue,
                (unsigned)event->scalarValue,
                (unsigned)event->quality,
                (unsigned)event->statusBits
            );
            break;
        case COPROC_OBS_EVENT_IMU_STATE:
            Platform_Uart_Printf(
                "STM32_OBS tick_ms=%lu evt=imu_state roll_deg_x100=%d pitch_deg_x100=%d yaw_deg_x100=%d acc_norm_mg=%u gyro_norm_dps_x10=%u motion_flags=0x%02X\r\n",
                (unsigned long)Platform_Time_GetTickMs(),
                (int)event->xValue,
                (int)event->yValue,
                (int)event->zValue,
                (unsigned)event->auxValue,
                (unsigned)event->scalarValue,
                (unsigned)event->statusBits
            );
            break;
        case COPROC_OBS_EVENT_POWER_5V_ENABLE:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=power_5v_enable seq=%lu source_tag=%u\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned long)event->refSeq,
                                 (unsigned)event->sourceTag);
            break;
        case COPROC_OBS_EVENT_POWER_5V_DISABLE:
            Platform_Uart_Printf("STM32_OBS tick_ms=%lu evt=power_5v_disable seq=%lu source_tag=%u\r\n",
                                 (unsigned long)Platform_Time_GetTickMs(),
                                 (unsigned long)event->refSeq,
                                 (unsigned)event->sourceTag);
            break;
        default:
            break;
    }
#else
    (void)ctx;
    (void)event;
#endif
}

static void Platform_CoprocUart_MaybeLogStats(uint8_t force)
{
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
    uint32_t now = Platform_Time_GetTickMs();
    const CoprocProtocolStats *stats = CoprocProtocol_GetStats(&s_uart2State.protocol);
    char previewHex[(APP_UART2_RX_PREVIEW_BYTES * 2U) + 1U];
    uint8_t countersChanged =
        (force != 0U) ||
        (s_uart2State.rxEventCount != s_uart2State.lastLoggedRxEventCount) ||
        (s_uart2State.dmaRearmErrorCount != s_uart2State.lastLoggedDmaRearmErrorCount) ||
        (s_uart2State.rxQueueOverflowCount != s_uart2State.lastLoggedRxQueueOverflowCount) ||
        (memcmp(stats, &s_uart2State.lastLoggedStats, sizeof(*stats)) != 0);
    uint8_t periodicDue =
        (s_uart2State.lastStatsLogTickMs == 0U) ||
        ((now - s_uart2State.lastStatsLogTickMs) >= APP_COPROC_STATS_PERIOD_MS);

#if defined(WATCHER_STRESS_BUILD)
    countersChanged = force;
#endif

    if ((countersChanged == 0U) && (periodicDue == 0U)) {
        return;
    }

    Platform_CoprocUart_BuildPreviewHex(previewHex, sizeof(previewHex));
    Platform_Uart_Printf(
        "STM32_OBS tick_ms=%lu evt=stats rx_event_count=%lu last_rx_size=%u preview_hex=%s preview_nonzero=%u dma_rearm_error_count=%lu rx_queue_overflow_count=%lu rx_bytes_total=%lu frame_candidate_count=%lu frame_drop_count=%lu ring_overflow_count=%lu frame_decode_ok=%lu frame_decode_fail=%lu crc_fail=%lu cobs_fail=%lu dispatch_fail_count=%lu\r\n",
        (unsigned long)now,
        (unsigned long)s_uart2State.rxEventCount,
        (unsigned)s_uart2State.lastSize,
        (previewHex[0] != '\0') ? previewHex : "-",
        (unsigned)s_uart2State.previewHasNonZero,
        (unsigned long)s_uart2State.dmaRearmErrorCount,
        (unsigned long)s_uart2State.rxQueueOverflowCount,
        (unsigned long)stats->rxBytesTotal,
        (unsigned long)stats->frameCandidateCount,
        (unsigned long)stats->frameDropCount,
        (unsigned long)stats->ringOverflowCount,
        (unsigned long)stats->frameDecodeOk,
        (unsigned long)stats->frameDecodeFail,
        (unsigned long)stats->crcFail,
        (unsigned long)stats->cobsFail,
        (unsigned long)stats->dispatchFailCount
    );
    s_uart2State.lastStatsLogTickMs = now;
    s_uart2State.lastLoggedRxEventCount = s_uart2State.rxEventCount;
    s_uart2State.lastLoggedDmaRearmErrorCount = s_uart2State.dmaRearmErrorCount;
    s_uart2State.lastLoggedRxQueueOverflowCount = s_uart2State.rxQueueOverflowCount;
    s_uart2State.lastLoggedStats = *stats;
#else
    (void)force;
#endif
}

#if defined(WATCHER_STRESS_BUILD)
static void Platform_CoprocUart_MaybeLogStressStats(uint8_t force)
{
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
    uint32_t now = Platform_Time_GetTickMs();
    const CoprocStressSimStats *stats = CoprocStressSim_GetStats(&s_uart2State.stressSim);
    uint8_t periodicDue =
        (s_uart2State.lastStressStatsLogTickMs == 0U) ||
        ((now - s_uart2State.lastStressStatsLogTickMs) >= APP_COPROC_STATS_PERIOD_MS);

    if ((force == 0U) && (periodicDue == 0U)) {
        return;
    }

    Platform_Uart_Printf(
        "STM32_OBS tick_ms=%lu evt=stress_stats servo_move_rx_count=%lu servo_stop_rx_count=%lu motion_done_tx_count=%lu touch_tx_count=%lu mag_tx_count=%lu dispatch_fail_count=%lu\r\n",
        (unsigned long)now,
        (unsigned long)stats->servoMoveRxCount,
        (unsigned long)stats->servoStopRxCount,
        (unsigned long)stats->motionDoneTxCount,
        (unsigned long)stats->touchTxCount,
        (unsigned long)stats->magTxCount,
        (unsigned long)CoprocProtocol_GetStats(&s_uart2State.protocol)->dispatchFailCount
    );
    s_uart2State.lastStressStatsLogTickMs = now;
    s_uart2State.lastLoggedStressStats = *stats;
#else
    (void)force;
#endif
}
#endif

static void Platform_CoprocUart_ArmRx(void)
{
    Platform_CoprocUart_ClearPendingFlags(&huart2);

    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, s_uart2RxBuffer, sizeof(s_uart2RxBuffer)) == HAL_OK) {
        __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
        s_uart2State.armPending = 1U;
    } else {
        s_uart2State.rearmFailedPending = 1U;
        s_uart2State.dmaRearmErrorCount++;
    }
}

void Platform_CoprocUart_Init(void)
{
    memset(&s_uart2State, 0, sizeof(s_uart2State));
    s_uart2RxQueueHead = 0U;
    s_uart2RxQueueTail = 0U;
    CoprocProtocol_Init(&s_uart2State.protocol);
#if defined(WATCHER_STRESS_BUILD)
    CoprocStressSim_Init(&s_uart2State.stressSim, Platform_CoprocUart_NowMs, NULL);
    CoprocProtocol_SetDispatchExtension(&s_uart2State.protocol, CoprocStressSim_ProcessFrame, &s_uart2State.stressSim);
#else
    {
        const CoprocRuntimeConfig runtimeConfig = {
            .nowMsFn = Platform_CoprocUart_NowMs,
#if (APP_TOUCH_ENABLE != 0U)
            .readTouchFn = Platform_CoprocUart_ReadTouch,
#endif
            .setServoAngleFn = Platform_CoprocUart_SetServoAngle,
            .setServoDegX10Fn = Platform_CoprocUart_SetServoDegX10,
            .stopServoFn = Platform_CoprocUart_StopServo,
            .releaseServoFn = Platform_CoprocUart_ReleaseServo,
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
            .readServoFeedbackFn = Platform_CoprocUart_ReadServoFeedback,
#endif
#if (APP_LED_ENABLE != 0U)
            .setLedCountFn = Platform_CoprocUart_SetLedCount,
            .getLedMaxCountFn = Platform_CoprocUart_GetLedMaxCount,
            .setLedRgbFn = Platform_CoprocUart_SetLedRgb,
            .startLedBreatheFn = Platform_CoprocUart_StartLedBreathe,
            .stopLedEffectFn = Platform_CoprocUart_StopLedEffect,
            .setBottomLedCountFn = Platform_CoprocUart_SetBottomLedCount,
            .getBottomLedMaxCountFn = Platform_CoprocUart_GetBottomLedMaxCount,
            .setBottomLedRgbFn = Platform_CoprocUart_SetBottomLedRgb,
            .startBottomLedBreatheFn = Platform_CoprocUart_StartBottomLedBreathe,
            .stopBottomLedEffectFn = Platform_CoprocUart_StopBottomLedEffect,
#endif
            .setPower5VFn = Platform_CoprocUart_SetPower5V,
            .touchDebounceMs = 30U,
            .servoFeedbackStateIntervalMs = APP_COPROC_SERVO_FEEDBACK_STATE_INTERVAL_MS
        };
        CoprocRuntime_Init(&s_uart2State.runtime, &runtimeConfig);
        CoprocProtocol_SetDispatchExtension(&s_uart2State.protocol, CoprocRuntime_ProcessFrame, &s_uart2State.runtime);
    }
#endif
    s_uart2State.bootPending = 1U;
    Platform_CoprocUart_ArmRx();

    Platform_Uart_Print("USART2 bring-up ready: 921600 8N1, RX DMA + IDLE enabled\r\n");
}

void Platform_CoprocUart_Poll(void)
{
    CoprocStatus protocolStatus;
    uint16_t rxChunkLength;
    uint8_t logArmObs = 1U;

    if (s_uart2State.bootPending != 0U) {
        s_uart2State.bootPending = 0U;
        Platform_CoprocUart_LogBasicObs("boot");
    }

    if (s_uart2State.armPending != 0U) {
        s_uart2State.armPending = 0U;
#if defined(WATCHER_STRESS_BUILD)
        if (s_uart2State.rxEventCount != 0U) {
            logArmObs = 0U;
        }
#endif
        if (logArmObs != 0U) {
            Platform_CoprocUart_LogBasicObs("uart2_rx_arm");
        }
    }

    if (s_uart2State.rxPending != 0U) {
#if !defined(WATCHER_STRESS_BUILD) && (APP_COPROC_OBS_LOG_ENABLE != 0U)
        char previewHex[(APP_UART2_RX_PREVIEW_BYTES * 2U) + 1U];
#endif

        s_uart2State.rxPending = 0U;
#if !defined(WATCHER_STRESS_BUILD) && (APP_COPROC_OBS_LOG_ENABLE != 0U)
        Platform_CoprocUart_BuildPreviewHex(previewHex, sizeof(previewHex));
        Platform_Uart_Printf("[USART2] rx_event=%lu size=%u preview=%s%s\r\n",
                             (unsigned long)s_uart2State.rxEventCount,
                             (unsigned)s_uart2State.lastSize,
                             (previewHex[0] != '\0') ? previewHex : "-",
                             (s_uart2State.lastSize > s_uart2State.previewLen) ? "..." : "");
#endif
    }

    while ((rxChunkLength = Platform_CoprocUart_DequeueRxChunk(s_uart2ProcessBuffer, sizeof(s_uart2ProcessBuffer))) != 0U) {
        (void)CoprocProtocol_IngestBytes(&s_uart2State.protocol, s_uart2ProcessBuffer, rxChunkLength);
    }

    protocolStatus = CoprocProtocol_Process(&s_uart2State.protocol,
                                            Platform_CoprocUart_TxWrite,
                                            NULL,
                                            Platform_CoprocUart_LogProtocolObs,
                                            NULL);
#if defined(WATCHER_STRESS_BUILD)
    if ((protocolStatus == COPROC_STATUS_OK) || (protocolStatus == COPROC_STATUS_EMPTY)) {
        (void)CoprocStressSim_Poll(&s_uart2State.stressSim,
                                   Platform_CoprocUart_AllocSeq,
                                   &s_uart2State.protocol,
                                   Platform_CoprocUart_TxWrite,
                                   NULL,
                                   Platform_CoprocUart_LogStressDispatchEvent,
                                   NULL);
    }
#else
    if ((protocolStatus == COPROC_STATUS_OK) || (protocolStatus == COPROC_STATUS_EMPTY)) {
        (void)CoprocRuntime_Poll(&s_uart2State.runtime,
                                 Platform_CoprocUart_AllocSeq,
                                 &s_uart2State.protocol,
                                 Platform_CoprocUart_TxWrite,
                                 NULL,
                                 Platform_CoprocUart_LogRuntimeDispatchEvent,
                                 NULL);
    }
    (void)protocolStatus;
#endif

    if (s_uart2State.uartErrorPending != 0U) {
        s_uart2State.uartErrorPending = 0U;
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
        Platform_Uart_Printf("USART2 bring-up: UART error code=0x%08lX\r\n", (unsigned long)s_uart2State.uartErrorCode);
        Platform_CoprocUart_LogBasicObs("uart2_error");
#endif
    }

    if (s_uart2State.rearmFailedPending != 0U) {
        s_uart2State.rearmFailedPending = 0U;
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
        Platform_Uart_Print("USART2 bring-up: RX DMA rearm failed\r\n");
        Platform_CoprocUart_LogBasicObs("uart2_rearm_failed");
#endif
    }

    if (s_uart2State.rxQueueOverflowPending != 0U) {
        s_uart2State.rxQueueOverflowPending = 0U;
#if (APP_COPROC_OBS_LOG_ENABLE != 0U)
        Platform_Uart_Print("USART2 bring-up: RX queue overflow\r\n");
        Platform_CoprocUart_LogBasicObs("uart2_rx_queue_overflow");
#endif
    }

    Platform_CoprocUart_MaybeLogStats(0U);
#if defined(WATCHER_STRESS_BUILD)
    Platform_CoprocUart_MaybeLogStressStats(0U);
#endif
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if ((huart != &huart2) || (size == 0U)) {
        return;
    }

    s_uart2State.lastSize = size;
    s_uart2State.previewLen = (size < APP_UART2_RX_PREVIEW_BYTES) ? (uint8_t)size : APP_UART2_RX_PREVIEW_BYTES;
    memcpy(s_uart2State.preview, s_uart2RxBuffer, s_uart2State.previewLen);
    s_uart2State.previewHasNonZero = 0U;
    for (uint8_t index = 0U; index < s_uart2State.previewLen; ++index) {
        if (s_uart2State.preview[index] != 0U) {
            s_uart2State.previewHasNonZero = 1U;
            break;
        }
    }
    s_uart2State.rxEventCount++;
    s_uart2State.rxPending = 1U;
    (void)Platform_CoprocUart_EnqueueRxChunkFromIsr(s_uart2RxBuffer, size);

    Platform_CoprocUart_ArmRx();
}

void Platform_CoprocUart_HandleError(UART_HandleTypeDef *huart)
{
    if (huart != &huart2) {
        return;
    }

    s_uart2State.uartErrorPending = 1U;
    s_uart2State.uartErrorCode = huart->ErrorCode;
    (void)HAL_UART_DMAStop(huart);
    Platform_CoprocUart_ClearPendingFlags(huart);
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    Platform_CoprocUart_ArmRx();
}
