# watcheRobot_STM32

> 当前仓库代码仍处于 **pre-v2 本地外设 demo** 状态。
> 后续 STM32 协处理器路线、分支规划和任务拆解统一以 [Documents/STM32_v2文档入口.md](Documents/STM32_v2文档入口.md) 为入口。

## 1. 当前仓库状态

当前 `dev` 分支里，仓库仍然保留本地外设 demo，但 `USART2` 协处理器链路的 P1/P2 最小闭环已经进入代码主线。当前已提交内容主要包括：

- `TIM3_CH1 / TIM3_CH2` 双舵机驱动
- `I2C1` 下的 `BMI160 / QMC6309` 读取
- `USART1` 本地调试日志与 CLI
- `USART2@921600` 的 `RX DMA + IDLE`
- `User/Protocol/` 下的 `ring buffer / delimiter framing / COBS / CRC16 / frame codec / dispatch / tx builder`
- `HELLO_REQ -> ACK -> HELLO_RSP` 的最小系统类闭环
- `User/TestHost/` 下的 native host protocol tests
- `IP5306 KEY / IRQ` 的本地测试链路
- CubeMX 工程与 CMake 工程骨架

当前主线行为仍然是：

- 舵机上电后默认归中并保持静止，可通过 `USART1 CLI` 直接设置目标角度
- `WS2812` 灯带已接入当前主线，基于 `TIM1_CH1 + PA8 + DMA1_Channel2` 输出时序，可通过 `USART1 CLI` 做静态颜色、灯珠数量与呼吸效果调试
- 传感器在本地初始化并通过 `USART1` 命令读取
- `USART2` 已形成 `COBS + CRC16 + delimiter + frame parser + HELLO` 的最小闭环
- 舵机反馈链路尚未接入当前主线：`ADC/CubeMX` 反馈配置未完成，角度-电压映射待后续标定

这意味着：

- 当前代码现状可作为板级摸底和已有驱动参考
- 当前代码现状仍然**不是** `proto/v2.0.0-stm32-align-20260416` 的完整实现，motion / led / sensors / recovery 仍待后续阶段补齐

## 2. 阅读入口

如果你只是想快速找到“该看哪份说明”，建议按下面顺序进入：

- 项目级主线入口
  - [Documents/STM32_v2文档入口.md](Documents/STM32_v2文档入口.md)
- ESP32 对接交接
  - [Documents/ESP32_TOUCH_EVENT_HANDOFF.md](Documents/ESP32_TOUCH_EVENT_HANDOFF.md)
- 当前仓库结构说明
  - [Documents/项目结构说明.md](Documents/项目结构说明.md)
- 当前本地联调相关模块
  - [Servo 驱动 README](User/Device/servo/README.md)
  - [WS2812 驱动 README](User/Device/ws2812/README.md)
  - [IP5306 驱动 README](User/Device/ip5306/README.md)
  - [Sensors 集成 README](User/Device/sensors/README.md)
  - [BMI160 驱动 README](User/Device/bmi160/README.md)
  - [QMC6309 驱动 README](User/Device/qmc6309/README.md)
  - [Touch Sensor 驱动 README](User/Device/touch_sensor/README.md)

## 3. 当前本地外设补充

当前本地硬件与测试相关的关键引脚如下：

- `USART1`
  - `PA9` → `USART1_TX` → 本地 CLI / 调试输出
  - `PA10` → `USART1_RX` → 本地 CLI / 调试输入
  - 当前波特率为 `115200`
- `USART2`
  - `PA2` → `USART2_TX` → 身体板发送到 Watcher 头部板
  - `PA3` → `USART2_RX` → 身体板接收来自 Watcher 头部板
  - 当前波特率为 `921600`
  - 当前 `USART2_TX` 开启 DMA，通道为 `DMA1_Channel7`
  - 当前 `USART2_RX` 开启 DMA，通道为 `DMA1_Channel6`
  - 当前 `USART2` 已接入 `RX DMA + IDLE + COBS + CRC16 + frame parser`
- `GPIOA PIN4`
  - 触摸传感器输入
  - 当前为 active-low：`LOW` 表示触摸中，`HIGH` 表示未触摸
  - 当前已通过 `USART2` 上报 `TOUCH_EVENT`
- `GPIOB PIN12`
  - `IP5306_KEY` 控制脚
  - 当前用于驱动外部 `NMOS` 的 `Gate`
  - `PB12 = HIGH` 时 `NMOS` 导通，`KEY` 被拉到地
  - `PB12 = LOW` 时 `NMOS` 截止，`KEY` 断开
  - 当前只控制 STM32 侧舵机 / WS2812 LED 的 5V 外设电源，不控制 USB-C 供电的 ESP32 本体
