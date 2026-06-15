# WS2812 驱动 README

> **日期**: 2026-04-18
> **项目**: watcheRobot_STM32
> **主题**: WS2812 驱动接入、命令行调试口径与当前验证边界留档

## 1. 本轮目标

本轮目标覆盖以下内容：

- 在当前工程中接入 `WS2812` 驱动
- 通过 `CubeMX` 为 `WS2812` 生成定时器与 DMA 底座
- 在 `USART1 CLI` 中补齐静态颜色、灯珠数量与呼吸效果命令
- 完成本地构建验证，作为后续接线测试的固件基础

本轮**不**覆盖以下内容：

- 灯带上板后的长时间稳定性验证
- 电平转换方案的硬件可靠性评估
- 多段灯带、分组灯效或动画脚本系统

## 2. 当前配置口径

当前主线使用如下输出链路驱动 `WS2812`：

- 输出引脚：`PA8`
- 定时器通道：`TIM1_CH1`
- DMA 通道：`DMA1_Channel2`
- 定时器周期：`ARR = 89`
- 目标位时序：`800kHz`

当前实现中，`TIM1` 负责产生 PWM，占空比缓存由 DMA 逐个搬运到 `CCR1`，从而形成 `WS2812` 所需的高低电平时序。

## 3. 当前已完成内容

### 3.1 驱动层

当前本目录中的 `ws2812.c/.h` 已提供：

- `WS2812_Init()`
- `WS2812_SetLedCount()`
- `WS2812_SetPixel()`
- `WS2812_Fill()`
- `WS2812_Show()`
- `WS2812_IsBusy()`

实现特征：

- 采用 `TIM PWM + DMA` 发送，不使用阻塞式 bit-banging
- 发送缓存按灯珠数量与 reset 段长度动态组织
- 通过 `HAL_TIM_PWM_PulseFinishedCallback()` 收尾并释放 busy 状态

### 3.2 应用层

当前 `App` 层已经补齐：

- 固定颜色设置
- 活动灯珠数量设置
- 简单整条灯带呼吸效果
- 上电默认熄灭

当前配置项位于 `User/Config/app_config.h`，包括：

- `APP_WS2812_LED_COUNT`
- `APP_WS2812_DEFAULT_ACTIVE_LED_COUNT`
- `APP_WS2812_RESET_SLOTS`
- `APP_WS2812_BIT_0_PULSE`
- `APP_WS2812_BIT_1_PULSE`
- `APP_WS2812_SHOW_TIMEOUT_MS`
- `APP_WS2812_BREATHE_STEP`
- `APP_WS2812_BREATHE_INTERVAL_MS`

### 3.3 CLI 命令

当前 CLI 已支持：

- `ws off`
- `ws red`
- `ws green`
- `ws blue`
- `ws white`
- `ws rgb <r> <g> <b>`
- `ws count <n>`
- `ws breathe red|green|blue|white`
- `ws breathe rgb <r> <g> <b>`
- `ws stop`

这些命令统一走 `USART1 @ 115200 8N1`。

## 4. 当前未完成内容

本轮仍有以下边界：

- 还没有形成正式的灯带动画框架
- 还没有做逐灯独立寻址的上层 CLI
- 还没有完成灯带接线后的硬件实测记录

因此，本轮可以确认的是：

- `WS2812` 驱动代码已进入主线
- `TIM1/PA8/DMA1_Channel2` 的软件链路已经打通
- 当前工程可正常构建

本轮暂时不能确认的是：

- 用户当前手头灯珠对 `3.3V` 数据电平的容忍度
- 长线场景下是否需要 `74AHCT125 / 74HCT14` 一类电平转换
- 灯带接线完成后的颜色顺序是否与当前实现完全一致

## 5. 接线与后续测试建议

后续上板联调时，建议按以下最小口径接线：

- `PA8` -> `WS2812 DIN`
- `5V` -> `WS2812 VCC`
- `GND` -> `WS2812 GND`
- STM32 与灯带必须共地

建议补充：

- `PA8` 与 `DIN` 之间串联 `330Ω`
- 如果灯带对高电平较敏感，再补电平转换器

建议上板后按下面顺序验证：

1. `ws red`
2. `ws green`
3. `ws blue`
4. `ws white`
5. `ws count 8`
6. `ws breathe blue`
7. `ws stop`

## 6. 与舵机反馈链路的关系

本轮 `WS2812` 接入不改变此前的结论：

- 舵机反馈 ADC 仍未进入当前主线
- 反馈引脚与 `ADC` 采样配置仍待下一轮完成
- 反馈舵机到位后，仍按既定计划继续做电压采样与角度映射

也就是说，当前分支同时包含：

- 舵机串口控角联调能力
- `WS2812` 灯带调试能力
- 舵机反馈待下一轮实物验证的留档
