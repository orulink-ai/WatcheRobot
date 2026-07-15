import "./styles.css";
import {
  FEEDBACK_CHART_RENDER_INTERVAL_MS,
  FEEDBACK_CHART_WINDOW_MS,
  FEEDBACK_STATUS_POLL_INTERVAL_MS,
  appendFeedbackSample,
  buildFeedbackChartModel,
} from "./feedbackChart.js";
import {
  TransportMode,
  cliLimitCommand,
  cliLedCommand,
  cliLedCountCommand,
  cliMoveTimeCommand,
  cliServoOffCommand,
  cliSetPositionCommand,
  cliStatusCommand,
  parseServoStatusBlock,
} from "./serialProtocol.js";
import { resolveServoStatusView } from "./servoStatusView.js";
import {
  SERVO_DEFAULTS,
  SERVO_AUTO_DEFAULT_US_PER_FRAME,
  SERVO_AUTO_MAX_US_PER_FRAME,
  SERVO_AUTO_MIN_US_PER_FRAME,
  SERVO_EASE_STRENGTH_DEFAULT_PERCENT,
  SERVO_EASE_STRENGTH_MAX_PERCENT,
  SERVO_EASE_STRENGTH_MIN_PERCENT,
  SERVO_MAX_TIME_STEPS,
  angleToPulseUs,
  buildMotionCurve,
  buildMotionFrameSteps,
  clamp,
  estimateServoMotionResolution,
  motionPointAt,
  normalizeEaseStrengthPercent,
  normalizeLimit,
  normalizeMotionProfile,
  planAutoSmoothMotion,
} from "./motionMath.js";

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const state = {
  connected: false,
  mode: TransportMode.CLI,
  baudRate: 115200,
  port: null,
  reader: null,
  writer: null,
  readAbort: false,
  rxTextBuffer: "",
  liveDrag: false,
  liveDragTimer: 0,
  feedbackPolling: false,
  feedbackToggleBusy: false,
  feedbackPollTimer: 0,
  feedbackPollInFlight: false,
  motionAnimationFrame: 0,
  feedbackChartTimer: 0,
  commandHistory: [],
  commandHistoryIndex: -1,
  draftManualCommand: "",
  log: [],
  servos: SERVO_DEFAULTS.map((servo) => ({
    ...servo,
    target: servo.angle,
    timeTarget: servo.angle,
    durationMs: 800,
    steps: 40,
    autoSmooth: false,
    targetPulsePerFrameUs: SERVO_AUTO_DEFAULT_US_PER_FRAME,
    easeStrengthPercent: SERVO_EASE_STRENGTH_DEFAULT_PERCENT,
    profile: "ease",
    commandAngle: servo.angle,
    activeMotion: null,
    feedback: null,
    feedbackSamples: [],
    limitPendingUntil: 0,
    scheduledMotionTimers: [],
  })),
  lights: {
    side: { target: "ws", label: "侧边灯", count: 1, max: 16, color: "#00c2ff" },
    bottom: { target: "ws_bottom", label: "底部灯", count: 40, max: 40, color: "#8fc31f" },
  },
};

const app = document.querySelector("#app");
app.innerHTML = `
  <main class="debug-shell">
    <header class="debug-header">
      <div>
        <p class="eyeline">WatcheRobot STM32</p>
        <h1>STM32调试页面</h1>
        <p class="subtitle">面向硬件调试的 Web Serial 工具。页面只封装 STM32 已有原子命令，复杂动作组合留给上层系统。</p>
      </div>
      <div class="connection">
        <button id="connectButton" class="primary">连接 STM32</button>
        <button id="disconnectButton" disabled>断开</button>
        <label class="baud-field">波特率
          <input id="baudRate" type="number" min="9600" step="9600" value="115200" />
        </label>
      </div>
    </header>

    <section id="connectionNotice" class="connection-notice" role="status"></section>

    <section class="panel control-ribbon" aria-label="全局调试控制">
      <div>
        <span>Feedback</span>
        <h2>反馈采集 / 舵机释放</h2>
        <p>开启时释放两个舵机 PWM 并读取反馈角度；关闭时按最近反馈角重新锁住当前位置。</p>
      </div>
      <div class="ribbon-actions">
        <button id="refreshServoStatus" data-requires-connection>读取状态</button>
        <label class="toggle feedback-toggle">
          <input id="autoRefreshServoStatus" type="checkbox" data-requires-connection />
          开启反馈
        </label>
        <button id="clearFeedbackChart">清空曲线</button>
      </div>
    </section>

    <section class="layout-grid">
      <div class="panel-stack telemetry-stack">
        <section class="panel feedback-chart-panel">
          <div class="panel-title">
            <div>
              <span>Feedback</span>
              <h2>反馈角曲线</h2>
            </div>
            <div class="status-actions">
              <span id="feedbackChartMeta" class="soft-note">等待样本</span>
            </div>
          </div>
          <div class="feedback-chart-shell" aria-live="polite">
            <svg id="feedbackChart" viewBox="0 0 760 260" role="img" aria-label="STM32 servo feedback angle curve"></svg>
          </div>
          <div class="feedback-chart-legend">
            <span><i class="legend-dot legend-dot-y"></i>Y 轴 Servo 1</span>
            <span><i class="legend-dot legend-dot-x"></i>X 轴 Servo 2</span>
          </div>
        </section>

        <section class="panel pose-panel">
          <div class="panel-title">
            <div>
              <span>Posture</span>
              <h2>当前姿态</h2>
            </div>
          </div>
          <div class="pose-stage" aria-hidden="true">
            <div class="axis-label axis-top">上</div>
            <div class="axis-label axis-left">左</div>
            <div class="axis-label axis-right">右</div>
            <div class="axis-label axis-bottom">下</div>
            <div class="robot-face"><span></span><span></span></div>
            <div id="poseDot" class="pose-dot"></div>
          </div>
          <div class="pose-readout">
            <div><span>X 左右轴</span><strong id="poseXValue">90 度</strong></div>
            <div><span>Y 上下轴</span><strong id="poseYValue">90 度</strong></div>
          </div>
          <div class="quick-actions">
            <button id="centerAll" data-requires-connection>回中</button>
          </div>
        </section>

        <section class="panel serial-panel">
          <div class="panel-title">
            <div>
              <span>Serial</span>
              <h2>串口日志</h2>
            </div>
            <button id="clearLog">清空日志</button>
          </div>
          <pre id="serialLog"></pre>
          <div class="manual-row">
            <input id="manualCommand" type="text" placeholder="手动输入 STM32 CLI 命令，Enter 发送，↑/↓ 切换历史" data-requires-connection />
            <button id="sendManual" data-requires-connection>发送</button>
          </div>
        </section>
      </div>

      <div class="panel-stack control-stack">
        <section class="panel servo-panel">
          <div class="panel-title">
            <div>
              <span>Servo</span>
              <h2>舵机位置控制</h2>
            </div>
            <label class="toggle"><input id="liveDrag" type="checkbox" data-requires-connection /> 滑动实时发送</label>
          </div>
          <div id="servoGrid" class="servo-grid"></div>
        </section>

        <section class="panel light-panel">
          <div class="panel-title">
            <div>
              <span>Light</span>
              <h2>灯光控制</h2>
            </div>
            <span class="soft-note">侧边灯 ws / 底部灯 ws_bottom</span>
          </div>
          <div id="lightGrid" class="light-grid"></div>
        </section>
      </div>
    </section>
  </main>
`;