- `GPIOA PIN1`
  - `IP5306_IRQ` 状态输入脚
  - 当前保持输入配置，并带内部下拉
  - 本轮提交未继续验证 IRQ 中断链路
- `GPIOA PIN8`
  - `WS2812` 数据输出脚
  - 当前复用为 `TIM1_CH1`
  - 时序发送通过 `DMA1_Channel2` 驱动

当前本地 CLI 中与舵机相关的命令包括：

- `servo <angle>`
  - 直接控制 1 号舵机转到指定角度，范围 `0~180`
- `servo <id> <angle>`
  - 直接控制指定编号舵机转到目标角度，当前 `id` 支持 `1/2`
- `servo_move_time <id> <angle> <ms> [steps] [linear|ease|anti_drop]`
  - 以 STM32 端非阻塞方式按 us 脉宽插值移动到目标角度
  - `steps` 会换算为更新周期，低于 20ms 的请求按 50Hz PWM 帧周期保护
- `servo_time <id> <angle> <ms> [steps] [linear|ease|anti_drop]`
  - `servo_move_time` 的兼容别名
- 上电启动后会打印 `=== STM32 CLI Ready ===`
  - 用于快速确认 `USART1` 串口链路与固件已启动

当前本地 CLI 中与 `WS2812` 相关的命令包括：

- `ws off`
  - 熄灭当前活动灯珠
- `ws red` / `ws green` / `ws blue` / `ws white`
  - 设置整条活动灯珠为固定颜色
- `ws rgb <r> <g> <b>`
  - 以 `0~255` 的 RGB 值设置整条活动灯珠颜色
- `ws count <n>`
  - 设置当前活动灯珠数量，范围由 `APP_WS2812_LED_COUNT` 上限约束
- `ws breathe red|green|blue|white`
  - 以预设颜色启动整条灯带呼吸效果
- `ws breathe rgb <r> <g> <b>`
  - 以自定义 RGB 颜色启动整条灯带呼吸效果
- `ws stop`
  - 停止呼吸效果，保留当前灯带显示状态

当前本地 CLI 中与 IP5306 相关的命令包括：

- `ip5306_irq`
  - 读取 `PA1` 当前电平、待处理标志、累计中断次数和最近一次触发时间
- `ip5306_irq_clear`
  - 清除软件侧记录的 IRQ 待处理标志
- `ip5306_on`
  - 将 `PB12` 拉高约 `100ms`，通过外部 `NMOS` 模拟短按
  - 当前 Debug 固件会在 POWER enable 后延时 `600ms`，本地驱动两路舵机到观测角度，用于肉眼确认舵机 5V rail 已上电；这是 Debug 验证探针，不应混同为正式 App 固件默认动作
- `ip5306_off`
  - 在 `1s` 内连续两次将 `PB12` 拉高约 `100ms`，通过外部 `NMOS` 模拟双击短按
- `ip5306_double`
  - 在 `1s` 内连续两次将 `PB12` 拉高约 `100ms`，通过外部 `NMOS` 模拟双击短按
- `ip5306_long`
  - 将 `PB12` 拉高约 `2500ms`，通过外部 `NMOS` 模拟长按
- `ip5306_hold`
  - 将 `PB12` 持续拉高 `5000ms`，方便万用表验证 `Gate / KEY` 通路

当前电源测试判据：

- ESP32 由 USB-C 5V 独立供电，`ip5306_off` 后 ESP32 串口继续输出日志是符合预期的。
- IP5306 POWER 通路的有效负载是 STM32 侧舵机和 WS2812 LED 的 5V rail。
- 验证 `ip5306_on/off` 或协议侧 `POWER_5V_ENABLE/DISABLE` 时，应测舵机/LED 5V 或 IP5306 输出端，而不是用 ESP32 是否掉电作为判据。

如果你当前关注的是本地板级联调，而不是 v2 协处理器主线，建议优先查看：

- [Documents/2026-04-18-舵机串口控角与反馈待测记录.md](Documents/2026-04-18-舵机串口控角与反馈待测记录.md)
- [WS2812 驱动 README](User/Device/ws2812/README.md)
- [IP5306 驱动 README](User/Device/ip5306/README.md)
- [Documents/项目结构说明.md](Documents/项目结构说明.md)
- [Sensors 集成 README](User/Device/sensors/README.md)

## 4. 后续开发入口

后续 STM32 v2 开发统一从 `Documents/` 下的主线文档进入，不再从旧专题文档反推主线方向：

- [Documents/STM32_v2文档入口.md](Documents/STM32_v2文档入口.md)
  - 文档导航入口
- [Documents/上游协议基线.md](Documents/上游协议基线.md)
  - 固定的 upstream baseline 与引用规则
- [Documents/分支规划.md](Documents/分支规划.md)
  - 后续分支规划与 PR 口径
