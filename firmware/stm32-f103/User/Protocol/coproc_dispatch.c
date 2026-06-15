#include "coproc_dispatch.h"

static uint16_t CoprocDispatch_ReadU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8u));
}

static uint32_t CoprocDispatch_ReadU32Le(const uint8_t *src)
{
    return (uint32_t)src[0]
           | ((uint32_t)src[1] << 8u)
           | ((uint32_t)src[2] << 16u)
           | ((uint32_t)src[3] << 24u);
}

static void CoprocDispatch_EmitEvent(CoprocDispatchEventFn eventFn,
                                     void *eventCtx,
                                     CoprocDispatchEventType type,
                                     const CoprocFrame *frame,
                                     uint32_t refSeq,
                                     uint16_t reasonCode,
                                     uint8_t faultSource,
                                     uint16_t faultCode)
{
    CoprocDispatchEvent event;

    if (eventFn == NULL || frame == NULL) {
        return;
    }

    event.type = type;
    event.header = frame->header;
    event.refSeq = refSeq;
    event.reasonCode = reasonCode;
    event.faultSource = faultSource;
    event.faultCode = faultCode;
    eventFn(eventCtx, &event);
}

static CoprocStatus CoprocDispatch_RequirePayloadLength(const CoprocFrame *frame, uint16_t expectedLength)
{
    if (frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    return (frame->header.payloadLength == expectedLength) ? COPROC_STATUS_OK : COPROC_STATUS_INVALID_SIZE;
}

static CoprocStatus CoprocDispatch_SendMessage(const CoprocTxMessage *message,
                                               CoprocDispatchTxWriteFn txWriteFn,
                                               void *txWriteCtx)
{
    if (message == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    return txWriteFn(txWriteCtx, message->wire, message->wireLength);
}

static CoprocStatus CoprocDispatch_SendNack(const CoprocFrame *frame,
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
    uint32_t seq;

    if (frame == NULL || allocSeqFn == NULL || txWriteFn == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    seq = allocSeqFn(allocSeqCtx);
    status = CoprocTxBuilder_BuildNack(seq, frame->header.seq, reasonCode, &message);
    if (status != COPROC_STATUS_OK) {
        return status;
    }

    status = CoprocDispatch_SendMessage(&message, txWriteFn, txWriteCtx);
    if (status == COPROC_STATUS_OK) {
        CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_NACK, &message.frame,
                                 frame->header.seq, reasonCode, 0u, 0u);
    }
    return status;
}

CoprocStatus CoprocDispatch_ProcessFrame(const CoprocFrame *frame,
                                         CoprocDispatchAllocSeqFn allocSeqFn,
                                         void *allocSeqCtx,
                                         CoprocDispatchTxWriteFn txWriteFn,
                                         void *txWriteCtx,
                                         CoprocDispatchEventFn eventFn,
                                         void *eventCtx,
                                         CoprocDispatchExtensionFn extensionFn,
                                         void *extensionCtx)
{
    CoprocStatus status;

    if (frame == NULL) {
        return COPROC_STATUS_INVALID_ARG;
    }

    if (frame->header.msgClass != COPROC_MSG_CLASS_SYS) {
        if (extensionFn != NULL) {
            status = extensionFn(frame, allocSeqFn, allocSeqCtx, txWriteFn, txWriteCtx, eventFn, eventCtx, extensionCtx);
            if (status != COPROC_STATUS_UNSUPPORTED) {
                return status;
            }
        }

        return CoprocDispatch_SendNack(frame, COPROC_NACK_REASON_UNSUPPORTED_MSG, allocSeqFn, allocSeqCtx,
                                       txWriteFn, txWriteCtx, eventFn, eventCtx);
    }

    switch (frame->header.msgId) {
        case COPROC_SYS_MSG_HELLO_REQ: {
            CoprocTxMessage ackMessage;
            CoprocTxMessage helloRspMessage;
            CoprocStatus txStatus;
            uint32_t txSeq;

            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_HELLO_REQ, frame, 0u, 0u, 0u, 0u);

            if (frame->header.payloadLength != COPROC_PAYLOAD_LEN_HELLO_REQ) {
                return CoprocDispatch_SendNack(frame, COPROC_NACK_REASON_INVALID_PAYLOAD, allocSeqFn, allocSeqCtx,
                                               txWriteFn, txWriteCtx, eventFn, eventCtx);
            }

            if ((frame->header.flags & COPROC_FRAME_FLAG_ACK_REQ) == 0u) {
                return CoprocDispatch_SendNack(frame, COPROC_NACK_REASON_INVALID_STATE, allocSeqFn, allocSeqCtx,
                                               txWriteFn, txWriteCtx, eventFn, eventCtx);
            }

            if (allocSeqFn == NULL || txWriteFn == NULL) {
                return COPROC_STATUS_INVALID_ARG;
            }

            txSeq = allocSeqFn(allocSeqCtx);
            txStatus = CoprocTxBuilder_BuildAck(txSeq, frame->header.seq, &ackMessage);
            if (txStatus != COPROC_STATUS_OK) {
                return txStatus;
            }

            txStatus = CoprocDispatch_SendMessage(&ackMessage, txWriteFn, txWriteCtx);
            if (txStatus != COPROC_STATUS_OK) {
                return txStatus;
            }
            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_ACK, &ackMessage.frame,
                                     frame->header.seq, 0u, 0u, 0u);

            txSeq = allocSeqFn(allocSeqCtx);
            txStatus = CoprocTxBuilder_BuildHelloRsp(txSeq, &helloRspMessage);
            if (txStatus != COPROC_STATUS_OK) {
                return txStatus;
            }

            txStatus = CoprocDispatch_SendMessage(&helloRspMessage, txWriteFn, txWriteCtx);
            if (txStatus == COPROC_STATUS_OK) {
                CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_HELLO_RSP, &helloRspMessage.frame,
                                         0u, 0u, 0u, 0u);
            }
            return txStatus;
        }
        case COPROC_SYS_MSG_ACK:
            if (CoprocDispatch_RequirePayloadLength(frame, COPROC_PAYLOAD_LEN_ACK) != COPROC_STATUS_OK) {
                return COPROC_STATUS_INVALID_SIZE;
            }
            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_ACK, frame,
                                     CoprocDispatch_ReadU32Le(frame->payload),
                                     0u, 0u, 0u);
            return COPROC_STATUS_OK;
        case COPROC_SYS_MSG_NACK:
            if (CoprocDispatch_RequirePayloadLength(frame, COPROC_PAYLOAD_LEN_NACK) != COPROC_STATUS_OK) {
                return COPROC_STATUS_INVALID_SIZE;
            }
            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_NACK, frame,
                                     CoprocDispatch_ReadU32Le(frame->payload),
                                     CoprocDispatch_ReadU16Le(&frame->payload[6]),
                                     0u, 0u);
            return COPROC_STATUS_OK;
        case COPROC_SYS_MSG_FAULT:
            if (CoprocDispatch_RequirePayloadLength(frame, COPROC_PAYLOAD_LEN_FAULT) != COPROC_STATUS_OK) {
                return COPROC_STATUS_INVALID_SIZE;
            }
            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_FAULT, frame,
                                     CoprocDispatch_ReadU32Le(frame->payload),
                                     0u,
                                     frame->payload[4],
                                     CoprocDispatch_ReadU16Le(&frame->payload[5]));
            return COPROC_STATUS_OK;
        case COPROC_SYS_MSG_HELLO_RSP:
            if (CoprocDispatch_RequirePayloadLength(frame, COPROC_PAYLOAD_LEN_HELLO_RSP) != COPROC_STATUS_OK) {
                return COPROC_STATUS_INVALID_SIZE;
            }
            CoprocDispatch_EmitEvent(eventFn, eventCtx, COPROC_DISPATCH_EVENT_HELLO_RSP, frame, 0u, 0u, 0u, 0u);
            return COPROC_STATUS_OK;
        default:
            return CoprocDispatch_SendNack(frame, COPROC_NACK_REASON_UNSUPPORTED_MSG, allocSeqFn, allocSeqCtx,
                                           txWriteFn, txWriteCtx, eventFn, eventCtx);
    }
}