const elements = {
  baudRate: document.querySelector("#baudRate"),
  connectButton: document.querySelector("#connectButton"),
  disconnectButton: document.querySelector("#disconnectButton"),
  connectionNotice: document.querySelector("#connectionNotice"),
  liveDrag: document.querySelector("#liveDrag"),
  poseDot: document.querySelector("#poseDot"),
  poseXValue: document.querySelector("#poseXValue"),
  poseYValue: document.querySelector("#poseYValue"),
  refreshServoStatus: document.querySelector("#refreshServoStatus"),
  autoRefreshServoStatus: document.querySelector("#autoRefreshServoStatus"),
  centerAll: document.querySelector("#centerAll"),
  servoGrid: document.querySelector("#servoGrid"),
  lightGrid: document.querySelector("#lightGrid"),
  feedbackChart: document.querySelector("#feedbackChart"),
  feedbackChartMeta: document.querySelector("#feedbackChartMeta"),
  clearFeedbackChart: document.querySelector("#clearFeedbackChart"),
  serialLog: document.querySelector("#serialLog"),
  clearLog: document.querySelector("#clearLog"),
  manualCommand: document.querySelector("#manualCommand"),
  sendManual: document.querySelector("#sendManual"),
};

state.servos.filter((servo) => servo.autoSmooth).forEach(applyAutoSmoothPlan);
renderServos();
renderAllMotionCurves();
renderFeedbackChart();
renderLights();
bindEvents();
updateConnectionState();
updatePosePreview();
writeLog("STM32调试页面已就绪。请使用 Chromium / Edge 的 localhost 环境连接 Web Serial。", "system");

function renderServos() {
  elements.servoGrid.innerHTML = state.servos
    .map(
      (servo) => `
        <article class="servo-card ${servo.autoSmooth ? "auto-smooth-enabled" : ""}" data-servo="${servo.id}">
          <div class="servo-head">
            <div>
              <h3>${servo.name}</h3>
              <span>${servo.pin}</span>
            </div>
            <div class="servo-head-actions">
              <strong><span data-role="angle">${servo.target}</span> 度</strong>
              <button data-action="read-status" data-requires-connection>反馈</button>
            </div>
          </div>

          <div class="feedback-strip" data-role="feedback">
            <div>
              <span>反馈角</span>
              <strong data-feedback="angle">--</strong>
            </div>
            <div>
              <span>ADC raw</span>
              <strong data-feedback="raw">--</strong>
            </div>
            <div>
              <span>电压</span>
              <strong data-feedback="mv">--</strong>
            </div>
            <div>
              <span>状态</span>
              <strong data-feedback="state">未读取</strong>
            </div>
          </div>

          <section class="mode-block position-mode" aria-label="${servo.name} 位置基模式">
            <div class="mode-head">
              <div>
                <strong>位置基模式</strong>
                <span>发送 servo，按 STM32 当前最快速度到达</span>
              </div>
              <button data-action="position-move" class="primary" data-requires-connection>立即到达</button>
            </div>
            <input class="motion-slider" data-role="slider" type="range" min="${servo.min}" max="${servo.max}" value="${servo.target}" data-requires-connection />
            <div class="motion-fields">
              <label>目标角
                <input data-role="target" type="number" min="${servo.min}" max="${servo.max}" value="${servo.target}" data-requires-connection />
              </label>
              <label>最小角
                <input data-role="min" type="number" min="0" max="180" value="${servo.min}" data-requires-connection />
              </label>
              <label>最大角
                <input data-role="max" type="number" min="0" max="180" value="${servo.max}" data-requires-connection />
              </label>
            </div>
          </section>

          <section class="mode-block time-mode" aria-label="${servo.name} 时间基模式">
            <div class="mode-head">
              <div>
                <strong>时间基模式</strong>
                <span>发送 servo_move_time，由 STM32 端按 us 脉宽非阻塞插值</span>
              </div>
              <button data-action="time-move" class="primary" data-requires-connection>按时间到达</button>
            </div>
            <div class="auto-smooth-bar" data-role="auto-smooth-panel">
              <label class="auto-toggle"><input data-role="auto-smooth" type="checkbox" ${servo.autoSmooth ? "checked" : ""} /> 自动细腻</label>
              <label>峰值每帧 us
                <input data-role="auto-us" type="number" min="${SERVO_AUTO_MIN_US_PER_FRAME}" max="${SERVO_AUTO_MAX_US_PER_FRAME}" step="0.1" value="${servo.targetPulsePerFrameUs}" />
              </label>
              <span data-role="auto-plan">按峰值速度自动计算到达时间和步数</span>
            </div>
            <div class="time-fields">
              <label>目标位置
                <input data-role="time-target" type="number" min="${servo.min}" max="${servo.max}" value="${servo.timeTarget}" data-requires-connection />
              </label>
              <label>到达时间 ms
                <input data-role="duration" type="number" min="1" max="60000" step="50" value="${servo.durationMs}" ${servo.autoSmooth ? "readonly" : ""} data-requires-connection />
              </label>
              <label>步数 / 分辨率
                <input data-role="steps" type="number" min="1" max="${SERVO_MAX_TIME_STEPS}" step="1" value="${servo.steps}" ${servo.autoSmooth ? "readonly" : ""} data-requires-connection />
              </label>
              <label>曲线
                <select data-role="profile" data-requires-connection>
                  <option value="ease" ${servo.profile === "ease" ? "selected" : ""}>缓入缓出</option>
                  <option value="linear" ${servo.profile === "linear" ? "selected" : ""}>线性</option>
                </select>
              </label>
            </div>
            <div class="ease-strength-row ${servo.profile === "linear" ? "is-disabled" : ""}">
              <label>缓入缓出强度
                <input data-role="ease-strength" type="range" min="${SERVO_EASE_STRENGTH_MIN_PERCENT}" max="${SERVO_EASE_STRENGTH_MAX_PERCENT}" step="5" value="${servo.easeStrengthPercent}" data-requires-connection />
              </label>
              <label>强度 %
                <input data-role="ease-strength-number" type="number" min="${SERVO_EASE_STRENGTH_MIN_PERCENT}" max="${SERVO_EASE_STRENGTH_MAX_PERCENT}" step="5" value="${servo.easeStrengthPercent}" data-requires-connection />
              </label>
              <span data-role="ease-strength-note">100% 为当前 smoothstep，0% 接近线性</span>
            </div>
          </section>
          <div class="curve-panel" data-role="curve-panel">
            <div class="curve-head">
              <strong>计划曲线</strong>
              <span data-role="curve-summary">目标命令轨迹，不代表真实反馈</span>
            </div>
            <svg data-role="curve-svg" viewBox="0 0 520 180" preserveAspectRatio="none" aria-hidden="true">
              <line x1="38" y1="16" x2="38" y2="146"></line>
              <line x1="38" y1="146" x2="496" y2="146"></line>
              <path data-role="curve-path"></path>
              <g data-role="curve-steps" class="curve-steps"></g>
              <circle data-role="curve-dot" r="3.5"></circle>
            </svg>
            <div class="curve-scale">
              <span data-role="curve-start"></span>
              <span data-role="curve-end"></span>
            </div>
            <div class="resolution-metrics" data-role="resolution-metrics">
              <span data-metric="frames">有效帧 --</span>
              <span data-metric="pulse-step">每帧 -- us</span>
              <span data-metric="deadband">死区 --</span>
            </div>
            <p class="resolution-note" data-role="resolution-note">基于 50Hz PWM 和 3us 死区估算。</p>
          </div>
          <span class="pulse-note" data-role="pulse">${angleToPulseUs(servo.target)} us</span>
        </article>
      `,
    )
    .join("");
}

