export const SERVO_DEFAULTS = [
  { id: 1, name: "上下轴 Y", pin: "STM32 Servo 1 / PA6", min: 100, max: 130, angle: 115 },
  { id: 2, name: "左右轴 X", pin: "STM32 Servo 2 / PA7", min: 30, max: 150, angle: 90 },
];

export const SERVO_PWM_FRAME_MS = 20;
export const SERVO_DEADBAND_US = 3;
export const SERVO_MAX_TIME_STEPS = 3000;
export const SERVO_AUTO_MIN_US_PER_FRAME = 3;
export const SERVO_AUTO_MAX_US_PER_FRAME = 8;
export const SERVO_AUTO_DEFAULT_US_PER_FRAME = 4.2;
export const SERVO_EASE_STRENGTH_DEFAULT_PERCENT = 100;
export const SERVO_EASE_STRENGTH_MIN_PERCENT = 0;
export const SERVO_EASE_STRENGTH_MAX_PERCENT = 100;

export function clamp(value, min, max) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return min;
  }
  return Math.min(Math.max(numeric, min), max);
}

export function normalizeLimit(min, max) {
  const safeMin = clamp(Math.round(Number(min)), 0, 180);
  const safeMax = clamp(Math.round(Number(max)), 0, 180);

  if (safeMin <= safeMax) {
    return { min: safeMin, max: safeMax };
  }

  return { min: safeMax, max: safeMin };
}

export function angleToPulseUs(angle, pulseMin = 500, pulseMax = 2500) {
  const safeAngle = clamp(Math.round(Number(angle)), 0, 180);
  return Math.round(pulseMin + (safeAngle * (pulseMax - pulseMin)) / 180);
}

export function pulseToAngle(pulseUs, pulseMin = 500, pulseMax = 2500) {
  const safePulse = clamp(Math.round(Number(pulseUs)), pulseMin, pulseMax);
  return Math.round(((safePulse - pulseMin) * 180) / (pulseMax - pulseMin));
}

export function normalizeMotionProfile(profile) {
  return ["linear", "ease", "anti_drop"].includes(profile) ? profile : "ease";
}

export function normalizeEaseStrengthPercent(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return SERVO_EASE_STRENGTH_DEFAULT_PERCENT;
  }
  return clamp(Math.round(numeric), SERVO_EASE_STRENGTH_MIN_PERCENT, SERVO_EASE_STRENGTH_MAX_PERCENT);
}

export function motionPeakVelocityFactor(profile, easeStrengthPercent = SERVO_EASE_STRENGTH_DEFAULT_PERCENT) {
  const safeProfile = normalizeMotionProfile(profile);
  if (safeProfile === "linear") {
    return 1;
  }

  return 1 + normalizeEaseStrengthPercent(easeStrengthPercent) / 200;
}

export function buildAntiDropPlan({ startAngle, targetAngle, durationMs }) {
  const safeDurationMs = Math.max(1, Math.round(Number(durationMs)));
  const preloadDurationMs = Math.max(20, Math.round(safeDurationMs * 0.08));
  const holdDurationMs = Math.max(20, Math.round(safeDurationMs * 0.06));

  if (targetAngle >= startAngle || preloadDurationMs + holdDurationMs >= safeDurationMs) {
    return null;
  }

  const preloadAngle = pulseToAngle(angleToPulseUs(startAngle) + 18);
  return {
    preloadAngle,
    preloadDurationMs,
    holdDurationMs,
    descentStartMs: preloadDurationMs + holdDurationMs,
    descentDurationMs: safeDurationMs - preloadDurationMs - holdDurationMs,
  };
}