- [Documents/开发任务拆解.md](Documents/开发任务拆解.md)
  - 完整 v2 路线与阶段任务拆解
- [Documents/ESP32_TOUCH_EVENT_HANDOFF.md](Documents/ESP32_TOUCH_EVENT_HANDOFF.md)
  - 给 ESP32 侧的触摸事件对接说明、实机日志和后续建议

当前固定基线为：

```text
Protocol baseline: orulink-ai/WatcheRobot@proto/v2.0.0-stm32-align-20260416
```

本仓不再维护第二份独立协议字段表。协议字段、消息号、`ACK/NACK/FAULT` 语义、`HELLO_REQ -> ACK -> HELLO_RSP` 合同，一律以上游真源为准。

## 5. 文档分层

- 主线文档
  - `STM32_v2文档入口`
  - `上游协议基线`
  - `分支规划`
  - `开发任务拆解`
- 历史/本地专题文档
  - 项目结构说明
  - Servo 驱动 README
  - WS2812 驱动 README
  - IP5306 驱动 README
  - Sensors 集成 README
  - BMI160 驱动 README
  - QMC6309 驱动 README
  - Touch Sensor 驱动 README
  - 数字孪生场景下舵机运动控制问题

主线文档用于后续 STM32 v2 协处理器开发；历史/本地专题文档只用于理解当前仓库已提交现状、硬件资料和本地专题分析。

## 6. 历史文档说明

以下文档仍然保留，但需要按“历史/现状说明”理解，而不是按“后续主线规范”理解：

- [Documents/项目结构说明.md](Documents/项目结构说明.md)
- [Servo 驱动 README](User/Device/servo/README.md)
- [WS2812 驱动 README](User/Device/ws2812/README.md)
- [IP5306 驱动 README](User/Device/ip5306/README.md)
- [Sensors 集成 README](User/Device/sensors/README.md)
- [BMI160 驱动 README](User/Device/bmi160/README.md)
- [QMC6309 驱动 README](User/Device/qmc6309/README.md)
- [Touch Sensor 驱动 README](User/Device/touch_sensor/README.md)
- [Documents/2026-04-14-数字孪生场景下舵机运动控制问题.md](Documents/2026-04-14-数字孪生场景下舵机运动控制问题.md)

这些文档描述的是：

- 当前已提交旧实现
- 历史设计思路
- 本地硬件/外设参考信息

这些文档**不**描述：

- `proto/v2.0.0-stm32-align-20260416` 的协议真源
- v2 协处理器主线的分支顺序
- 后续 STM32 主线任务拆解

## 7. 当前仓库的最小事实

如果你只是快速了解当前代码状态，可以先记住这几条：

- MCU 目标仍是 `STM32F103C8Tx`
- 当前时钟配置为 `72MHz`
- `USART1` 仍承担本地调试日志和 CLI
- `USART2` 目标波特率固定为 `921600`
- 当前 `.ioc` 已开 `USART2_RX DMA + IDLE` bring-up，并启用了 `USART2_IRQn`
- 当前已经有 `COBS / CRC16 / frame / HELLO / ACK / NACK / FAULT` 的本地最小实现
- `READY`、`SNAPSHOT_REQ/RSP`、motion / led / sensors dispatcher 仍不在本轮范围

## 8. 本机入口

本机默认通过 Windows 工具链执行：

- firmware configure/build：`cmake --preset Debug` / `cmake --build --preset Debug`
- firmware flash：`.vscode/tasks.json` 中的 `flash: Debug via openocd`
- host tests：`cmake --preset HostDebug`、`cmake --build --preset HostDebug`、`ctest --preset HostDebug`
- dual-MCU bench：优先使用 `watche-dual-mcu-bringup` skill / wrapper，而不是手拼串口脚本

- 当前本地 CLI 已支持直接设置舵机角度
- 当前 `IP5306_KEY` 已切换为 `GPIO -> NMOS -> KEY` 控制方式
- 当前 `WS2812` 已接入 `TIM1_CH1 + PA8 + DMA1_Channel2`，并已提供静态设色、活动灯珠数量与呼吸灯命令
- 当前舵机反馈 ADC 尚未在 CubeMX 中配置
- 当前舵机反馈尚未完成实物测试与角度映射标定
- 当前 `WS2812` 代码链路已完成接入并通过本地构建验证，灯带硬件联调以接线后的实测为准

## 9. 后续开发原则

- STM32 仓只做实现，不做第二份协议真源
- 所有后续分支都从最新 `dev` 切出并回合到 `dev`
- 先打通 `USART2 + RX DMA + IDLE + HELLO`，再接 `motion / led / sensors`
- 所有跨仓联调都使用固定的 `Protocol baseline` 对齐
