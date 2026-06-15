# STM32 最新测试代码收录（可直接测试）

本清单用于你“收录当前可测试代码 + 直接测试”：

## 1) 核心测试代码位置（已确认）

- 本机端协议/运行时逻辑测试：`firmware/stm32-f103/User/TestHost/test_main.c`
- 主机协议库（协议/帧/CRC/调度/运行时核心）：`firmware/stm32-f103/User/Protocol/`
- 板端串口 CLI：`firmware/stm32-f103/User/App/cli.c`
- 板端与 WatchRobot 通讯（USART2 + COPROC）：`firmware/stm32-f103/User/Platform/stm32/platform_coproc_uart.c`
- 启动与主循环：`firmware/stm32-f103/Core/Src/main.c`

## 2) 本地可执行测试（HostDebug）

命令（当前仓库可直接跑通）：

```bash
cd <repo-root>/firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug --output-on-failure
```

当前结果（2026-05-14）：

- `coproc_host_tests` 通过
- `100%` 测试通过

## 3) CLI 手工测试（USART1，115200 8N1）

- 烧录后串口上电会打印：
  - `=== STM32 CLI Ready ===`
  - `USART1 @ 115200 8N1`
  - `Commands: help, servo 90, servo_fb, servo_angle, ws red`
- 常用命令（已在 `help` 中）：`help`、`servo`、`servo_off`、`servo_recip`、`servo_fb`、`servo_angle`、`ws` 系列、`scan`、`i2c_diag`、`i2c_recover`、`i2c_pin_test`、`touch`、`ip5306_*`、`all`

## 4) WatchRobot 通信测试（USART2，921600 8N1）

- 通讯栈链路：`USART2`（RX DMA + IDLE） -> `platform_coproc_uart` -> `CoprocProtocol` -> `CoprocRuntime`
- 启动日志包含：
  - `USART2 bring-up ready: 921600 8N1, RX DMA + IDLE enabled`
- 与上位机/协议交互时以 `HELLO_REQ -> ACK -> HELLO_RSP` 为握手基准

## 5) 固件构建与烧录（板端）

```bash
cd <repo-root>/firmware/stm32-f103
cmake --preset Debug
cmake --build --preset Debug
```

产物：

- `build/Debug/watcheRobot_STM32.bin`
- `build/Debug/watcheRobot_STM32.hex`

建议烧录任务（VS Code）：

- `flash: Debug via STM32CubeProgrammer`
- `flash: Debug via OpenOCD fallback`

烧录命令需按本机 STM32CubeProgrammer 或 OpenOCD 安装路径配置。