function renderLights() {
  elements.lightGrid.innerHTML = Object.entries(state.lights)
    .map(
      ([key, light]) => `
        <article class="light-card">
          <div class="light-control" data-light-panel="${key}">
            <strong>${light.label}</strong>
            <input data-light-role="color" type="color" value="${light.color}" aria-label="${light.label}颜色" data-requires-connection />
            <label>数量
              <input data-light-role="count" type="number" min="1" max="${light.max}" value="${light.count}" data-requires-connection />
            </label>
            <button data-light-action="rgb" class="primary" data-requires-connection>应用 RGB</button>
          </div>
          <div class="light-actions">
            <button data-light-action="preset" data-light-panel="${key}" data-value="red" data-requires-connection>红</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="green" data-requires-connection>绿</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="blue" data-requires-connection>蓝</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="white" data-requires-connection>白</button>
            <button data-light-action="effect" data-light-panel="${key}" data-value="breathe" data-requires-connection>呼吸</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="rainbow" data-requires-connection>彩虹</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="stop" data-requires-connection>停止</button>
            <button data-light-action="preset" data-light-panel="${key}" data-value="off" class="danger" data-requires-connection>关闭</button>
          </div>
        </article>
      `,
    )
    .join("");
}

function bindEvents() {
  elements.baudRate.addEventListener("input", () => {
    state.baudRate = Number(elements.baudRate.value);
  });
  elements.connectButton.addEventListener("click", connectSerial);
  elements.disconnectButton.addEventListener("click", disconnectSerial);
  elements.liveDrag.addEventListener("change", () => {
    state.liveDrag = elements.liveDrag.checked;
  });
  elements.refreshServoStatus.addEventListener("click", readAllServoStatus);
  elements.autoRefreshServoStatus.addEventListener("change", () => {
    setFeedbackPolling(elements.autoRefreshServoStatus.checked);
  });
  elements.centerAll.addEventListener("click", centerAllServos);
  elements.clearFeedbackChart.addEventListener("click", clearFeedbackChart);
  elements.servoGrid.addEventListener("input", handleServoInput);
  elements.servoGrid.addEventListener("change", handleServoCommit);
  elements.servoGrid.addEventListener("click", handleServoAction);

  document.querySelectorAll("[data-light-role]").forEach((input) => input.addEventListener("input", handleLightInput));
  document.querySelectorAll("[data-light-action]").forEach((button) => button.addEventListener("click", handleLightAction));

  elements.clearLog.addEventListener("click", () => {
    state.log = [];
    renderLog();
  });
  elements.sendManual.addEventListener("click", sendManualCommand);
  elements.manualCommand.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      sendManualCommand();
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      recallManualCommand(-1);
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      recallManualCommand(1);
    }
  });
}

function updateConnectionState() {
  document.querySelectorAll("[data-requires-connection]").forEach((element) => {
    const sendsCommand =
      element.tagName === "BUTTON" ||
      element.id === "manualCommand" ||
      element.id === "liveDrag" ||
      element.id === "autoRefreshServoStatus";
    const disabledByFeedbackMode = state.feedbackPolling && element.closest(".servo-panel") != null;
    element.disabled =
      (sendsCommand && !state.connected) ||
      disabledByFeedbackMode ||
      (element.id === "autoRefreshServoStatus" && state.feedbackToggleBusy);
  });
  elements.connectButton.disabled = state.connected;
  elements.disconnectButton.disabled = !state.connected;
  elements.baudRate.disabled = state.connected;
  elements.connectionNotice.classList.toggle("is-connected", state.connected);
  elements.connectionNotice.innerHTML = state.connected
    ? `<strong>已连接 STM32</strong><span>调试按钮已解锁，所有操作都会直接发到当前串口。</span>`
    : `<strong>未连接 STM32</strong><span>请先连接 USART1 CLI 串口，默认波特率 115200。</span>`;
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    writeLog("当前浏览器不支持 Web Serial。请使用 Chromium 或 Edge。", "error");
    return;
  }

  try {
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate: Number(state.baudRate) });
    state.writer = state.port.writable.getWriter();
    state.connected = true;
    state.readAbort = false;
    updateConnectionState();
    writeLog(`已连接：${state.baudRate} 波特。`, "system");
    readLoop();
    updateFeedbackPolling();
  } catch (error) {
    writeLog(`连接失败：${error.message}`, "error");
  }
}

async function disconnectSerial() {
  state.readAbort = true;
  try {
    if (state.connected && state.feedbackPolling && state.writer) {
      state.feedbackPolling = false;
      elements.autoRefreshServoStatus.checked = false;
      stopFeedbackPollingTimer();
      await lockAllServosFromFeedback();
    }
    if (state.reader) {
      await state.reader.cancel();
      state.reader.releaseLock();
      state.reader = null;
    }
    if (state.writer) {
      state.writer.releaseLock();
      state.writer = null;
    }
    if (state.port) {
      await state.port.close();
      state.port = null;
    }
  } catch (error) {
    writeLog(`断开提示：${error.message}`, "error");
  }
  stopFeedbackPollingTimer();
  state.feedbackPolling = false;
  state.feedbackToggleBusy = false;
  elements.autoRefreshServoStatus.checked = false;
  state.connected = false;
  updateConnectionState();
  writeLog("已断开。", "system");
}

async function readLoop() {
  while (state.port?.readable && !state.readAbort) {
    state.reader = state.port.readable.getReader();
    try {
      while (!state.readAbort) {
        const { value, done } = await state.reader.read();
        if (done) {
          break;
        }
        if (value) {
          processIncomingText(decoder.decode(value, { stream: true }));
        }
      }
    } catch (error) {
      if (!state.readAbort) {
        writeLog(`读取错误：${error.message}`, "error");
      }
    } finally {
      state.reader?.releaseLock();
      state.reader = null;
    }
  }
}

function handleServoInput(event) {
  const card = event.target.closest("[data-servo]");
  if (!card) {
    return;
  }

  const servo = getServo(Number(card.dataset.servo));
  syncServoFromCard(servo, card, event.target.dataset.role);
  updateServoCard(card, servo);

  if (state.connected && state.liveDrag && event.target.dataset.role === "slider") {
    window.clearTimeout(state.liveDragTimer);
    state.liveDragTimer = window.setTimeout(() => moveServo(servo.id, servo.target), 70);
  }
}

function handleServoCommit(event) {
  const card = event.target.closest("[data-servo]");
  const role = event.target.dataset.role;
  if (!card || !["slider", "target", "min", "max"].includes(role)) {
    return;
  }

  const servo = getServo(Number(card.dataset.servo));
  syncServoFromCard(servo, card, role);
  updateServoCard(card, servo);
  if (state.connected) {
    if (role === "min" || role === "max") {
      sendServoLimit(servo);
    } else {
      moveServo(servo.id, servo.target);
    }
  }
}

