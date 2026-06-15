# ESP32 TOUCH_EVENT 对接说明

> 日期：2026-05-01
> 分支：`codex/stm32-touch-upload`
> 用途：给 ESP32 侧确认 STM32 触摸上报已经具备实机信号与日志。

## 1. 范围

本说明只记录 STM32 本轮交付状态和 ESP32 对接所需事实。协议字段、消息号和完整语义以 `orulink-ai/WatcheRobot` 中公开的 STM32/ESP32 联调文档为准。

本轮 STM32 已完成：

- `PA4` 触摸输入接入 USART2 协处理器 runtime。
- `HELLO_RSP` 在 touch 启用时声明 touch capability 和 touch sensor bitmap。
- `TOUCH_EVENT` 通过 `USART2@921600` 上报给 ESP32。
- `USART1@115200` 打印 `STM32_OBS evt=touch_event` 观测日志。

本轮未做：

- ESP32 侧 `fondle_love` 触发表情逻辑。
- STM32 侧 long press / double tap 判定。

## 2. 事件口径

触摸硬件电平：

- `PA4 HIGH`：未触摸
- `PA4 LOW`：触摸中

STM32 上报的 `TOUCH_EVENT` payload 保持 6 字节：

```text
touch_id + event_code + ts_ms_le32
```

当前 STM32 只会上报：

- `event_code = 1`：press
- `event_code = 2`：release

`event_code = 3` 仍保留给后续 long press，本轮不会产生。

## 3. ESP32 侧建议处理

ESP32 当前主线已经能解析并打印 `MCU_OBS evt=touch_event`。如果要接入表情，建议只在 `event_code == 1` 时触发 `fondle_love`，`event_code == 2` 只更新触摸状态或记录日志，避免松手时重复打断当前行为。

建议 ESP32 侧注意：

- 对高频 `press/release` burst 做节流或状态门控。
- 如果正在 TTS / BLE feedback / action active，需要明确是否允许触摸表情打断当前状态。
- 如果 SD 动画包缺少 `fondle_love`，需要先确认动画资产和 behavior state 已打包。

## 4. 实机验收记录

本轮已刷写 STM32 Debug 固件并在 `<debug-port> @ 115200` 观察到触摸事件：

```text
STM32_OBS tick_ms=285104 evt=touch_event touch_id=0 code=1 timestamp_ms=285104
STM32_OBS tick_ms=285270 evt=touch_event touch_id=0 code=2 timestamp_ms=285270
```

CLI 读取也确认了 active-low 口径：

```text
PA4 State: HIGH (Not Touched)
Raw Value: 1

PA4 State: LOW (Touched)
Raw Value: 0
```

当前实机触摸输入存在一定抖动，STM32 侧默认去抖为 `30ms`。如果 ESP32 侧观察到密集 `press/release`，优先在 ESP32 触发表情处增加节流；必要时再回到 STM32 调大去抖窗口。

## 5. 已验证命令

```text
git diff --check
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug --output-on-failure
cmake --preset Debug -D CMAKE_MAKE_PROGRAM=<local-ninja>
cmake --build --preset Debug
openocd ... program build/Debug/watcheRobot_STM32.bin verify reset exit 0x08000000
```
