import test from "node:test";
import assert from "node:assert/strict";

import {
  SERVO_DEFAULTS,
  buildAntiDropPlan,
  buildMotionCurve,
  buildMotionFrameSteps,
  estimateServoMotionResolution,
  motionPeakVelocityFactor,
  motionPointAt,
  normalizeLimit,
  planAutoSmoothMotion,
  servoToAxisPayload,
} from "../src/motionMath.js";
import {
  MotionProfile,
  buildServoMoveFrame,
  buildServoStopFrame,
  cliLedCountCommand,
  cliLimitCommand,
  cliMoveTimeCommand,
  cliServoOffCommand,
  cliSetPositionCommand,
  cliStopCommand,
  cliStatusCommand,
  cobsEncode,
  crc16Ccitt,
  parseServoStatusBlock,
} from "../src/serialProtocol.js";

test("crc16 uses the CCITT-FALSE vector", () => {
  assert.equal(crc16Ccitt(new TextEncoder().encode("123456789")), 0x29b1);
});

test("cobs encoder removes zero bytes before delimiter", () => {
  const encoded = cobsEncode(new Uint8Array([0x11, 0x00, 0x22, 0x33, 0x00]));
  assert.deepEqual(Array.from(encoded), [0x02, 0x11, 0x03, 0x22, 0x33, 0x01]);
});

test("servo axis mapping matches STM32 runtime constants", () => {
  assert.deepEqual(servoToAxisPayload(1, 900), { axisMask: 0x02, x: 0, y: 900 });
  assert.deepEqual(servoToAxisPayload(2, 1200), { axisMask: 0x01, x: 1200, y: 0 });
});

test("servo defaults expose the bench-safe axis limits", () => {
  assert.deepEqual(
    SERVO_DEFAULTS.map(({ id, min, max }) => ({ id, min, max })),
    [
      { id: 1, min: 100, max: 130 },
      { id: 2, min: 30, max: 150 },
    ],
  );
});

test("servo move frame encodes coprocessor frame with delimiter", () => {
  const wire = buildServoMoveFrame({
    seq: 7,
    servoId: 2,
    angle: 120,
    durationMs: 800,
    profile: MotionProfile.EASE_IN_OUT,
  });

  assert.equal(wire.at(-1), 0);
  assert.ok(wire.length > 12);
  assert.ok(wire.length < 40);
});

test("servo stop frame is compact and delimited", () => {
  const wire = buildServoStopFrame({ seq: 8, stopScope: 1 });

  assert.equal(wire.at(-1), 0);
  assert.ok(wire.length > 12);
});

test("cli command builders match firmware command grammar", () => {
  assert.equal(cliSetPositionCommand({ servoId: 1, angle: 91.2 }), "servo 1 91");
  assert.equal(cliMoveTimeCommand({ servoId: 1, angle: 120, durationMs: 800, steps: 40, profile: "ease" }), "servo_move_time 1 120 800 40 ease");
  assert.equal(cliMoveTimeCommand({ servoId: 1, angle: 120, durationMs: 2400, steps: 120, profile: "ease", easeStrengthPercent: 70 }), "servo_move_time 1 120 2400 120 ease 70");
  assert.equal(cliMoveTimeCommand({ servoId: 1, angle: 90, durationMs: 1600, steps: 80, profile: "anti_drop" }), "servo_move_time 1 90 1600 80 anti_drop");
  assert.equal(cliMoveTimeCommand({ servoId: 2, angle: 44.6, durationMs: 1200.4, steps: 60.3, profile: "linear" }), "servo_move_time 2 45 1200 60 linear");
  assert.equal(cliMoveTimeCommand({ servoId: 1, angle: 90, durationMs: 300, profile: "bad" }), "servo_move_time 1 90 300");
  assert.equal(cliStopCommand({ servoId: 2 }), "servo_stop 2");
  assert.equal(cliServoOffCommand({ servoId: 1 }), "servo_off 1");
  assert.equal(cliLimitCommand({ servoId: 1, min: 20, max: 160 }), "servo_limit 1 20 160");
  assert.equal(cliStatusCommand({ servoId: 2 }), "servo_status 2");
  assert.equal(cliLedCountCommand({ target: "ws_bottom", count: 40 }), "ws_bottom count 40");
});

test("servo status parser extracts feedback snapshot fields", () => {
  const parsed = parseServoStatusBlock(`
========== Servo Status ==========
  Servo : 2
  Pulse : 1833 us
  Target: 1833 us
  Motion: IDLE
  Profile: ease
  Ease  : 100%
  Update : 20 ms
  Limit : 0~180 deg
  Signal: ACTIVE
  Feedback: VALID
  Command Angle : 120 deg
  Feedback Raw  : 768
  Feedback V    : 618 mV
  Feedback Angle: 135 deg
==================================
`);

  assert.deepEqual(parsed, {
    servo: 2,
    pulseUs: 1833,
    targetPulseUs: 1833,
    motion: "IDLE",
    profile: "ease",
    easePercent: 100,
    updateMs: 20,
    limitMin: 0,
    limitMax: 180,
    signal: "ACTIVE",
    feedbackValid: true,
    commandAngle: 120,
    feedbackRaw: 768,
    feedbackMv: 618,
    feedbackAngle: 135,
  });
});

test("limits normalize and clamp to physical 0-180 degree range", () => {
  assert.deepEqual(normalizeLimit(160, 20), { min: 20, max: 160 });
  assert.deepEqual(normalizeLimit(-10, 220), { min: 0, max: 180 });
});