function handleServoAction(event) {
  if (!state.connected) {
    return;
  }

  const action = event.target.dataset.action;
  const card = event.target.closest("[data-servo]");
  if (!["position-move", "time-move", "read-status"].includes(action) || !card) {
    return;
  }

  const servo = getServo(Number(card.dataset.servo));
  if (action === "read-status") {
    readServoStatus(servo.id);
    return;
  }

  syncServoFromCard(servo, card);
  updateServoCard(card, servo);
  if (action === "position-move") {
    moveServo(servo.id, servo.target);
  } else {
    moveServoOverTime(servo.id, servo.timeTarget, servo.durationMs, servo.steps, servo.profile, servo.easeStrengthPercent);
  }
}

function syncServoFromCard(servo, card, changedRole = "") {
  const targetInput = card.querySelector('[data-role="target"]');
  const slider = card.querySelector('[data-role="slider"]');
  const minInput = card.querySelector('[data-role="min"]');
  const maxInput = card.querySelector('[data-role="max"]');
  const timeTargetInput = card.querySelector('[data-role="time-target"]');
  const durationInput = card.querySelector('[data-role="duration"]');
  const stepsInput = card.querySelector('[data-role="steps"]');
  const profileInput = card.querySelector('[data-role="profile"]');
  const easeStrengthInput = card.querySelector('[data-role="ease-strength"]');
  const easeStrengthNumberInput = card.querySelector('[data-role="ease-strength-number"]');
  const autoSmoothInput = card.querySelector('[data-role="auto-smooth"]');
  const autoUsInput = card.querySelector('[data-role="auto-us"]');

  const normalized = normalizeLimit(minInput.value, maxInput.value);
  servo.min = normalized.min;
  servo.max = normalized.max;
  servo.autoSmooth = autoSmoothInput.checked;
  servo.targetPulsePerFrameUs = clamp(Number(autoUsInput.value), SERVO_AUTO_MIN_US_PER_FRAME, SERVO_AUTO_MAX_US_PER_FRAME);
  servo.profile = normalizeMotionProfile(profileInput.value);
  const strengthSource = changedRole === "ease-strength" ? easeStrengthInput.value : easeStrengthNumberInput.value;
  servo.easeStrengthPercent = normalizeEaseStrengthPercent(strengthSource);

  if (!["time-target", "duration", "steps", "profile", "ease-strength", "ease-strength-number", "auto-smooth", "auto-us"].includes(changedRole)) {
    const sourceValue = changedRole === "slider" ? slider.value : targetInput.value;
    servo.target = clamp(Math.round(Number(sourceValue)), servo.min, servo.max);
  }
  servo.timeTarget = clamp(Math.round(Number(timeTargetInput.value)), servo.min, servo.max);
  if (servo.autoSmooth) {
    applyAutoSmoothPlan(servo);
  } else {
    servo.durationMs = clamp(Math.round(Number(durationInput.value)), 1, 60000);
    servo.steps = clamp(Math.round(Number(stepsInput.value)), 1, SERVO_MAX_TIME_STEPS);
  }

  slider.min = servo.min;
  slider.max = servo.max;
  slider.value = servo.target;
  targetInput.min = servo.min;
  targetInput.max = servo.max;
  targetInput.value = servo.target;
  timeTargetInput.min = servo.min;
  timeTargetInput.max = servo.max;
  timeTargetInput.value = servo.timeTarget;
  durationInput.value = servo.durationMs;
  durationInput.readOnly = servo.autoSmooth;
  stepsInput.value = servo.steps;
  stepsInput.readOnly = servo.autoSmooth;
  autoSmoothInput.checked = servo.autoSmooth;
  autoUsInput.value = formatMetricNumber(servo.targetPulsePerFrameUs);
  profileInput.value = servo.profile;
  easeStrengthInput.value = servo.easeStrengthPercent;
  easeStrengthNumberInput.value = servo.easeStrengthPercent;
  const strengthRow = card.querySelector(".ease-strength-row");
  if (strengthRow) {
    strengthRow.classList.toggle("is-disabled", servo.profile === "linear");
  }
  card.classList.toggle("auto-smooth-enabled", servo.autoSmooth);
}

function updateServoCard(card, servo) {
  card.querySelector('[data-role="angle"]').textContent = servo.target;
  card.querySelector('[data-role="pulse"]').textContent = `${angleToPulseUs(servo.target)} us`;
  updateServoFeedbackCard(card, servo);
  updateAutoSmoothPlanText(card, servo);
  renderMotionCurve(card, servo);
  updatePosePreview();
}

function applyAutoSmoothPlan(servo) {
  const plan = planAutoSmoothMotion({
    startAngle: servo.target,
    targetAngle: servo.timeTarget,
    targetPulsePerFrameUs: servo.targetPulsePerFrameUs,
    profile: servo.profile,
    easeStrengthPercent: servo.easeStrengthPercent,
  });
  servo.durationMs = plan.durationMs;
  servo.steps = plan.steps;
  servo.targetPulsePerFrameUs = plan.targetPulsePerFrameUs;
}

function updateAutoSmoothPlanText(card, servo) {
  const autoPlan = card.querySelector('[data-role="auto-plan"]');
  if (!autoPlan) {
    return;
  }
  if (!servo.autoSmooth) {
    autoPlan.textContent = "手动模式：到达时间和步数由输入框决定";
    return;
  }

  const plan = planAutoSmoothMotion({
    startAngle: servo.target,
    targetAngle: servo.timeTarget,
    targetPulsePerFrameUs: servo.targetPulsePerFrameUs,
    profile: servo.profile,
    easeStrengthPercent: servo.easeStrengthPercent,
  });
  if (plan.deltaPulseUs === 0) {
    autoPlan.textContent = "目标与当前位置一致";
    return;
  }
  autoPlan.textContent =
    `${plan.deltaPulseUs} us，峰值 ${formatMetricNumber(plan.expectedPeakPulsePerFrameUs)} us/帧，推荐 ${plan.durationMs} ms`;
}

function renderAllMotionCurves() {
  state.servos.forEach((servo) => {
    const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
    if (card) {
      renderMotionCurve(card, servo);
    }
  });
}

