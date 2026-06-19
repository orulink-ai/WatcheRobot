export const FEEDBACK_CHART_WINDOW_MS = 30000;
export const FEEDBACK_CHART_MAX_SAMPLES = 600;
export const FEEDBACK_STATUS_POLL_INTERVAL_MS = 50;
export const FEEDBACK_CHART_RENDER_INTERVAL_MS = 50;

export function appendFeedbackSample(samples, sample, options = {}) {
  const nowMs = Number.isFinite(options.nowMs) ? options.nowMs : Date.now();
  const windowMs = Number.isFinite(options.windowMs) ? Math.max(1, options.windowMs) : FEEDBACK_CHART_WINDOW_MS;
  const maxSamples = Number.isFinite(options.maxSamples)
    ? Math.max(1, Math.round(options.maxSamples))
    : FEEDBACK_CHART_MAX_SAMPLES;
  const nextSample = {
    timeMs: nowMs,
    angle: Number(sample?.angle),
    raw: Number(sample?.raw),
    mv: Number(sample?.mv),
  };

  if (!Number.isFinite(nextSample.angle)) {
    return Array.isArray(samples) ? samples.slice() : [];
  }

  const cutoffMs = nowMs - windowMs;
  return [...(Array.isArray(samples) ? samples : []), nextSample]
    .filter((entry) => entry.timeMs >= cutoffMs)
    .slice(-maxSamples);
}

export function buildFeedbackChartModel(series, options = {}) {
  const width = Number.isFinite(options.width) ? options.width : 760;
  const height = Number.isFinite(options.height) ? options.height : 260;
  const padding = {
    left: 46,
    right: 18,
    top: 18,
    bottom: 34,
    ...(options.padding ?? {}),
  };
  const nowMs = Number.isFinite(options.nowMs) ? options.nowMs : Date.now();
  const windowMs = Number.isFinite(options.windowMs) ? Math.max(1, options.windowMs) : FEEDBACK_CHART_WINDOW_MS;
  const xMin = nowMs - windowMs;
  const xMax = nowMs;
  const visibleSeries = (Array.isArray(series) ? series : []).map((item) => ({
    ...item,
    samples: (Array.isArray(item.samples) ? item.samples : []).filter(
      (sample) => sample.timeMs >= xMin && sample.timeMs <= xMax && Number.isFinite(sample.angle),
    ),
  }));
  const angles = visibleSeries.flatMap((item) => item.samples.map((sample) => sample.angle));
  const explicitMin = Number.isFinite(options.minAngle) ? options.minAngle : null;
  const explicitMax = Number.isFinite(options.maxAngle) ? options.maxAngle : null;
  const minAngle = explicitMin ?? (angles.length ? Math.floor(Math.min(...angles) / 5) * 5 : 0);
  const maxAngle = explicitMax ?? (angles.length ? Math.ceil(Math.max(...angles) / 5) * 5 : 180);
  const yMin = Math.min(minAngle, maxAngle - 1);
  const yMax = Math.max(maxAngle, yMin + 1);
  const plotWidth = Math.max(1, width - padding.left - padding.right);
  const plotHeight = Math.max(1, height - padding.top - padding.bottom);

  const toX = (timeMs) => padding.left + ((timeMs - xMin) / windowMs) * plotWidth;
  const toY = (angle) => padding.top + (1 - (angle - yMin) / (yMax - yMin)) * plotHeight;

  return {
    width,
    height,
    padding,
    nowMs,
    windowMs,
    xMin,
    xMax,
    yMin,
    yMax,
    plotWidth,
    plotHeight,
    series: visibleSeries.map((item) => ({
      ...item,
      path: buildFeedbackPath(item.samples, toX, toY),
    })),
  };
}

export function buildFeedbackPath(samples, toX, toY) {
  if (!Array.isArray(samples) || samples.length === 0 || typeof toX !== "function" || typeof toY !== "function") {
    return "";
  }

  return samples
    .map((sample, index) => {
      const command = index === 0 ? "M" : "L";
      return `${command} ${toX(sample.timeMs).toFixed(1)} ${toY(sample.angle).toFixed(1)}`;
    })
    .join(" ");
}
