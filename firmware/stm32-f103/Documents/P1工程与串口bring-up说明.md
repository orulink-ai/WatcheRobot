# P1 工程与串口 bring-up 说明

## 1. 本阶段目标

本阶段只收口工程入口和串口 bring-up，不引入完整协议栈，也不开始 motion、led、sensor 业务扩展。

当前固定配置如下：

- MCU：`STM32F103C8Tx`
- 系统时钟：`72MHz`
- `USART1`：调试日志与本地 CLI，`115200 8N1`
- `USART2`：协处理器链路入口，`921600 8N1`

## 2. 串口角色划分

- `USART1`
  - 继续承担本地调试打印和命令行输入
  - 不参与后续 v2 协议收发
- `USART2`
  - 作为后续 ESP32 <-> STM32 协处理器链路入口
  - 本阶段只打通 `RX DMA + IDLE` bring-up，不做 `COBS / CRC16 / frame parser`

## 3. 当前 bring-up 配置

### 3.1 CubeMX / `.ioc` 口径

- `USART2_TX`：`DMA1_Channel7`
- `USART2_RX`：`DMA1_Channel6`
- `USART2_IRQn`：已打开
- DMA 模式：
  - `USART2_TX` 使用 `DMA_NORMAL`
  - `USART2_RX` 使用 `DMA_NORMAL`

### 3.2 软件入口

- `App_Init()`
  - 调用 `Platform_CoprocUart_Init()`
  - 启动 `HAL_UARTEx_ReceiveToIdle_DMA()`
- `App_RunOnce()`
  - 调用 `Platform_CoprocUart_Poll()`
  - 将 `USART2` 收到的数据摘要打印到 `USART1`
  - 继续在 `User/Protocol/` 中推进 `delimiter / COBS / CRC16 / HELLO`

### 3.3 本阶段验收行为

- `USART1` 启动后仍可输出调试日志和响应 CLI
- `USART2` 收到数据后，DMA 和 IDLE 事件能触发回调
- `USART1` 会打印收到的字节数和前若干字节十六进制预览
- `USART1` 同时输出单行结构化观测，前缀固定为 `STM32_OBS`

### 3.4 主机联调日志口径

- 本阶段主机通过 `USART1 + ESP32 log UART` 合并观察现场
- 不直接从 PC 嗅探板内 `USART2@921600`
- `STM32_OBS` 作为主机脚本唯一解析入口
- 人类可读日志继续保留，用于人工排查

## 4. VS Code 任务

仓库已提供以下任务文件：

- `.vscode/tasks.json`
  - `cmake: configure Debug`
  - `cmake: build Debug`
  - `flash: Debug via openocd`
  - `cmake: configure HostDebug`
  - `cmake: build HostDebug`
  - `ctest: HostDebug`
- `.vscode/launch.json`
  - `Debug STM32F103 (openocd)`

本机依赖约定如下：

- `cmake`：`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- `ninja`：`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- `arm-none-eabi-gdb`：`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\13.2 Rel1\bin\arm-none-eabi-gdb.exe`
- `openocd`：`C:\Users\50533\AppData\Local\Arduino15\packages\esp32\tools\openocd-esp32\v0.12.0-esp32-20241016\bin\openocd.exe`

bench 默认不从 VS Code 任务触发，优先使用现有 dual-MCU wrapper：

- `powershell -ExecutionPolicy Bypass -File "C:\Users\50533\.codex\skills\watche-dual-mcu-bringup\scripts\run-dual-mcu-bringup.ps1" -SkipStm32Build -SkipEsp32Build -SkipStm32Flash -SkipEsp32Flash -RestartStm32 -RestartEsp32 -DurationSec 30`

## 5. 本阶段明确不做

- 不实现 `SNAPSHOT_REQ / SNAPSHOT_RSP`
- 不实现 motion / led / sensor dispatcher
- 不改变当前已有 servo / sensor 的旧 demo 行为
- 不通过 PC 直接控制或嗅探板内 `USART2`