export function motionPointAt({ startAngle, targetAngle, durationMs, profile, easeStrengthPercent }, elapsedMs) {
  const safeDurationMs = Math.max(1, Math.round(Number(durationMs)));
  const t = clamp(Number(elapsedMs) / safeDurationMs, 0, 1);
  const safeProfile = normalizeMotionProfile(profile);
  const safeEaseStrengthPercent = normalizeEaseStrengthPercent(easeStrengthPercent);

  if (safeProfile === "linear") {
    return startAngle + (targetAngle - startAngle) * t;
  }

  if (safeProfile === "anti_drop" && targetAngle < startAngle) {
    const plan = buildAntiDropPlan({ startAngle, targetAngle, durationMs: safeDurationMs });
    if (!plan) {
      return startAngle + (targetAngle - startAngle) * smoothStep(t, safeEaseStrengthPercent);
    }

    if (elapsedMs <= plan.preloadDurationMs) {
      return startAngle + (plan.preloadAngle - startAngle) * smoothStep(elapsedMs / plan.preloadDurationMs, safeEaseStrengthPercent);
    }
    if (elapsedMs <= plan.descentStartMs) {
      return plan.preloadAngle;
    }
    return plan.preloadAngle + (targetAngle - plan.preloadAngle) * smoothStep((elapsedMs - plan.descentStartMs) / plan.descentDurationMs, safeEaseStrengthPercent);
  }

  return startAngle + (targetAngle - startAngle) * smoothStep(t, safeEaseStrengthPercent);
}

export function buildMotionCurve({ startAngle, targetAngle, durationMs, profile, easeStrengthPercent, samples = 80 }) {
  const safeSamples = clamp(Math.round(Number(samples)), 8, 240);
  const safeDurationMs = Math.max(1, Math.round(Number(durationMs)));
  const points = [];

  for (let index = 0; index <= safeSamples; index += 1) {
    const elapsedMs = Math.round((safeDurationMs * index) / safeSamples);
    points.push({
      elapsedMs,
      angle: motionPointAt({ startAngle, targetAngle, durationMs: safeDurationMs, profile, easeStrengthPercent }, elapsedMs),
    });
  }

  return points;
}

export function buildMotionFrameSteps({ startAngle, targetAngle, durationMs, steps, profile, easeStrengthPercent }) {
  const safeDurationMs = Math.max(1, Math.round(Number(durationMs)));
  const resolution = estimateServoMotionResolution({ startAngle, targetAngle, durationMs: safeDurationMs, steps });
  const points = [];

  for (let index = 0; index <= resolution.effectiveSteps; index += 1) {
    const elapsedMs = Math.min(safeDurationMs, index * resolution.effectivePeriodMs);
    const angle = motionPointAt({ startAngle, targetAngle, durationMs: safeDurationMs, profile, easeStrengthPercent }, elapsedMs);
    points.push({
      index,
      elapsedMs,
      angle,
      pulseUs: angleToPulseUsPrecise(angle),
    });
  }

  return points;
}

export function planAutoSmoothMotion({
  startAngle,
  targetAngle,
  targetPulsePerFrameUs = SERVO_AUTO_DEFAULT_US_PER_FRAME,
  profile = "ease",
  easeStrengthPercent = SERVO_EASE_STRENGTH_DEFAULT_PERCENT,
}) {
  const safeTargetPulsePerFrameUs = clamp(
    Number(targetPulsePerFrameUs),
    SERVO_AUTO_MIN_US_PER_FRAME,
    SERVO_AUTO_MAX_US_PER_FRAME,
  );
  const safeProfile = normalizeMotionProfile(profile);
  const safeEaseStrengthPercent = normalizeEaseStrengthPercent(easeStrengthPercent);
  const peakVelocityFactor = motionPeakVelocityFactor(safeProfile, safeEaseStrengthPercent);
  const startPulseUs = angleToPulseUs(startAngle);
  const targetPulseUs = angleToPulseUs(targetAngle);
  const deltaPulseUs = Math.abs(targetPulseUs - startPulseUs);
  const peakWeightedDeltaPulseUs = deltaPulseUs * peakVelocityFactor;
  const minUsefulSteps = Math.max(1, Math.ceil(peakWeightedDeltaPulseUs / SERVO_AUTO_MAX_US_PER_FRAME));
  const maxUsefulSteps = Math.floor(peakWeightedDeltaPulseUs / SERVO_AUTO_MIN_US_PER_FRAME);
  const targetSteps = Math.max(1, Math.ceil(peakWeightedDeltaPulseUs / safeTargetPulsePerFrameUs));
  const effectiveSteps =
    deltaPulseUs === 0 || maxUsefulSteps < 1 ? 1 : clamp(targetSteps, minUsefulSteps, maxUsefulSteps);
  const steps = clamp(effectiveSteps, 1, SERVO_MAX_TIME_STEPS);
  const durationMs = steps * SERVO_PWM_FRAME_MS;
  const expectedAveragePulsePerFrameUs = deltaPulseUs / steps;
  const expectedPeakPulsePerFrameUs = expectedAveragePulsePerFrameUs * peakVelocityFactor;

  return {
    durationMs,
    steps,
    targetPulsePerFrameUs: safeTargetPulsePerFrameUs,
    profile: safeProfile,
    easeStrengthPercent: safeEaseStrengthPercent,
    peakVelocityFactor,
    startPulseUs,
    targetPulseUs,
    deltaPulseUs,
    expectedAveragePulsePerFrameUs,
    expectedPeakPulsePerFrameUs,
    expectedPulsePerStepUs: expectedPeakPulsePerFrameUs,
  };
}