function renderMotionCurve(card, servo) {
  const panel = card.querySelector('[data-role="curve-panel"]');
  const path = card.querySelector('[data-role="curve-path"]');
  const stepLayer = card.querySelector('[data-role="curve-steps"]');
  const dot = card.querySelector('[data-role="curve-dot"]');
  const summary = card.querySelector('[data-role="curve-summary"]');
  const startLabel = card.querySelector('[data-role="curve-start"]');
  const endLabel = card.querySelector('[data-role="curve-end"]');
  const metrics = card.querySelector('[data-role="resolution-metrics"]');
  const framesMetric = card.querySelector('[data-metric="frames"]');
  const pulseStepMetric = card.querySelector('[data-metric="pulse-step"]');
  const deadbandMetric = card.querySelector('[data-metric="deadband"]');
  const resolutionNote = card.querySelector('[data-role="resolution-note"]');
  if (!panel || !path || !stepLayer || !dot || !summary || !startLabel || !endLabel) {
    return;
  }

  const now = performance.now();
  const active = servo.activeMotion && now - servo.activeMotion.startedAt <= servo.activeMotion.durationMs;
  if (!active) {
    updateAutoSmoothPlanText(card, servo);
  }
  const plan = active
    ? servo.activeMotion
    : {
        startAngle: servo.target,
        targetAngle: servo.timeTarget,
        durationMs: servo.durationMs,
        steps: servo.steps,
        profile: servo.profile,
        easeStrengthPercent: servo.easeStrengthPercent,
      };
  const points = buildMotionCurve({
    startAngle: plan.startAngle,
    targetAngle: plan.targetAngle,
    durationMs: plan.durationMs,
    profile: plan.profile,
    easeStrengthPercent: plan.easeStrengthPercent,
    samples: 72,
  });
  const angles = points.map((point) => point.angle);
  const minAngle = Math.min(...angles, plan.startAngle, plan.targetAngle) - 2;
  const maxAngle = Math.max(...angles, plan.startAngle, plan.targetAngle) + 2;
  const xMin = 38;
  const xMax = 496;
  const yMin = 16;
  const yMax = 146;
  const yRange = Math.max(1, maxAngle - minAngle);

  const toX = (elapsedMs) => xMin + (elapsedMs / plan.durationMs) * (xMax - xMin);
  const toY = (angle) => yMax - ((angle - minAngle) / yRange) * (yMax - yMin);
  path.setAttribute(
    "d",
    points
      .map((point, index) => `${index === 0 ? "M" : "L"} ${toX(point.elapsedMs).toFixed(1)} ${toY(point.angle).toFixed(1)}`)
      .join(" "),
  );

  const resolution = estimateServoMotionResolution(plan);
  const stepPoints = buildMotionFrameSteps(plan);
  const stepRadius = resolution.effectiveSteps <= 90 ? 2 : resolution.effectiveSteps <= 180 ? 1.55 : 1.05;
  const stepSignature = [
    Math.round(plan.startAngle * 10),
    Math.round(plan.targetAngle * 10),
    plan.durationMs,
    plan.steps,
    plan.profile,
    plan.easeStrengthPercent,
    resolution.effectiveSteps,
    Math.round(minAngle * 10),
    Math.round(maxAngle * 10),
  ].join(":");

  if (stepLayer.dataset.signature !== stepSignature) {
    stepLayer.innerHTML = stepPoints
      .map((point, index) => {
        const isEndpoint = index === 0 || index === stepPoints.length - 1;
        return `<circle class="${isEndpoint ? "is-endpoint" : ""}" cx="${toX(point.elapsedMs).toFixed(1)}" cy="${toY(point.angle).toFixed(1)}" r="${isEndpoint ? 3 : stepRadius}"></circle>`;
      })
      .join("");
    stepLayer.dataset.signature = stepSignature;
  }

  const elapsedMs = active ? clamp(now - plan.startedAt, 0, plan.durationMs) : 0;
  const dotAngle = active ? motionPointAt(plan, elapsedMs) : plan.startAngle;
  dot.setAttribute("cx", toX(elapsedMs).toFixed(1));
  dot.setAttribute("cy", toY(dotAngle).toFixed(1));
  dot.style.opacity = active ? "1" : "0.5";

  panel.classList.toggle("is-running", Boolean(active));
  summary.textContent = active
    ? `执行中 ${Math.round(elapsedMs)} / ${plan.durationMs} ms`
    : `${profileLabel(plan.profile, plan.easeStrengthPercent)}，${Math.round(plan.startAngle)} -> ${Math.round(plan.targetAngle)} 度，${plan.durationMs} ms，${resolution.effectiveSteps} 帧 / ${stepPoints.length} 点`;
  startLabel.textContent = `${Math.round(plan.startAngle)} 度`;
  endLabel.textContent = `${Math.round(plan.targetAngle)} 度`;
  if (metrics && framesMetric && pulseStepMetric && deadbandMetric && resolutionNote) {
    metrics.dataset.level = resolution.status.level;
    framesMetric.textContent = `有效帧 ${resolution.effectiveSteps}`;
    pulseStepMetric.textContent = `峰值 ${formatMetricNumber(resolution.peakPulsePerFrameUs)} us`;
    deadbandMetric.textContent = `死区 ${resolution.status.label}`;
    resolutionNote.textContent =
      `${resolution.startPulseUs} -> ${resolution.targetPulseUs} us，平均 ${formatMetricNumber(resolution.pulsePerStepUs)} us/帧，更新 ${resolution.effectivePeriodMs} ms，${resolution.status.detail}`;
  }
}

function startMotionCurveAnimation() {
  if (state.motionAnimationFrame !== 0) {
    cancelAnimationFrame(state.motionAnimationFrame);
  }

  const tick = () => {
    let hasActiveMotion = false;
    const now = performance.now();
    state.servos.forEach((servo) => {
      const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
      if (!card || !servo.activeMotion) {
        return;
      }
      if (now - servo.activeMotion.startedAt > servo.activeMotion.durationMs) {
        servo.activeMotion = null;
      } else {
        hasActiveMotion = true;
      }
      renderMotionCurve(card, servo);
    });

    state.motionAnimationFrame = hasActiveMotion ? requestAnimationFrame(tick) : 0;
  };

  state.motionAnimationFrame = requestAnimationFrame(tick);
}

function profileLabel(profile, easeStrengthPercent = SERVO_EASE_STRENGTH_DEFAULT_PERCENT) {
  return profile === "linear" ? "线性" : `缓入缓出 ${normalizeEaseStrengthPercent(easeStrengthPercent)}%`;
}

function formatMetricNumber(value) {
  if (!Number.isFinite(value)) {
    return "--";
  }
  if (value >= 10) {
    return Math.round(value).toString();
  }
  return value.toFixed(1);
}

function moveServo(servoId, angle) {
  const servo = getServo(servoId);
  const target = clamp(Math.round(angle), servo.min, servo.max);
  clearScheduledMotion(servo);
  sendCli(cliSetPositionCommand({ servoId, angle: target }));
  servo.activeMotion = null;
  servo.angle = target;
  servo.commandAngle = target;
  servo.target = target;
  updatePosePreview();
  scheduleServoStatusRefresh(servoId);
}

function moveServoOverTime(servoId, angle, durationMs, steps, profile, easeStrengthPercent) {
  const servo = getServo(servoId);
  const startAngle = servo.target;
  const target = clamp(Math.round(angle), servo.min, servo.max);
  const safeDurationMs = clamp(Math.round(Number(durationMs)), 1, 60000);
  const safeSteps = clamp(Math.round(Number(steps)), 1, SERVO_MAX_TIME_STEPS);
  const safeProfile = normalizeMotionProfile(profile);
  const safeEaseStrengthPercent = normalizeEaseStrengthPercent(easeStrengthPercent);
  clearScheduledMotion(servo);
  sendTimedServoCommand({
    servo,
    servoId,
    startAngle,
    target,
    durationMs: safeDurationMs,
    steps: safeSteps,
    profile: safeProfile,
    easeStrengthPercent: safeEaseStrengthPercent,
  });
  servo.activeMotion = {
    startedAt: performance.now(),
    startAngle,
    targetAngle: target,
    durationMs: safeDurationMs,
    steps: safeSteps,
    profile: safeProfile,
    easeStrengthPercent: safeEaseStrengthPercent,
  };
  servo.angle = target;
  servo.commandAngle = target;
  servo.target = target;
  servo.timeTarget = target;
  servo.durationMs = safeDurationMs;
  servo.steps = safeSteps;
  servo.profile = safeProfile;
  servo.easeStrengthPercent = safeEaseStrengthPercent;
  updatePosePreview();
  scheduleServoStatusRefresh(servoId, safeDurationMs + 80);
  const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
  if (card) {
    renderMotionCurve(card, servo);
  }
  startMotionCurveAnimation();
}

