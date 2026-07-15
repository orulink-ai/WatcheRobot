import { servoToAxisPayload } from "./motionMath.js";

export const TransportMode = Object.freeze({
  CLI: "cli",
  COPROC: "coproc",
});

export const MotionProfile = Object.freeze({
  LINEAR: 0,
  EASE_IN_OUT: 1,
});

const MSG_CLASS = Object.freeze({
  MOTION: 0x02,
  LED: 0x03,
  POWER: 0x05,
});

const MSG_ID = Object.freeze({
  SERVO_MOVE: 0x01,
  SERVO_STOP: 0x02,
  LED_SET_RGB: 0x01,
  LED_BREATHE: 0x02,
  LED_OFF: 0x03,
  POWER_5V_ENABLE: 0x01,
  POWER_5V_DISABLE: 0x02,
});

const ACK_REQ = 0x01;
const PROTO_VERSION = 0x01;
const MAGIC0 = 0xa5;
const MAGIC1 = 0x5a;

export function crc16Ccitt(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) !== 0 ? ((crc << 1) ^ 0x1021) : crc << 1;
      crc &= 0xffff;
    }
  }
  return crc;
}

export function cobsEncode(input) {
  const output = [];
  let codeIndex = 0;
  let code = 1;

  output.push(0);
  for (const byte of input) {
    if (byte === 0) {
      output[codeIndex] = code;
      codeIndex = output.length;
      output.push(0);
      code = 1;
      continue;
    }

    output.push(byte);
    code += 1;
    if (code === 0xff) {
      output[codeIndex] = code;
      codeIndex = output.length;
      output.push(0);
      code = 1;
    }
  }

  output[codeIndex] = code;
  return new Uint8Array(output);
}

export function buildFrame({ msgClass, msgId, flags = ACK_REQ, seq, payload = [] }) {
  const body = new Uint8Array(12 + payload.length);
  body[0] = MAGIC0;
  body[1] = MAGIC1;
  body[2] = PROTO_VERSION;
  body[3] = msgClass;
  body[4] = msgId;
  body[5] = flags;
  writeU32Le(body, 6, seq);
  writeU16Le(body, 10, payload.length);
  body.set(payload, 12);

  const crc = crc16Ccitt(body);
  const raw = new Uint8Array(body.length + 2);
  raw.set(body);
  writeU16Le(raw, body.length, crc);

  const encoded = cobsEncode(raw);
  const wire = new Uint8Array(encoded.length + 1);
  wire.set(encoded);
  wire[wire.length - 1] = 0;
  return wire;
}

export function buildServoMoveFrame({ seq, servoId, angle, durationMs, profile = MotionProfile.EASE_IN_OUT, sourceTag = 1 }) {
  const targetDegX10 = Math.round(angle * 10);
  const axis = servoToAxisPayload(servoId, targetDegX10);
  const payload = new Uint8Array(9);

  payload[0] = axis.axisMask;
  writeI16Le(payload, 1, axis.x);
  writeI16Le(payload, 3, axis.y);
  writeU16Le(payload, 5, durationMs);
  payload[7] = profile;
  payload[8] = sourceTag;

  return buildFrame({
    msgClass: MSG_CLASS.MOTION,
    msgId: MSG_ID.SERVO_MOVE,
    seq,
    payload,
  });
}

export function buildServoStopFrame({ seq, stopScope = 1, sourceTag = 1 }) {
  return buildFrame({
    msgClass: MSG_CLASS.MOTION,
    msgId: MSG_ID.SERVO_STOP,
    seq,
    payload: new Uint8Array([stopScope, sourceTag]),
  });
}

export function buildLedSetRgbFrame({ seq, red, green, blue, activeCount = 0 }) {
  return buildFrame({
    msgClass: MSG_CLASS.LED,
    msgId: MSG_ID.LED_SET_RGB,
    seq,
    payload: new Uint8Array([red, green, blue, activeCount & 0xff, (activeCount >> 8) & 0xff]),
  });
}

export function buildLedBreatheFrame({ seq, red, green, blue, step = 5, intervalMs = 15 }) {
  return buildFrame({
    msgClass: MSG_CLASS.LED,
    msgId: MSG_ID.LED_BREATHE,
    seq,
    payload: new Uint8Array([red, green, blue, step, intervalMs, 0]),
  });
}

export function buildLedOffFrame({ seq }) {
  return buildFrame({
    msgClass: MSG_CLASS.LED,
    msgId: MSG_ID.LED_OFF,
    seq,
    payload: new Uint8Array(0),
  });
}

