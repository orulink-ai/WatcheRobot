<div align="center">

<p><strong>English</strong> | <a href="README_zh.md">简体中文</a></p>

</div>

# Firmware

This directory contains the public board-level firmware source for WatcheRobot. It is organized around two firmware projects:

- `esp32-s3/`: ESP32-S3 main-controller firmware for the main application, display and animation assets, BLE / Wi-Fi / WebSocket communication, server discovery, OTA, audio, peripheral services, and MCU Link communication with the STM32F103 co-processor.
- `stm32-f103/`: STM32F103 co-processor firmware for servo control, WS2812 LEDs, touch input, power control, sensor reading, local UART CLI, and USART2 protocol communication with ESP32-S3.

## ESP32-S3

Build with ESP-IDF v5.2.1:

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

Main contents:

- `main/`: ESP32-S3 application entry.
- `components/`: hardware abstraction, communication protocols, device services, and utility components.
- `spiffs/`: actions, animations, and sound assets packaged with firmware.
- `assets/`: source animation and sound assets.
- `tools/`: animation asset generation and sync tools.
- `docs/`: ESP32-S3 protocol, asset, and development workflow documents.

## STM32F103

Host-side test build:

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

Main contents:

- `Core/`: STM32CubeMX-generated base project code.
- `Drivers/`: CMSIS and STM32 HAL drivers.
- `User/App/`: application entry, local CLI, and business scheduling.
- `User/Board/`: board-level pin definitions.
- `User/Config/`: firmware configuration.
- `User/Device/`: servo, sensor, power, LED, and other device drivers.
- `User/Platform/`: UART, I2C, PWM, time, and other platform adapters.
- `User/Protocol/`: COBS, CRC16, frame codec, protocol dispatch, and runtime.
- `User/TestHost/`: host-side protocol and device-logic tests.
- `Tools/`: debug and integration tools.
- `Documents/`: STM32-side design notes, protocol notes, and test records.

## Flashing and Assets

For first-time reproduction, download these assets from the same [GitHub Release](https://github.com/orulink-ai/WatcheRobot/releases) bundle:

| Content | File |
| --- | --- |
| ESP32-S3 firmware | `WatcheRobot-ESP32S3-v0.3.2.zip` |
| STM32F103 firmware | `WatcheRobot-STM32F103-v0.1.1.zip` |
| SD-card assets | `WatcheRobot-SDCard-Assets-v0.3.2.zip` |
| AI flashing skill | `WatcheRobot-Flashing-Skill-v0.1.1.zip` |
| Manifest and checksums | `WatcheRobot-Bundle-v0.1.1.manifest.json`, `SHA256SUMS.txt` |

Do not mix firmware and SD-card assets from different releases. The complete asset list is maintained in [Downloads](../docs/downloads.md).

### AI-Assisted Flashing

The repository provides an AI-oriented flashing skill: [WatcheRobot Firmware Flashing Skill](../tools/flashing/README.md). The Release also includes the same skill as `WatcheRobot-Flashing-Skill-v0.1.1.zip`. If you use Codex or another AI coding assistant, ask it to read this README or the README inside the ZIP, then let it handle Release asset selection, serial-port detection, ESP32-S3 flashing, SD-card asset preparation, and boot-log checks.

You can say:

```text
Please read tools/flashing/README.md and help me flash WatcheRobot.
Use the latest Release ESP32-S3 firmware, STM32F103 firmware, and SD-card assets, detect the current serial port, and check boot logs after flashing.
```

The commands below are mainly for AI assistants, automation flows, or manual troubleshooting.

ESP32-S3 release ZIP flashing:

```bash
python -m pip install -r tools/win_flasher/requirements.txt
python -m tools.win_flasher flash --zip .\WatcheRobot-ESP32S3-v0.3.2.zip --port COM7
```

On Windows, you can also use:

```powershell
tools\flash-release.cmd --zip .\WatcheRobot-ESP32S3-v0.3.2.zip --port COM7 --monitor
```

For repeated multi-device or automated flashing, prefer asking the AI assistant to read [WatcheRobot Firmware Flashing Skill](../tools/flashing/README.md) and execute the flow. The underlying entrypoint is `tools/run-lane.ps1`; the device-map template is `tools/flashing/device-map.example.toml`.

If ESP-IDF is already installed, you can also run this from `firmware/esp32-s3`:

```bash
idf.py flash monitor
```

SD-card asset layout and copy instructions are documented in [SD Card Assets](../docs/sd-card-assets.md). Driver notes, port discovery, and platform differences are documented in [Firmware Flashing Guide](../docs/flashing.md).

## Communication Protocols

The readable protocol overview is maintained in [Communication Protocols](COMMUNICATION_PROTOCOLS.md). This README only keeps entry links to avoid duplicating protocol tables across files.

| Link | Document | Code |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | [Communication Protocols](COMMUNICATION_PROTOCOLS.md#2-esp32-s3---stm32f103) | `esp32-s3/components/protocols/mcu_link/`, `stm32-f103/User/Protocol/`, `stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
| ESP32-S3 <-> Server / Wi-Fi / WebSocket | [Communication Protocols](COMMUNICATION_PROTOCOLS.md#3-esp32-s3---server) | `esp32-s3/components/utils/wifi_manager/`, `esp32-s3/components/protocols/discovery/`, `esp32-s3/components/protocols/ws_client/` |
| ESP32-S3 <-> App / BLE | [Communication Protocols](COMMUNICATION_PROTOCOLS.md#4-esp32-s3---app--ble) | `esp32-s3/components/protocols/ble_service/` |

Note: `esp32-s3/docs/COPROC_COMM_PROTOCOL.md` describes the ESP32-S3 to HX6538 / SSCMA vision co-processor link. It is not the main ESP32-S3 to STM32F103 protocol entry.

## Detailed References

Lower-level READMEs under `components/`, `User/Device/`, and `Documents/` are kept as implementation notes, hardware bring-up notes, or historical records. Start from this README and the protocol overview first; then open detailed notes only when you need a specific driver, command, pin mapping, or test procedure.

## Release

Prebuilt firmware, flashing bundles, and SD-card animation asset packages should not be committed directly to this Git repository. Upload them as GitHub Release assets.

Release notes should state the compatible App, Server, Desktop, hardware, and model versions.