function sendTimedServoCommand({ servo, servoId, startAngle, target, durationMs, steps, profile, easeStrengthPercent }) {
  void servo;
  void startAngle;
  sendCli(cliMoveTimeCommand({ servoId, angle: target, durationMs, steps, profile, easeStrengthPercent }));
}

function sendServoLimit(servo) {
  servo.limitPendingUntil = performance.now() + 1500;
  sendCli(cliLimitCommand({ servoId: servo.id, min: servo.min, max: servo.max }));
  scheduleServoStatusRefresh(servo.id, 160);
}

function clearScheduledMotion(servo) {
  servo.scheduledMotionTimers.forEach((timerId) => window.clearTimeout(timerId));
  servo.scheduledMotionTimers = [];
}

function centerAllServos() {
  state.servos.forEach((servo) => {
    const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
    servo.target = clamp(90, servo.min, servo.max);
    servo.timeTarget = servo.target;
    if (servo.autoSmooth) {
      applyAutoSmoothPlan(servo);
    }
    if (card) {
      card.querySelector('[data-role="target"]').value = servo.target;
      card.querySelector('[data-role="time-target"]').value = servo.timeTarget;
      card.querySelector('[data-role="duration"]').value = servo.durationMs;
      card.querySelector('[data-role="steps"]').value = servo.steps;
      card.querySelector('[data-role="auto-smooth"]').checked = servo.autoSmooth;
      card.querySelector('[data-role="auto-us"]').value = formatMetricNumber(servo.targetPulsePerFrameUs);
      card.querySelector('[data-role="ease-strength"]').value = servo.easeStrengthPercent;
      card.querySelector('[data-role="ease-strength-number"]').value = servo.easeStrengthPercent;
      card.querySelector('[data-role="slider"]').value = servo.target;
      updateServoCard(card, servo);
    }
    moveServo(servo.id, servo.target);
  });
}

async function readAllServoStatus() {
  for (const servo of state.servos) {
    await readServoStatus(servo.id);
  }
}

async function readServoStatus(servoId) {
  await sendCli(cliStatusCommand({ servoId }));
}

function scheduleServoStatusRefresh(servoId, delayMs = 120) {
  if (!state.connected) {
    return;
  }
  window.setTimeout(() => readServoStatus(servoId), delayMs);
}

async function setFeedbackPolling(enabled) {
  if (!state.connected) {
    state.feedbackPolling = false;
    elements.autoRefreshServoStatus.checked = false;
    stopFeedbackPollingTimer();
    return;
  }

  if (state.feedbackToggleBusy) {
    elements.autoRefreshServoStatus.checked = state.feedbackPolling;
    return;
  }

  const shouldEnable = Boolean(enabled);
  state.feedbackToggleBusy = true;

  try {
    if (shouldEnable) {
      state.feedbackPolling = true;
      elements.autoRefreshServoStatus.checked = true;
      updateConnectionState();
      await releaseAllServosForFeedback();
      updateFeedbackPolling();
    } else {
      state.feedbackPolling = false;
      elements.autoRefreshServoStatus.checked = false;
      stopFeedbackPollingTimer();
      updateConnectionState();
      await lockAllServosFromFeedback();
      renderFeedbackChart();
    }
  } finally {
    state.feedbackToggleBusy = false;
    updateConnectionState();
  }
}

function updateFeedbackPolling() {
  stopFeedbackPollingTimer();
  if (!state.connected || !state.feedbackPolling) {
    return;
  }
  pollFeedbackStatusNow();
  state.feedbackPollTimer = window.setInterval(pollFeedbackStatusNow, FEEDBACK_STATUS_POLL_INTERVAL_MS);
  startFeedbackChartTimer();
}

function pollFeedbackStatusNow() {
  if (!state.connected || !state.feedbackPolling || state.feedbackPollInFlight) {
    return;
  }

  state.feedbackPollInFlight = true;
  readAllServoStatus().finally(() => {
    state.feedbackPollInFlight = false;
  });
}

function stopFeedbackPollingTimer() {
  if (state.feedbackPollTimer !== 0) {
    window.clearInterval(state.feedbackPollTimer);
    state.feedbackPollTimer = 0;
  }
  state.feedbackPollInFlight = false;
  stopFeedbackChartTimer();
}

function startFeedbackChartTimer() {
  stopFeedbackChartTimer();
  state.feedbackChartTimer = window.setInterval(renderFeedbackChart, FEEDBACK_CHART_RENDER_INTERVAL_MS);
}

function stopFeedbackChartTimer() {
  if (state.feedbackChartTimer !== 0) {
    window.clearInterval(state.feedbackChartTimer);
    state.feedbackChartTimer = 0;
  }
}

function clearFeedbackChart() {
  state.servos.forEach((servo) => {
    servo.feedbackSamples = [];
  });
  renderFeedbackChart();
}

async function releaseAllServosForFeedback() {
  writeLog("开启反馈：释放两个舵机 PWM，开始读取反馈角度。", "system");
  for (const servo of state.servos) {
    clearScheduledMotion(servo);
    servo.activeMotion = null;
    await sendCli(cliServoOffCommand({ servoId: servo.id }));
  }
}

async function lockAllServosFromFeedback() {
  writeLog("关闭反馈：按最近反馈角锁定两个舵机。", "system");
  for (const servo of state.servos) {
    const lockAngle = feedbackLockAngle(servo);
    clearScheduledMotion(servo);
    servo.activeMotion = null;
    servo.angle = lockAngle;
    servo.commandAngle = lockAngle;
    servo.target = lockAngle;
    servo.timeTarget = lockAngle;
    if (servo.autoSmooth) {
      applyAutoSmoothPlan(servo);
    }
    syncServoCardFromState(servo);
    await sendCli(cliSetPositionCommand({ servoId: servo.id, angle: lockAngle }));
  }
  updatePosePreview();
}

function feedbackLockAngle(servo) {
  const feedbackAngle =
    servo.feedback?.valid && Number.isFinite(servo.feedback.angle) ? Math.round(servo.feedback.angle) : servo.target;
  return clamp(feedbackAngle, servo.min, servo.max);
}

function syncServoCardFromState(servo) {
  const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
  if (!card) {
    return;
  }
  card.querySelector('[data-role="slider"]').value = servo.target;
  card.querySelector('[data-role="target"]').value = servo.target;
  card.querySelector('[data-role="time-target"]').value = servo.timeTarget;
  card.querySelector('[data-role="duration"]').value = servo.durationMs;
  card.querySelector('[data-role="steps"]').value = servo.steps;
  card.querySelector('[data-role="auto-smooth"]').checked = servo.autoSmooth;
  card.querySelector('[data-role="auto-us"]').value = formatMetricNumber(servo.targetPulsePerFrameUs);
  card.querySelector('[data-role="ease-strength"]').value = servo.easeStrengthPercent;
  card.querySelector('[data-role="ease-strength-number"]').value = servo.easeStrengthPercent;
  updateServoCard(card, servo);
}

