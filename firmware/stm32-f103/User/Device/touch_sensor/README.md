# Touch Sensor 驱动 README

> **日期**: 2026-05-01
> **项目**: watcheRobot_STM32
> **模块**: GPIO 触摸输入

## 1. 当前职责

本目录收口触摸传感器相关代码与说明：

- `touch_sensor.c`
- `touch_sensor.h`

当前模块职责很简单：

- 读取 `PA4` 当前输入电平
- 向 CLI 或上层业务暴露统一读取接口
- 当前已通过协处理器 runtime 接入 `TOUCH_EVENT` 上报

## 2. 当前接口

- `TouchSensor_Read()`

返回值约定：

- `HIGH`：未触摸
- `LOW`：触摸中

具体串口显示口径见 `User/App/cli.c` 的 `touch` 命令实现。

## 3. 当前实现口径

- `TouchSensor_Read()` 仍然直接读取 GPIO 输入电平
- 协处理器 runtime 侧将 `LOW` 转换为 active touch，并做 `30ms` 去抖
- 当前只产生 `press/release`，不产生 `long_press` 或 `double_tap`
- 当前引脚映射通过 `User/Board/board_pins.h` 间接引用 `PA4`

## 4. 当前边界

- 这是一个非常轻量的输入模块，不承载复杂事件逻辑
- 边沿检测和去抖当前在 `User/Protocol/coproc_runtime.c` 中完成
- 如果后续要扩展触摸输入，建议优先补：
  - 长按/短按判定
  - 更强的抗抖或节流策略
  - 多触点 `touch_id` 映射