test("anti-drop curve preloads upward before descending", () => {
  const points = buildMotionCurve({ startAngle: 120, targetAngle: 90, durationMs: 1600, profile: "anti_drop", samples: 20 });
  const plan = buildAntiDropPlan({ startAngle: 120, targetAngle: 90, durationMs: 1600 });

  assert.deepEqual(
    {
      preloadDurationMs: plan.preloadDurationMs,
      holdDurationMs: plan.holdDurationMs,
      descentStartMs: plan.descentStartMs,
      descentDurationMs: plan.descentDurationMs,
    },
    {
      preloadDurationMs: 128,
      holdDurationMs: 96,
      descentStartMs: 224,
      descentDurationMs: 1376,
    },
  );
  assert.ok(points.some((point) => point.angle > 120));
  assert.equal(Math.round(points.at(-1).angle), 90);
  assert.ok(motionPointAt({ startAngle: 120, targetAngle: 90, durationMs: 1600, profile: "anti_drop" }, 900) < 120);
});

test("resolution estimate reports effective PWM frame and deadband limits", () => {
  const normal = estimateServoMotionResolution({ startAngle: 150, targetAngle: 120, durationMs: 800, steps: 40, profile: "linear" });
  assert.equal(normal.effectivePeriodMs, 20);
  assert.equal(normal.effectiveSteps, 40);
  assert.equal(normal.deltaPulseUs, 334);
  assert.equal(normal.status.level, "normal");

  const belowDeadband = estimateServoMotionResolution({ startAngle: 150, targetAngle: 120, durationMs: 3000, steps: 200, profile: "linear" });
  assert.equal(belowDeadband.effectivePeriodMs, 20);
  assert.equal(belowDeadband.effectiveSteps, 150);
  assert.equal(belowDeadband.status.level, "below-deadband");

  const saturated = estimateServoMotionResolution({ startAngle: 150, targetAngle: 120, durationMs: 3000, steps: 3000, profile: "linear" });
  assert.equal(saturated.effectivePeriodMs, 20);
  assert.equal(saturated.effectiveSteps, 150);
});

test("auto smooth planner chooses duration and steps from target us per frame", () => {
  const planned = planAutoSmoothMotion({ startAngle: 150, targetAngle: 120, targetPulsePerFrameUs: 4.2, profile: "ease", easeStrengthPercent: 100 });
  assert.equal(planned.durationMs, 2400);
  assert.equal(planned.steps, 120);
  assert.equal(Math.round(planned.expectedPeakPulsePerFrameUs * 10), 42);
  assert.equal(motionPeakVelocityFactor("ease", 100), 1.5);

  const softerEase = planAutoSmoothMotion({ startAngle: 150, targetAngle: 120, targetPulsePerFrameUs: 4.2, profile: "ease", easeStrengthPercent: 50 });
  assert.equal(softerEase.steps, 100);
  assert.equal(softerEase.durationMs, 2000);

  const linear = planAutoSmoothMotion({ startAngle: 150, targetAngle: 120, targetPulsePerFrameUs: 4.2, profile: "linear", easeStrengthPercent: 100 });
  assert.equal(linear.durationMs, 1600);
  assert.equal(linear.steps, 80);

  const lowerBound = planAutoSmoothMotion({ startAngle: 180, targetAngle: 0, targetPulsePerFrameUs: 0.5 });
  assert.equal(lowerBound.targetPulsePerFrameUs, 3);
  assert.equal(lowerBound.steps, 1000);
  assert.equal(lowerBound.durationMs, 20000);
  assert.ok(lowerBound.expectedPeakPulsePerFrameUs >= 3);

  const hold = planAutoSmoothMotion({ startAngle: 90, targetAngle: 90, targetPulsePerFrameUs: 4.2 });
  assert.equal(hold.steps, 1);
  assert.equal(hold.durationMs, 20);
});

test("frame step points expose the effective PWM updates", () => {
  const linear = buildMotionFrameSteps({ startAngle: 150, targetAngle: 120, durationMs: 1600, steps: 80, profile: "linear" });
  assert.equal(linear.length, 81);
  assert.equal(linear[0].elapsedMs, 0);
  assert.equal(linear.at(-1).elapsedMs, 1600);
  assert.equal(Math.round(linear[0].angle), 150);
  assert.equal(Math.round(linear.at(-1).angle), 120);

  const linearFirstDelta = Math.abs(linear[1].pulseUs - linear[0].pulseUs);
  const linearMidDelta = Math.abs(linear[41].pulseUs - linear[40].pulseUs);
  assert.ok(Math.abs(linearFirstDelta - linearMidDelta) <= 1);

  const ease = buildMotionFrameSteps({ startAngle: 150, targetAngle: 120, durationMs: 1600, steps: 80, profile: "ease" });
  const easeFirstDelta = Math.abs(ease[1].pulseUs - ease[0].pulseUs);
  const easeMidDelta = Math.abs(ease[41].pulseUs - ease[40].pulseUs);
  assert.ok(easeMidDelta > easeFirstDelta);

  const softerEase = buildMotionFrameSteps({
    startAngle: 150,
    targetAngle: 120,
    durationMs: 1600,
    steps: 80,
    profile: "ease",
    easeStrengthPercent: 50,
  });
  const softerEaseFirstDelta = Math.abs(softerEase[1].pulseUs - softerEase[0].pulseUs);
  assert.ok(softerEaseFirstDelta > easeFirstDelta);
});