function updatePosePreview() {
  const yServo = getServo(1);
  const xServo = getServo(2);
  const xAngle = poseAngleForServo(xServo);
  const yAngle = poseAngleForServo(yServo);
  const xRatio = (xAngle - xServo.min) / Math.max(1, xServo.max - xServo.min);
  const yRatio = (yAngle - yServo.min) / Math.max(1, yServo.max - yServo.min);
  elements.poseDot.style.left = `${10 + xRatio * 80}%`;
  elements.poseDot.style.top = `${50 - yRatio * 40}%`;
  elements.poseXValue.textContent = formatPoseReadout(xServo);
  elements.poseYValue.textContent = formatPoseReadout(yServo);
}

function poseAngleForServo(servo) {
  return servo.feedback?.valid && Number.isFinite(servo.feedback.angle) ? servo.feedback.angle : servo.target;
}

function formatPoseReadout(servo) {
  if (servo.feedback?.valid && Number.isFinite(servo.feedback.angle)) {
    const commandAngle = Number.isFinite(servo.commandAngle) ? servo.commandAngle : servo.target;
    return state.feedbackPolling
      ? `反馈 ${servo.feedback.angle} deg / 命令 ${commandAngle} deg`
      : `命令 ${commandAngle} deg / 反馈 ${servo.feedback.angle} deg`;
  }
  return `命令 ${servo.target} 度`;
}

function handleLightInput(event) {
  const panel = event.target.closest("[data-light-panel]");
  if (panel) {
    syncLightFromPanel(panel.dataset.lightPanel);
  }
}

async function handleLightAction(event) {
  if (!state.connected) {
    return;
  }

  const action = event.currentTarget.dataset.lightAction;
  const panelKey = event.currentTarget.dataset.lightPanel ?? event.currentTarget.closest("[data-light-panel]")?.dataset.lightPanel;
  syncLightFromPanel(panelKey);

  if (action === "rgb") {
    await sendLightRgb(panelKey);
  } else if (action === "preset") {
    await sendLightPreset(panelKey, event.currentTarget.dataset.value);
  } else if (action === "effect") {
    await sendLightEffect(panelKey, event.currentTarget.dataset.value);
  }
}

function syncLightFromPanel(panelKey) {
  const light = state.lights[panelKey];
  const panel = document.querySelector(`[data-light-panel="${panelKey}"]`);
  if (!light || !panel) {
    return;
  }

  const countInput = panel.querySelector('[data-light-role="count"]');
  const colorInput = panel.querySelector('[data-light-role="color"]');
  light.count = clamp(Math.round(Number(countInput.value)), 1, light.max);
  light.color = colorInput.value;
  countInput.value = light.count;
}

async function sendLightCount(light) {
  await sendCli(cliLedCountCommand({ target: light.target, count: light.count }));
}

async function sendLightRgb(panelKey) {
  const light = state.lights[panelKey];
  const { red, green, blue } = parseColor(light.color);
  await sendLightCount(light);
  await sendCli(cliLedCommand({ target: light.target, command: `rgb ${red} ${green} ${blue}` }));
}

async function sendLightPreset(panelKey, preset) {
  const light = state.lights[panelKey];
  await sendLightCount(light);
  await sendCli(cliLedCommand({ target: light.target, command: preset }));
}

async function sendLightEffect(panelKey, effect) {
  const light = state.lights[panelKey];
  const { red, green, blue } = parseColor(light.color);
  await sendLightCount(light);
  if (effect === "breathe") {
    await sendCli(cliLedCommand({ target: light.target, command: `breathe rgb ${red} ${green} ${blue}` }));
  }
}

async function sendManualCommand() {
  if (!state.connected) {
    writeLog("未连接：请先连接 STM32 串口。", "error");
    return;
  }
  const command = elements.manualCommand.value.trim();
  if (!command) {
    return;
  }
  pushManualCommandHistory(command);
  await sendCli(command);
  elements.manualCommand.value = "";
}

function pushManualCommandHistory(command) {
  if (state.commandHistory.at(-1) !== command) {
    state.commandHistory.push(command);
  }
  if (state.commandHistory.length > 80) {
    state.commandHistory.shift();
  }
  state.commandHistoryIndex = state.commandHistory.length;
  state.draftManualCommand = "";
}

function recallManualCommand(direction) {
  if (state.commandHistory.length === 0) {
    return;
  }
  if (state.commandHistoryIndex === state.commandHistory.length) {
    state.draftManualCommand = elements.manualCommand.value;
  }
  const nextIndex = clamp(state.commandHistoryIndex + direction, 0, state.commandHistory.length);
  state.commandHistoryIndex = nextIndex;
  elements.manualCommand.value = nextIndex === state.commandHistory.length ? state.draftManualCommand : state.commandHistory[nextIndex];
}

async function sendCli(command) {
  await sendBytes(encoder.encode(`${command}\r\n`), `> ${command}`);
}

async function sendBytes(bytes, logLine) {
  if (!state.connected || !state.writer) {
    writeLog(`未连接：${logLine}`, "error");
    return;
  }
  try {
    await state.writer.write(bytes);
    writeLog(logLine, "tx");
  } catch (error) {
    writeLog(`写入失败：${error.message}`, "error");
  }
}

function getServo(servoId) {
  return state.servos.find((servo) => servo.id === servoId);
}

function parseColor(value) {
  const hex = value.replace("#", "");
  return {
    red: parseInt(hex.slice(0, 2), 16),
    green: parseInt(hex.slice(2, 4), 16),
    blue: parseInt(hex.slice(4, 6), 16),
  };
}

function writeLog(message, kind) {
  const text = String(message).trimEnd();
  if (!text) {
    return;
  }
  state.log.push({ time: new Date().toLocaleTimeString(), kind, text });
  if (state.log.length > 320) {
    state.log.shift();
  }
  renderLog();
}

function processIncomingText(text) {
  writeLog(text, "rx");
  state.rxTextBuffer = `${state.rxTextBuffer}${text}`.slice(-12000);
  consumeServoStatusBlocks();
}

function consumeServoStatusBlocks() {
  const blockPattern = /={5,}\s*Servo Status\s*={5,}[\s\S]*?={10,}/g;
  let consumedUntil = 0;
  let match;

  while ((match = blockPattern.exec(state.rxTextBuffer)) !== null) {
    const parsed = parseServoStatusBlock(match[0]);
    if (parsed) {
      applyServoStatus(parsed);
    }
    consumedUntil = Math.max(consumedUntil, match.index + match[0].length);
  }

  if (consumedUntil > 0) {
    state.rxTextBuffer = state.rxTextBuffer.slice(consumedUntil);
  }
}

