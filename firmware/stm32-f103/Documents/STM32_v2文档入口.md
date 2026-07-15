# STM32 v2 文档入口

> 本文是 `watcherobot_stm32` 后续 STM32 协处理器路线的主文档入口。
> 当前固定引用：
>
> ```text
> Protocol baseline: Ro-In-AI/WatcheRobot-Firmware@proto/v2.0.0-stm32-align-20260416
> ```

## 1. 本文用途

`Documents/` 下的主线文档只承载后续 STM32 v2 开发口径，包括：

- upstream baseline 固定口径
- 本仓分支规划
- 后续阶段任务与开发顺序
- 本仓与上游协议真源的引用边界

本文不承载：

- 第二份独立协议字段表
- 与 upstream 冲突的本地消息定义
- 当前旧 demo 的实现细节说明

## 2. `Documents/` 内的文档分层

- 主线文档
  - [上游协议基线](./上游协议基线.md)
  - [分支规划](./分支规划.md)
  - [开发任务拆解](./开发任务拆解.md)
  - [ESP32 TOUCH_EVENT 对接说明](./ESP32_TOUCH_EVENT_HANDOFF.md)
- 历史文档
  - [项目结构说明](./项目结构说明.md)
  - [Servo 驱动 README](../User/Device/servo/README.md)
  - [WS2812 驱动 README](../User/Device/ws2812/README.md)
  - [IP5306 驱动 README](../User/Device/ip5306/README.md)
  - [Sensors 集成 README](../User/Device/sensors/README.md)
  - [BMI160 驱动 README](../User/Device/bmi160/README.md)
  - [QMC6309 驱动 README](../User/Device/qmc6309/README.md)
  - [Touch Sensor 驱动 README](../User/Device/touch_sensor/README.md)
  - [2026-04-14-数字孪生场景下舵机运动控制问题](./2026-04-14-数字孪生场景下舵机运动控制问题.md)

如果主线文档与历史文档出现口径冲突，以：

1. upstream protocol baseline
2. `Documents/` 下主线文档
3. `Documents/` 下历史文档

的优先级解释。

## 3. 建议阅读顺序

建议按以下顺序阅读：

1. [上游协议基线](./上游协议基线.md)
2. [分支规划](./分支规划.md)
3. [开发任务拆解](./开发任务拆解.md)
4. [ESP32 TOUCH_EVENT 对接说明](./ESP32_TOUCH_EVENT_HANDOFF.md)

## 4. 上游协议真源位置

当前固定引用的上游真源文件位于：

- `Ro-In-AI/WatcheRobot-Firmware`
- `firmware/integration_docs/STM32_UART_PROTOCOL.md`
- `firmware/integration_docs/STM32_COPROC_REFACTOR_PLAN.md`
- `firmware/integration_docs/IMPLEMENTATION_STATUS.md`
- `firmware/integration_docs/FIELD_REVIEW_TODO.md`

这些文件的本仓摘要与引用规则统一收口在 [上游协议基线](./上游协议基线.md)。

## 5. 本地最重要的约束

- 本仓不再维护第二份协议真源
- `ACK/NACK/FAULT`、消息号、flags、`HELLO_REQ -> ACK -> HELLO_RSP` 合同统一以上游协议真源为准
- 后续功能分支统一从 `dev` 切出并回合到 `dev`
- 文档中提到的“当前代码现状”只用于解释已有提交，不代表 v2 主线目标

## 6. 关联文档

- [上游协议基线](./上游协议基线.md)
  - 固定 baseline、commit 与上游真源文件列表
- [分支规划](./分支规划.md)
  - 分支命名、依赖顺序、进入/退出条件、PR 口径
- [开发任务拆解](./开发任务拆解.md)
  - 完整 v2 路线与阶段交付标准
- [ESP32 TOUCH_EVENT 对接说明](./ESP32_TOUCH_EVENT_HANDOFF.md)
  - STM32 触摸上报的 ESP32 对接事实、实机日志和后续建议

## 7. 关联的历史文档

如果需要理解当前仓库已经提交了什么，可以再回看以下历史文档：

- [项目结构说明](./项目结构说明.md)
- [Servo 驱动 README](../User/Device/servo/README.md)
- [WS2812 驱动 README](../User/Device/ws2812/README.md)
- [IP5306 驱动 README](../User/Device/ip5306/README.md)
- [Sensors 集成 README](../User/Device/sensors/README.md)
- [BMI160 驱动 README](../User/Device/bmi160/README.md)
- [QMC6309 驱动 README](../User/Device/qmc6309/README.md)
- [Touch Sensor 驱动 README](../User/Device/touch_sensor/README.md)
- [2026-04-14-数字孪生场景下舵机运动控制问题](./2026-04-14-数字孪生场景下舵机运动控制问题.md)

这些文档描述的是旧实现、当前现状或专题分析，不是 v2 协处理器主线规范。