export function buildPower5VFrame({ seq, enabled, sourceTag = 1 }) {
  return buildFrame({
    msgClass: MSG_CLASS.POWER,
    msgId: enabled ? MSG_ID.POWER_5V_ENABLE : MSG_ID.POWER_5V_DISABLE,
    seq,
    payload: new Uint8Array([sourceTag]),
  });
}

export function cliSetPositionCommand({ servoId, angle }) {
  return `servo ${servoId} ${Math.round(angle)}`;
}

export function cliMoveTimeCommand({ servoId, angle, durationMs, steps, profile = "ease", easeStrengthPercent }) {
  const command = `servo_move_time ${servoId} ${Math.round(angle)} ${Math.round(durationMs)}`;
  const safeSteps = Math.round(Number(steps));
  const normalizedProfile = ["linear", "ease", "anti_drop"].includes(profile) ? profile : "";
  const safeEaseStrengthPercent = Math.round(Number(easeStrengthPercent));
  const parts = [command];
  if (Number.isFinite(safeSteps) && safeSteps > 0) {
    parts.push(String(safeSteps));
  }
  if (normalizedProfile !== "") {
    parts.push(normalizedProfile);
  }
  if (normalizedProfile === "ease" && Number.isFinite(safeEaseStrengthPercent)) {
    parts.push(String(Math.min(Math.max(safeEaseStrengthPercent, 0), 100)));
  }
  return parts.join(" ");
}

export function cliStopCommand({ servoId }) {
  return `servo_stop ${servoId}`;
}

export function cliServoOffCommand({ servoId }) {
  return `servo_off ${servoId}`;
}

export function cliLimitCommand({ servoId, min, max }) {
  return `servo_limit ${servoId} ${Math.round(min)} ${Math.round(max)}`;
}

export function cliStatusCommand({ servoId }) {
  return `servo_status ${servoId}`;
}

export function parseServoStatusBlock(text) {
  const source = String(text ?? "");
  if (!source.includes("Servo Status")) {
    return null;
  }

  const servo = readInteger(source, /\bServo\s*:\s*(\d+)/i);
  if (servo == null) {
    return null;
  }

  const limit = source.match(/\bLimit\s*:\s*(\d+)\s*~\s*(\d+)\s*deg/i);
  const feedbackState = source.match(/\bFeedback\s*:\s*([A-Z]+)/i)?.[1]?.toUpperCase() ?? "UNKNOWN";
  const feedbackValid = feedbackState === "VALID";

  return {
    servo,
    pulseUs: readInteger(source, /\bPulse\s*:\s*(\d+)\s*us/i),
    targetPulseUs: readInteger(source, /\bTarget\s*:\s*(\d+)\s*us/i),
    motion: readText(source, /\bMotion\s*:\s*([A-Z_]+)/i),
    profile: readText(source, /\bProfile\s*:\s*([a-zA-Z_]+)/i),
    easePercent: readInteger(source, /\bEase\s*:\s*(\d+)%/i),
    updateMs: readInteger(source, /\bUpdate\s*:\s*(\d+)\s*ms/i),
    limitMin: limit ? Number(limit[1]) : null,
    limitMax: limit ? Number(limit[2]) : null,
    signal: readText(source, /\bSignal\s*:\s*([A-Z_]+)/i),
    feedbackValid,
    commandAngle: readInteger(source, /\bCommand Angle\s*:\s*(\d+)\s*deg/i),
    feedbackRaw: readInteger(source, /\bFeedback Raw\s*:\s*(\d+)/i),
    feedbackMv: readInteger(source, /\bFeedback V\s*:\s*(\d+)\s*mV/i),
    feedbackAngle: readInteger(source, /\bFeedback Angle\s*:\s*(\d+)\s*deg/i),
  };
}

export function cliLedCommand({ target = "ws", command }) {
  return `${target} ${command}`;
}

export function cliLedCountCommand({ target = "ws", count }) {
  return cliLedCommand({ target, command: `count ${Math.round(count)}` });
}

function writeU16Le(target, offset, value) {
  target[offset] = value & 0xff;
  target[offset + 1] = (value >> 8) & 0xff;
}

function writeI16Le(target, offset, value) {
  writeU16Le(target, offset, value < 0 ? 0x10000 + value : value);
}

function writeU32Le(target, offset, value) {
  target[offset] = value & 0xff;
  target[offset + 1] = (value >> 8) & 0xff;
  target[offset + 2] = (value >> 16) & 0xff;
  target[offset + 3] = (value >> 24) & 0xff;
}

function readInteger(source, pattern) {
  const match = source.match(pattern);
  return match ? Number(match[1]) : null;
}

function readText(source, pattern) {
  return source.match(pattern)?.[1] ?? null;
}
