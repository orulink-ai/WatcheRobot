# STM32调试页面

这个工具是 WatcheRobot STM32 端的 Web Serial 调试页面。它只封装 STM32 固件已经暴露的原子 CLI 命令，用于快速验证硬件能力，不承担动作编排。

## 当前面板

- 串口连接与日志：默认 `USART1 CLI / 115200`，支持手动输入原始命令。
- 舵机位置控制：滑条或角度输入提交后发送 `servo <id> <angle>`，支持页面顶部读取 `servo_status <id>`。
- 时间基控制：每个舵机卡片可发送 `servo_move_time <id> <angle> <duration_ms> <steps> <profile> [ease_strength]`；STM32 端按 us 脉宽做非阻塞插值，页面可调整步数/分辨率和缓入缓出强度，并在计划曲线上显示每个 20ms 有效帧点。
- 自动细腻：默认按峰值 `4.2us/frame` 自动反推到达时间和 steps；可在 `3~8us/frame` 范围内调整峰值目标，也可关闭后手动输入到达时间与步数。
- 灯光控制：发送 `ws ...` 与 `ws_bottom ...`，支持 RGB、常用颜色、呼吸、彩虹、停止和关闭。

## 本地运行

```powershell
npm install
npm run dev -- --port 5174
```

从 workspace 根目录也可以运行：

```powershell
yarn stm32:web
```

然后用 Chromium 或 Edge 打开 `http://127.0.0.1:5174`。

## 验证

```powershell
npm test
npm run build
```