function applyServoStatus(status) {
  const servo = getServo(status.servo);
  if (!servo) {
    return;
  }

  const statusMatchesPendingLimit =
    status.limitMin === servo.min &&
    status.limitMax === servo.max;
  const mayApplyStatusLimit =
    Number.isFinite(status.limitMin) &&
    Number.isFinite(status.limitMax) &&
    (performance.now() >= servo.limitPendingUntil || statusMatchesPendingLimit);

  if (mayApplyStatusLimit) {
    servo.min = status.limitMin;
    servo.max = status.limitMax;
    servo.limitPendingUntil = 0;
  }
  const servoStatusView = resolveServoStatusView(
    servo,
    {
      ...status,
      limitMin: mayApplyStatusLimit ? status.limitMin : servo.min,
      limitMax: mayApplyStatusLimit ? status.limitMax : servo.max,
    },
    { feedbackPolling: state.feedbackPolling },
  );
  servo.min = servoStatusView.min;
  servo.max = servoStatusView.max;
  servo.commandAngle = servoStatusView.commandAngle;
  servo.target = servoStatusView.displayAngle;
  servo.timeTarget = servoStatusView.timeTarget;
  if (servoStatusView.tracksFeedback && servo.autoSmooth) {
    applyAutoSmoothPlan(servo);
  }
  servo.feedback = {
    valid: Boolean(status.feedbackValid),
    angle: status.feedbackAngle,
    raw: status.feedbackRaw,
    mv: status.feedbackMv,
    motion: status.motion,
    signal: status.signal,
    pulseUs: status.pulseUs,
    targetPulseUs: status.targetPulseUs,
    updatedAt: new Date(),
  };
  if (servo.feedback.valid) {
    servo.feedbackSamples = appendFeedbackSample(
      servo.feedbackSamples,
      {
        angle: servo.feedback.angle,
        raw: servo.feedback.raw,
        mv: servo.feedback.mv,
      },
      { nowMs: Date.now() },
    );
    renderFeedbackChart();
  }

  const card = elements.servoGrid.querySelector(`[data-servo="${servo.id}"]`);
  if (card) {
    const targetInput = card.querySelector('[data-role="target"]');
    const slider = card.querySelector('[data-role="slider"]');
    const minInput = card.querySelector('[data-role="min"]');
    const maxInput = card.querySelector('[data-role="max"]');
    targetInput.value = servo.target;
    const timeTargetInput = card.querySelector('[data-role="time-target"]');
    slider.min = servo.min;
    slider.max = servo.max;
    slider.value = servo.target;
    if (servoStatusView.tracksFeedback && timeTargetInput) {
      timeTargetInput.value = servo.timeTarget;
    }
    minInput.value = servo.min;
    maxInput.value = servo.max;
    updateServoCard(card, servo);
  }
}

function renderFeedbackChart() {
  if (!elements.feedbackChart) {
    return;
  }

  const nowMs = Date.now();
  const model = buildFeedbackChartModel(
    [
      {
        id: "y",
        label: "Y servo 1",
        color: "#12745a",
        samples: getServo(1)?.feedbackSamples ?? [],
      },
      {
        id: "x",
        label: "X servo 2",
        color: "#1664a7",
        samples: getServo(2)?.feedbackSamples ?? [],
      },
    ],
    {
      nowMs,
      windowMs: FEEDBACK_CHART_WINDOW_MS,
      minAngle: 0,
      maxAngle: 180,
    },
  );
  const yTicks = [0, 45, 90, 135, 180];
  const xTicks = [30, 20, 10, 0];
  const plotLeft = model.padding.left;
  const plotRight = model.width - model.padding.right;
  const plotTop = model.padding.top;
  const plotBottom = model.height - model.padding.bottom;
  const toY = (angle) => plotTop + (1 - (angle - model.yMin) / (model.yMax - model.yMin)) * model.plotHeight;
  const toXSeconds = (secondsAgo) => plotRight - (secondsAgo / (model.windowMs / 1000)) * model.plotWidth;
  const allSamples = model.series.reduce((total, item) => total + item.samples.length, 0);
  const latest = latestFeedbackSummary();

  elements.feedbackChart.innerHTML = `
    <rect class="feedback-chart-bg" x="${plotLeft}" y="${plotTop}" width="${model.plotWidth}" height="${model.plotHeight}" rx="6"></rect>
    ${yTicks
      .map(
        (tick) => `
          <line class="feedback-chart-grid" x1="${plotLeft}" x2="${plotRight}" y1="${toY(tick).toFixed(1)}" y2="${toY(tick).toFixed(1)}"></line>
          <text class="feedback-chart-tick" x="${plotLeft - 10}" y="${(toY(tick) + 4).toFixed(1)}" text-anchor="end">${tick}</text>
        `,
      )
      .join("")}
    ${xTicks
      .map(
        (secondsAgo) => `
          <line class="feedback-chart-grid feedback-chart-grid-vertical" x1="${toXSeconds(secondsAgo).toFixed(1)}" x2="${toXSeconds(secondsAgo).toFixed(1)}" y1="${plotTop}" y2="${plotBottom}"></line>
          <text class="feedback-chart-tick" x="${toXSeconds(secondsAgo).toFixed(1)}" y="${model.height - 10}" text-anchor="middle">-${secondsAgo}s</text>
        `,
      )
      .join("")}
    <text class="feedback-chart-axis-label" x="${plotLeft}" y="13">angle deg</text>
    ${model.series
      .map(
        (item) => `<path class="feedback-chart-line feedback-chart-line-${item.id}" d="${item.path}" style="--line-color: ${item.color}"></path>`,
      )
      .join("")}
    ${allSamples === 0 ? `<text class="feedback-chart-empty" x="${model.width / 2}" y="${model.height / 2}" text-anchor="middle">开启反馈后绘制曲线</text>` : ""}
  `;
  elements.feedbackChartMeta.textContent =
    allSamples > 0 ? `${allSamples} 样本 / ${Math.round(FEEDBACK_CHART_WINDOW_MS / 1000)}s 窗口 / ${latest}` : "等待样本";
}

function latestFeedbackSummary() {
  return state.servos
    .map((servo) => {
      const latest = servo.feedbackSamples.at(-1);
      return latest ? `S${servo.id} ${formatMetricNumber(latest.angle)}deg` : null;
    })
    .filter(Boolean)
    .join(" | ");
}

function updateServoFeedbackCard(card, servo) {
  const feedback = servo.feedback;
  const setText = (name, value) => {
    const element = card.querySelector(`[data-feedback="${name}"]`);
    if (element) {
      element.textContent = value;
    }
  };

  if (!feedback) {
    setText("angle", "--");
    setText("raw", "--");
    setText("mv", "--");
    setText("state", "未读取");
    return;
  }

  setText("angle", feedback.valid && Number.isFinite(feedback.angle) ? `${feedback.angle} deg` : "--");
  setText("raw", feedback.valid && Number.isFinite(feedback.raw) ? String(feedback.raw) : "--");
  setText("mv", feedback.valid && Number.isFinite(feedback.mv) ? `${feedback.mv} mV` : "--");
  const signal = state.feedbackPolling && feedback.signal === "ACTIVE" ? "ACTIVE 未释放" : feedback.signal ?? "--";
  setText("state", `${feedback.motion ?? "--"} / ${signal}`);
}

function renderLog() {
  const kindLabels = {
    error: "错误",
    rx: "接收",
    system: "系统",
    tx: "发送",
  };
  elements.serialLog.textContent = state.log.map((entry) => `[${entry.time}] ${kindLabels[entry.kind] ?? entry.kind} ${entry.text}`).join("\n");
  elements.serialLog.scrollTop = elements.serialLog.scrollHeight;
}