export function estimateServoMotionResolution({ startAngle, targetAngle, durationMs, steps, profile = "ease", easeStrengthPercent = SERVO_EASE_STRENGTH_DEFAULT_PERCENT }) {
  const safeDurationMs = Math.max(1, Math.round(Number(durationMs)));
  const safeSteps = Math.max(1, Math.round(Number(steps)));
  const safeProfile = normalizeMotionProfile(profile);
  const safeEaseStrengthPercent = normalizeEaseStrengthPercent(easeStrengthPercent);
  const startPulseUs = angleToPulseUs(startAngle);
  const targetPulseUs = angleToPulseUs(targetAngle);
  const deltaPulseUs = Math.abs(targetPulseUs - startPulseUs);
  const requestedPeriodMs = Math.max(1, Math.round(safeDurationMs / safeSteps));
  const effectivePeriodMs = Math.max(SERVO_PWM_FRAME_MS, requestedPeriodMs);
  const effectiveSteps = Math.max(1, Math.ceil(safeDurationMs / effectivePeriodMs));
  const pulsePerStepUs = deltaPulseUs / effectiveSteps;
  const peakVelocityFactor = motionPeakVelocityFactor(safeProfile, safeEaseStrengthPercent);
  const peakPulsePerFrameUs = pulsePerStepUs * peakVelocityFactor;
  const anglePerStepDeg = pulsePerStepUs * (180 / 2000);
  const status = resolutionStatus(peakPulsePerFrameUs, deltaPulseUs);

  return {
    startPulseUs,
    targetPulseUs,
    deltaPulseUs,
    requestedPeriodMs,
    effectivePeriodMs,
    effectiveSteps,
    pulsePerStepUs,
    peakVelocityFactor,
    peakPulsePerFrameUs,
    anglePerStepDeg,
    status,
  };
}

export function servoToAxisPayload(servoId, targetDegX10) {
  if (servoId === 1) {
    return { axisMask: 0x02, x: 0, y: targetDegX10 };
  }
  if (servoId === 2) {
    return { axisMask: 0x01, x: targetDegX10, y: 0 };
  }
  throw new Error(`Unsupported servo id: ${servoId}`);
}

function smoothStep(value, easeStrengthPercent = SERVO_EASE_STRENGTH_DEFAULT_PERCENT) {
  const t = clamp(value, 0, 1);
  const smooth = t * t * (3 - 2 * t);
  return t + (smooth - t) * (normalizeEaseStrengthPercent(easeStrengthPercent) / 100);
}

function angleToPulseUsPrecise(angle, pulseMin = 500, pulseMax = 2500) {
  const safeAngle = clamp(Number(angle), 0, 180);
  return Math.round(pulseMin + (safeAngle * (pulseMax - pulseMin)) / 180);
}

function resolutionStatus(pulsePerStepUs, deltaPulseUs) {
  if (deltaPulseUs === 0) {
    return {
      level: "hold",
      label: "保持",
      detail: "目标与起点一致",
    };
  }

  if (pulsePerStepUs < SERVO_DEADBAND_US) {
    return {
      level: "below-deadband",
      label: "低于死区",
      detail: `每帧 < ${SERVO_DEADBAND_US}us，可能攒误差后跳动`,
    };
  }

  if (pulsePerStepUs <= 8) {
    return {
      level: "fine",
      label: "细腻",
      detail: "接近舵机有效控制边界",
    };
  }

  if (pulsePerStepUs <= 12) {
    return {
      level: "normal",
      label: "正常",
      detail: "可观察到轻微阶梯感",
    };
  }

  return {
    level: "coarse",
    label: "偏粗",
    detail: "单帧跨度较大，慢速运动可能不够柔和",
  };
}
