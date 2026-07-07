# SDK and Public Protocol Boundary

This repository does not currently publish a single packaged SDK. The public integration surface is the firmware source, host tests, protocol documentation, and release tools listed below.

## Current Public Interfaces

| Area | Entry point | Status |
| --- | --- | --- |
| BLE app commands | `firmware/esp32-s3/components/protocols/ble_service/` | Public implementation, command behavior still needs cross-entry contract tests. |
| WebSocket server commands | `firmware/esp32-s3/components/protocols/ws_client/` | Public implementation, JSON and WSPK media protocol need golden tests. |
| ESP32-S3 to STM32F103 MCU Link | `firmware/COMMUNICATION_PROTOCOLS.md` | Public overview, core frame format documented, cross-end matrix tracked as TODO. |
| UDP discovery | `firmware/esp32-s3/components/protocols/discovery/` | Public implementation, edge-case behavior needs tests. |
| SSCMA vision co-processor | `firmware/esp32-s3/components/sscma_client/` | Public source integration, protocol boundary needs more host/fake IO coverage. |
| Windows release flashing | `tools/win_flasher/` | Public Python tool for ESP32 release ZIPs. |

## Stable for Participants

Participants may rely on:

- repository layout documented in `README.md`
- ESP-IDF v5.2.1 as the ESP32 build baseline
- STM32 host-test entry commands in `firmware/README.md`
- release assets being distributed through GitHub Releases when available
- SD-card behavior assets being copied under `anim/`

## Not Yet Fully Frozen

The following are public but not fully frozen as a single SDK contract:

- BLE JSON and WebSocket JSON parity
- LED payload semantics across ESP32 and STM32
- ESP32 motion sequence messages before STM32 runtime support is finalized
- WSPK binary media header golden-vector coverage
- Discovery malformed-response behavior
- SSCMA AT framing and SPI/I2C/UART transport edge cases

The cross-end protocol contract work is tracked in [GitHub issue #5](https://github.com/orulink-ai/WatcheRobot/issues/5).

## How to Build Against the Current Repo

- For firmware work, include component headers directly from the relevant firmware project.
- For automation or release tooling, call the Python tools through `python -m tools.win_flasher` or the scripts under `firmware/esp32-s3/tools/`.
- For hardware reproduction, use the manufacturing package under `hardware/pcb/`.

If a formal SDK package is added later, this document should become its public version and compatibility index.
