# QMC6309 驱动 README

> **模块路径**: `User/Device/qmc6309/`
> **芯片**: QST QMC6309
> **接口**: I2C1

本模块提供 QMC6309 地磁传感器的最小项目接入：

- `qmc6309.c`
- `qmc6309.h`

## 当前配置

| 项目 | 配置 |
|------|------|
| I2C 7-bit 地址 | `0x7C` |
| Chip ID | `0x90` |
| 输出数据寄存器 | `0x01..0x06` |
| 状态寄存器 | `0x09` |
| 工作模式 | Normal |
| 输出速率 | 200 Hz |
| 量程 | 32 G |

## 接口

```c
uint8_t QMC6309_Init(void);
uint8_t QMC6309_ReadData(QMC6309_DataTypeDef *data);
uint8_t QMC6309_ReadStatus(uint8_t *status);
```

`QMC6309_ReadData()` 会等待 `DRDY` 置位，检测溢出位，并读取三轴原始 `int16_t` 数据。

## CLI

```text
qmc6309
mag
all
```

`qmc6309` 和 `mag` 读取 QMC6309 三轴原始磁场数据；`all` 会同时读取 BMI160 和 QMC6309。
