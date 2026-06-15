# I2C 传感器集成 README

> **项目**: watcheRobot_STM32
> **硬件**: STM32F103C8T6 + BMI160 陀螺仪/加速度计 + QMC6309 地磁传感器

本文描述当前主线代码中的 I2C 传感器接入状态。

## 总线拓扑

```text
STM32F103C8T6
  PB6  I2C1_SCL  --> BMI160 SCL, QMC6309 SCL
  PB7  I2C1_SDA  --> BMI160 SDA, QMC6309 SDA
```

I2C1 当前按 CubeMX 配置为 Fast Mode，400 kHz。

## 设备地址

| 传感器 | 型号 | I2C 7-bit 地址 |
|--------|------|----------------|
| 陀螺仪/加速度计 | BMI160 | `0x68` 或 `0x69`，当前默认 `0x69` |
| 地磁 | QMC6309 | `0x7C` |

相关配置位于 `User/Config/app_config.h`：

```c
#define APP_BMI160_ADDR 0x69U
#define APP_QMC6309_ADDR 0x7CU
```

## 代码位置

| 文件 | 说明 |
|------|------|
| `Core/Src/i2c.c` | I2C1 外设初始化 |
| `User/Platform/stm32/platform_i2c.c` | 7-bit 地址到 HAL 地址封装 |
| `User/Device/bmi160/bmi160.c` | BMI160 初始化与数据读取 |
| `User/Device/qmc6309/qmc6309.c` | QMC6309 初始化与数据读取 |
| `User/Device/sensors/sensors.c` | 启动阶段 I2C 扫描 |
| `User/App/cli.c` | 串口命令处理 |

## 启动流程

`App_Init()` 中会按以下顺序处理传感器：

```c
Sensors_Scan();
BMI160_Init();
QMC6309_Init();
```

典型扫描输出：

```text
========== I2C Sensor Scan ==========
[FOUND] BMI160 @ 0x69
[FOUND] QMC6309 @ 0x7C
====================================
```

## CLI 命令

| 命令 | 说明 |
|------|------|
| `scan` | 扫描 I2C 设备 |
| `bmi160` | 读取 BMI160 陀螺仪和加速度计原始数据 |
| `qmc6309` | 读取 QMC6309 地磁原始数据 |
| `mag` | `qmc6309` 的短别名 |
| `all` | 读取 BMI160 和 QMC6309 |

QMC6309 输出示例：

```text
========== QMC6309 Data ==========
  Mag X: 123
  Mag Y: -45
  Mag Z: 678
=================================
```

## QMC6309 配置

当前驱动按 QMC6309 数据手册配置：

| 项目 | 值 |
|------|----|
| Chip ID | `0x90` |
| 数据寄存器 | `0x01..0x06` |
| 状态寄存器 | `0x09` |
| 控制寄存器 | `0x0A`, `0x0B` |
| 工作模式 | Normal |
| ODR | 200 Hz |
| 量程 | 32 G |

`QMC6309_ReadData()` 会等待 `DRDY`，检查溢出位，再读取 X/Y/Z 三轴 `int16_t` 原始数据。

## 排查

| 现象 | 检查项 |
|------|--------|
| 扫描不到 QMC6309 | 确认供电、SCL/SDA、上拉电阻、地址是否为 `0x7C` |
| Chip ID 不等于 `0x90` | 确认芯片型号和 I2C 地址，排查总线冲突 |
| `Read failed or data not ready` | 确认 `QMC6309_Init()` 已成功，检查状态寄存器 `DRDY/OVFL` |
| 数据跳变明显 | 远离强磁干扰，后续可增加校准和滤波 |
