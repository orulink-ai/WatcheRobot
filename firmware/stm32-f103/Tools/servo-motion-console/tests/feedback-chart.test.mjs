import test from "node:test";
import assert from "node:assert/strict";
import {
  FEEDBACK_CHART_RENDER_INTERVAL_MS,
  FEEDBACK_STATUS_POLL_INTERVAL_MS,
  appendFeedbackSample,
  buildFeedbackChartModel,
  buildFeedbackPath,
} from "../src/feedbackChart.js";

test("feedback timing matches the STM32 coprocessor stream cadence", () => {
  assert.equal(FEEDBACK_STATUS_POLL_INTERVAL_MS, 50);
  assert.equal(FEEDBACK_CHART_RENDER_INTERVAL_MS, 50);
});

test("appendFeedbackSample keeps only valid samples inside the window", () => {
  const samples = [
    { timeMs: 1000, angle: 90, raw: 512, mv: 412 },
    { timeMs: 2500, angle: 92, raw: 520, mv: 419 },
  ];

  const next = appendFeedbackSample(samples, { angle: 95, raw: 540, mv: 435 }, { nowMs: 4000, windowMs: 2000 });
  assert.deepEqual(
    next.map((sample) => sample.angle),
    [92, 95],
  );

  const unchanged = appendFeedbackSample(next, { angle: Number.NaN }, { nowMs: 4100, windowMs: 2000 });
  assert.deepEqual(
    unchanged.map((sample) => sample.angle),
    [92, 95],
  );
});

test("buildFeedbackChartModel generates one path per visible series", () => {
  const model = buildFeedbackChartModel(
    [
      {
        id: "x",
        samples: [
          { timeMs: 1000, angle: 90 },
          { timeMs: 2000, angle: 120 },
        ],
      },
      {
        id: "y",
        samples: [{ timeMs: 2000, angle: 100 }],
      },
    ],
    { nowMs: 2000, windowMs: 1000, minAngle: 0, maxAngle: 180, width: 200, height: 120 },
  );

  assert.equal(model.series.length, 2);
  assert.ok(model.series[0].path.startsWith("M "));
  assert.ok(model.series[0].path.includes(" L "));
  assert.ok(model.series[1].path.startsWith("M "));
});

test("buildFeedbackPath returns an empty path without samples", () => {
  assert.equal(buildFeedbackPath([], (value) => value, (value) => value), "");
});
