import { clamp } from "./motionMath.js";

export function resolveServoStatusView(servo, status, options = {}) {
  const min = Number.isFinite(status.limitMin) ? status.limitMin : servo.min;
  const max = Number.isFinite(status.limitMax) ? status.limitMax : servo.max;
  const previousCommandAngle = Number.isFinite(servo.commandAngle) ? servo.commandAngle : servo.target;
  const commandAngle = Number.isFinite(status.commandAngle)
    ? clamp(Math.round(status.commandAngle), min, max)
    : clamp(Math.round(previousCommandAngle), min, max);
  const feedbackAngle =
    status.feedbackValid && Number.isFinite(status.feedbackAngle)
      ? clamp(Math.round(status.feedbackAngle), min, max)
      : null;
  const tracksFeedback = Boolean(options.feedbackPolling && feedbackAngle != null);
  const displayAngle = tracksFeedback ? feedbackAngle : commandAngle;

  return {
    min,
    max,
    commandAngle,
    feedbackAngle,
    tracksFeedback,
    displayAngle,
    timeTarget: tracksFeedback ? displayAngle : servo.timeTarget,
  };
}
