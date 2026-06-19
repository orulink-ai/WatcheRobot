import assert from "node:assert/strict";
import test from "node:test";

import { resolveServoStatusView } from "../src/servoStatusView.js";

test("feedback collection displays measured angle as the live target", () => {
  const view = resolveServoStatusView(
    { target: 83, timeTarget: 90, min: 0, max: 180, commandAngle: 83 },
    { commandAngle: 83, feedbackValid: true, feedbackAngle: 44 },
    { feedbackPolling: true },
  );

  assert.equal(view.commandAngle, 83);
  assert.equal(view.feedbackAngle, 44);
  assert.equal(view.displayAngle, 44);
  assert.equal(view.timeTarget, 44);
  assert.equal(view.tracksFeedback, true);
});

test("normal control mode keeps command angle as the target", () => {
  const view = resolveServoStatusView(
    { target: 83, timeTarget: 90, min: 0, max: 180, commandAngle: 83 },
    { commandAngle: 83, feedbackValid: true, feedbackAngle: 44 },
    { feedbackPolling: false },
  );

  assert.equal(view.commandAngle, 83);
  assert.equal(view.feedbackAngle, 44);
  assert.equal(view.displayAngle, 83);
  assert.equal(view.timeTarget, 90);
  assert.equal(view.tracksFeedback, false);
});

test("feedback display angle is clamped to the active servo limits", () => {
  const view = resolveServoStatusView(
    { target: 113, timeTarget: 113, min: 90, max: 130, commandAngle: 113 },
    { commandAngle: 113, feedbackValid: true, feedbackAngle: 42 },
    { feedbackPolling: true },
  );

  assert.equal(view.displayAngle, 90);
  assert.equal(view.timeTarget, 90);
});
