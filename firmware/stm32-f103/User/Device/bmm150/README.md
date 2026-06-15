# BMM150 驱动 README

> **日期**: 2026-04-18
> **项目**: watcheRobot_STM32
> **芯片**: Bosch BMM150

## 1. 当前职责

本目录收口 `BMM150` 相关代码与说明：

- `bmm150.c`
- `bmm150.h`

当前驱动负责：

- 读取 `CHIP_ID`
- 完成基础初始化
- 触发一次测量并读取磁力计原始值

## 2. 当前接口

- `BMM150_Init()`
- `BMM150_ReadData(BMM150_DataTypeDef *data)`

数据结构定义位于 `bmm150.h`：

- `magX / magY / magZ`

## 3. 当前实现口径

- I2C 地址由 `APP_BMM150_ADDR` 提供
- 初始化时会先校验 `CHIP_ID == 0x32`
- 当前读取流程维持 forced mode：
  - 上电使能
  - 触发一次测量
  - 等待测量完成
  - 读取寄存器原始值

## 4. 关联模块

- `User/Device/sensors/README.md`
  - 总线扫描、整体初始化和 CLI 口径
- `User/App/cli.c`
  - `bmm150` 命令读取原始数据

## 5. 当前边界

- 当前输出仍是原始值，不做物理单位换算
- 当前未做软铁/硬铁校准
- 当前未引入连续读取或滤波策略
