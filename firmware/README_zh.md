<div align="center">

<p><a href="README.md">English</a> | <strong>简体中文</strong></p>

</div>

# Firmware

`firmware/` 提供 WatcheRobot 当前公开板级固件源码，主要包含两个固件工程：

- `esp32-s3/`：ESP32-S3 主控板固件代码，用于运行主应用、屏幕与动画资源、BLE / Wi-Fi / WebSocket 通信、服务发现、OTA、音频与外设服务，并通过 MCU Link 与 STM32F103 协处理器通信。
- `stm32-f103/`：STM32F103 协处理器固件代码，用于处理舵机、WS2812 灯带、触摸输入、电源控制、传感器读取、本地串口 CLI，以及与 ESP32-S3 之间的 USART2 协议通信。

## ESP32-S3

使用 ESP-IDF v5.2.1 构建：

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

主要内容：

- `main/`：ESP32-S3 应用入口。
- `components/`：硬件抽象、通信协议、设备服务和工具组件。
- `spiffs/`：随固件打包的动作、动画和音效资源。
- `assets/`：原始动画和音效素材。
- `tools/`：动画资源生成和同步工具。
- `docs/`：ESP32-S3 侧协议、资源和开发流程文档。

## STM32F103

主机侧测试构建：

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

主要内容：

- `Core/`：STM32CubeMX 生成的基础工程代码。
- `Drivers/`：CMSIS 和 STM32 HAL 驱动。
- `User/App/`：应用入口、本地 CLI 和业务调度。
- `User/Board/`：板级引脚定义。
- `User/Config/`：固件配置。
- `User/Device/`：舵机、传感器、电源、灯带等设备驱动。
- `User/Platform/`：UART、I2C、PWM、时间等平台适配层。
- `User/Protocol/`：COBS、CRC16、帧编解码、协议分发和运行时。
- `User/TestHost/`：主机侧协议和设备逻辑测试。
- `Tools/`：调试和联调工具。
- `Documents/`：STM32 侧设计记录、协议说明和测试记录。

## 烧录和资源

首次复现建议从同一个 [GitHub Release](https://github.com/orulink-ai/WatcheRobot/releases) 套装中下载：

| 内容 | 文件 |
| --- | --- |
| ESP32-S3 固件 | `WatcheRobot-ESP32S3-v0.3.2.zip` |
| STM32F103 固件 | `WatcheRobot-STM32F103-v0.1.1.zip` |
| SD 卡资源 | `WatcheRobot-SDCard-Assets-v0.3.2.zip` |
| AI 烧录 Skill | `WatcheRobot-Flashing-Skill-v0.1.1.zip` |
| 版本和校验 | `WatcheRobot-Bundle-v0.1.1.manifest.json`、`SHA256SUMS.txt` |

不要混用不同 Release 中的固件和 SD 卡资源。完整资源清单见 [下载说明](../docs/downloads.md)。

### AI 辅助烧录

仓库提供了面向 AI 助手的烧录 Skill：[WatcheRobot 固件烧录 Skill](../tools/flashing/README_zh.md)。Release 中也提供同名压缩包 `WatcheRobot-Flashing-Skill-v0.1.1.zip`。如果你使用 Codex 或其他 AI 编程助手，可以直接让它读取这个 README 或压缩包内的 README，并帮你完成 Release 资源选择、串口识别、ESP32-S3 烧录、SD 卡资源准备和启动日志检查。

你可以直接这样说：

```text
请阅读 tools/flashing/README_zh.md，帮我烧录 WatcheRobot。
使用最新 Release 中的 ESP32-S3 固件、STM32F103 固件和 SD 卡资源，识别当前串口，烧录后帮我查看启动日志。
```

下面的命令主要给 AI 助手、自动化流程或需要手动排查时参考。

ESP32-S3 Release ZIP 烧录：

```bash
python -m pip install -r tools/win_flasher/requirements.txt
python -m tools.win_flasher flash --zip .\WatcheRobot-ESP32S3-v0.3.2.zip --port COM7
```

Windows 下也可以使用：

```powershell
tools\flash-release.cmd --zip .\WatcheRobot-ESP32S3-v0.3.2.zip --port COM7 --monitor
```

多设备或自动化烧录优先交给 AI 助手读取 [WatcheRobot 固件烧录 Skill](../tools/flashing/README_zh.md) 后执行；底层入口是 `tools/run-lane.ps1`，设备映射模板见 `tools/flashing/device-map.example.toml`。

如果你已经安装 ESP-IDF，也可以在 `firmware/esp32-s3` 目录下使用：

```bash
idf.py flash monitor
```

SD 卡资源目录结构和写入说明见 [SD 卡资源说明](../docs/sd-card-assets.md)。更多串口驱动、端口识别和平台差异见 [固件烧录说明](../docs/flashing.md)。

## 通信协议入口

通信协议的简版说明集中在 [通信协议总览](COMMUNICATION_PROTOCOLS_zh.md)。`README` 只保留入口，避免在多个文件里重复维护字段表。

| 通信链路 | 文档入口 | 代码入口 |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | [通信协议总览](COMMUNICATION_PROTOCOLS_zh.md#2-esp32-s3---stm32f103) | `esp32-s3/components/protocols/mcu_link/`、`stm32-f103/User/Protocol/`、`stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
| ESP32-S3 <-> Server / Wi-Fi / WebSocket | [通信协议总览](COMMUNICATION_PROTOCOLS_zh.md#3-esp32-s3---server) | `esp32-s3/components/utils/wifi_manager/`、`esp32-s3/components/protocols/discovery/`、`esp32-s3/components/protocols/ws_client/` |
| ESP32-S3 <-> App / BLE | [通信协议总览](COMMUNICATION_PROTOCOLS_zh.md#4-esp32-s3---app--ble) | `esp32-s3/components/protocols/ble_service/` |

说明：`esp32-s3/docs/COPROC_COMM_PROTOCOL.md` 是 ESP32-S3 与 HX6538 / SSCMA 视觉协处理器相关文档，不是 ESP32-S3 与 STM32F103 的主协议入口。

## 详细参考

`components/`、`User/Device/` 和 `Documents/` 下的底层 README 会继续保留为实现说明、硬件 bring-up 记录或历史资料。建议先阅读本 README 和协议总览；只有需要具体驱动、命令、引脚或测试流程时，再进入这些详细文档。

## Release

预编译固件、烧录包、SD 卡动画资源包等发布产物不直接提交到 Git 仓库，应作为 GitHub Release 资产上传。

Release 说明中需要标明兼容的 App、Server、Desktop、硬件和模型版本。
