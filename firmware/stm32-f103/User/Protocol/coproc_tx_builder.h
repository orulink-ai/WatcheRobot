#ifndef USER_PROTOCOL_COPROC_TX_BUILDER_H
#define USER_PROTOCOL_COPROC_TX_BUILDER_H

#include "coproc_frame_codec.h"

typedef struct
{
    CoprocFrame frame;
    uint8_t wire[COPROC_FRAME_MAX_WIRE_SIZE];
    size_t wireLength;
} CoprocTxMessage;

CoprocStatus CoprocTxBuilder_BuildAck(uint32_t seq, uint32_t refSeq, CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildNack(uint32_t seq, uint32_t refSeq, uint16_t reasonCode, CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildFault(uint32_t seq,
                                        uint32_t refSeq,
                                        uint8_t faultSource,
                                        uint16_t faultCode,
                                        uint16_t detail,
                                        CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildHelloRsp(uint32_t seq, CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildMotionDone(uint32_t seq,
                                             uint32_t refSeq,
                                             uint8_t resultCode,
                                             int16_t finalXDegX10,
                                             int16_t finalYDegX10,
                                             uint16_t execTimeMs,
                                             CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildMotionState(uint32_t seq,
                                              uint32_t timestampMs,
                                              uint8_t validMask,
                                              int16_t xDegX10,
                                              int16_t yDegX10,
                                              uint16_t servo1Raw,
                                              uint16_t servo2Raw,
                                              CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildServoFeedbackRsp(uint32_t seq,
                                                   uint8_t validMask,
                                                   uint16_t servo1Raw,
                                                   int16_t servo1DegX10,
                                                   uint16_t servo2Raw,
                                                   int16_t servo2DegX10,
                                                   uint32_t timestampMs,
                                                   CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildLedDone(uint32_t seq,
                                          uint32_t refSeq,
                                          uint8_t resultCode,
                                          CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildTouchEvent(uint32_t seq,
                                             uint8_t touchId,
                                             uint8_t eventCode,
                                             uint32_t timestampMs,
                                             CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildMagState(uint32_t seq,
                                           uint16_t headingDegX100,
                                           uint16_t fieldNormUt,
                                           uint8_t quality,
                                           uint8_t statusBits,
                                           CoprocTxMessage *outMessage);
CoprocStatus CoprocTxBuilder_BuildImuState(uint32_t seq,
                                           int16_t rollDegX100,
                                           int16_t pitchDegX100,
                                           int16_t yawDegX100,
                                           uint16_t accNormMg,
                                           uint16_t gyroNormDpsX10,
                                           uint8_t motionFlags,
                                           CoprocTxMessage *outMessage);

#endif /* USER_PROTOCOL_COPROC_TX_BUILDER_H */
