# BMI160 驱动 README

> **日期**: 2026-04-18  
> **项目**: watcheRobot_STM32  
> **芯片**: Bosch BMI160

## 1. 当前职责

本目录收口 `BMI160` 相关代码与说明：

- `bmi160.c`
- `bmi160.h`

当前驱动负责：

- 读取 `CHIP_ID`
- 完成基础初始化
- 读取陀螺仪与加速度计原始值

## 2. 当前接口

- `BMI160_Init()`
- `BMI160_ReadData(BMI160_DataTypeDef *data)`

数据结构定义位于 `bmi160.h`：

- `gyroX / gyroY / gyroZ`
- `accX / accY / accZ`

## 3. 当前实现口径

- I2C 地址由 `APP_BMI160_ADDR` 提供
- 初始化时会先校验 `CHIP_ID == 0xD1`
- 当前初始化顺序保持为：
  - 唤醒加速度计
  - 唤醒陀螺仪
  - 写入量程/带宽配置

## 4. 关联模块

- `User/Device/sensors/README.md`
  - 总线扫描、整体初始化和 CLI 口径
- `User/App/cli.c`
  - `bmi160` 命令读取原始数据

## 5. 当前边界

- 当前输出仍是原始值，不做物理单位换算
- 当前未做滤波、零偏校准或姿态融合
- 后续如要扩展更完整的 IMU 逻辑，优先继续在本目录内演进
