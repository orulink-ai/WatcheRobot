#ifndef USER_TESTHOST_STUBS_SERVO_H
#define USER_TESTHOST_STUBS_SERVO_H

#include <stdint.h>

typedef struct {
    uint16_t currentPulse;
    uint16_t targetPulse;
    uint16_t motionStartPulse;
    uint16_t motionTargetPulse;
    uint16_t motionDurationMs;
    uint16_t motionUpdatePeriodMs;
    uint8_t motionEaseStrengthPercent;
    uint8_t direction;
    uint8_t motionProfile;
    uint8_t isReciprocating;
    uint8_t isMotionActive;
    uint8_t isInitialized;
    uint8_t isSignalActive;
    uint32_t lastStepTickMs;
    uint32_t motionStartedAtMs;
    uint32_t lastMotionUpdateTickMs;
    uint32_t releaseDeadlineMs;
} Servo_StateTypeDef;

typedef enum {
    SERVO_MOTION_PROFILE_LINEAR = 0x00U,
    SERVO_MOTION_PROFILE_EASE_IN_OUT = 0x01U,
    SERVO_MOTION_PROFILE_ANTI_DROP = 0x02U
} Servo_MotionProfileTypeDef;

#define SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT 100U
#define SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT 100U

#endif /* USER_TESTHOST_STUBS_SERVO_H */
