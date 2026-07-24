<div align="center">

<p><strong>English</strong> | <a href="README_zh.md">简体中文</a></p>

<img src="docs/images/watcher-robot-render.png" alt="WatcheRobot render" width="720">

<p>Open-source materials for the WatcheRobot desktop robot, including embedded firmware, hardware manufacturing files, mechanical models, SD-card behavior assets, and release tooling.</p>

<p>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/Package-0.1.0-brightgreen" alt="Package 0.1.0">
  <img src="https://img.shields.io/badge/ESP32--S3-v0.3.2-green" alt="ESP32-S3 firmware v0.3.2">
  <img src="https://img.shields.io/badge/STM32F103-0.1.1-green" alt="STM32F103 firmware 0.1.1">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.2.1-green" alt="ESP-IDF v5.2.1">
  <img src="https://img.shields.io/badge/Hardware-Gerber%20%7C%20BOM%20%7C%20CPL-orange" alt="Hardware: Gerber, BOM, CPL">
</p>

</div>

---

## Overview

WatcheRobot is an open-source robot project for desktop interaction scenarios. The full system is built around SenseCAP Watcher, an ESP32-S3, an STM32F103 co-processor, custom PCBs, mechanical parts, and SD-card behavior assets. It supports the complete validation flow from hardware reproduction and firmware flashing to behavior-asset debugging.

This repository publishes the embedded firmware, hardware production files, mechanical assembly model, flashing tools, behavior-asset documentation, and Python SDK entrypoint for WatcheRobot. You can use these materials to reproduce the full robot, or reuse the hardware design, firmware protocol, motion-control logic, resource layout, or host-side control capability independently.

Through the Python SDK, developers can control the WatcheRobot ESP32-S3 from the host side and use the camera, microphone, speaker, and built-in expressions and animation effects. For interfaces and examples, see the [Python SDK documentation](python-sdk/README.md).

## Quick Start

This section helps you get the repository, prepare tools, and complete the smallest verifiable startup flow.
If you only want to reproduce or try the robot, prefer the prebuilt firmware and resource packages from GitHub Releases. If you need to modify firmware or develop further, build from source.

### 1. Get the Repository

```bash
git clone https://github.com/orulink-ai/WatcheRobot.git
cd WatcheRobot
git submodule update --init --recursive
```

If you plan to submit changes, fork the repository first and create a branch from your fork.

### 2. Prepare Tools

| Purpose | Tools |
| --- | --- |
| Base environment | Git, Python 3.11+ |
| ESP32-S3 flashing | USB serial driver, ESP-IDF v5.2.1, or the repository flashing tool |
| STM32F103 build/test | CMake, local C/C++ toolchain |
| SD-card assets | FAT32 SD card, behavior asset files |
| Hardware validation | Serial tool, multimeter, or basic hardware debugging tools |

For detailed Windows/macOS ESP-IDF v5.2.1 setup steps in Chinese, see [docs/esp-idf-setup_zh.md](docs/esp-idf-setup_zh.md).
If you only need reproduction or evaluation, prefer prebuilt firmware, SD-card resources, and client installers from [GitHub Releases](https://github.com/orulink-ai/WatcheRobot/releases).

### 3. Get Firmware and Behavior Assets

Release assets are the preferred path. Download firmware and SD-card resources from the same [GitHub Release](https://github.com/orulink-ai/WatcheRobot/releases) bundle. First-time reproduction needs at least the ESP32-S3 firmware, STM32F103 firmware, and SD-card assets from the same version; the Release also includes an AI flashing skill ZIP that an AI assistant can read before helping with flashing.

The current `watche-v0.1.1` asset list is documented in [Downloads](docs/downloads.md).

### 4. Flash Firmware and Prepare the SD Card

After downloading Release assets, flash STM32F103, flash ESP32-S3, and copy the SD-card assets before running first-start validation. The concrete steps are documented in [Firmware: Flashing and Assets](firmware/README.md#flashing-and-assets).

If you want an AI assistant to help with flashing, download `WatcheRobot-Flashing-Skill-v0.1.1.zip` from the Release or ask the AI to read [WatcheRobot Firmware Flashing Skill](tools/flashing/README.md). The skill guides the AI through same-version asset selection, serial-port detection, flashing, and boot-log checks.

### 5. First-Start Validation

Use the [first-start validation checklist](docs/action-test.md) to check:

- ESP32 boot logs and display startup
- SD-card `anim/` assets
- BLE or WebSocket command path
- one servo motion
- one LED behavior
- one touch event when hardware is available

After these checks pass, the smallest hardware, firmware, and resource chain is working. Continue with firmware, hardware, SDK, or resource documentation for further development.

## Repository Layout

```text
firmware/
  README.md                       Firmware build and directory guide
  COMMUNICATION_PROTOCOLS.md      Public firmware protocol overview
  esp32-s3/                       ESP32-S3 firmware source, assets, and ESP-IDF project files
  stm32-f103/                     STM32F103 co-processor firmware, protocol code, and host tests

hardware/
  README.md       Hardware package map and BOM notes
  pcb/            PCB source, schematics, layout PDFs, Gerber, BOM, CPL files, and spare-parts template
  3d-models/      Mechanical model exports
  assembly/       Assembly images or documents when available

python-sdk/
  README.md       Python SDK submodule for host-side integration and examples

docs/
  esp-idf-setup_zh.md     Windows/macOS ESP-IDF v5.2.1 setup guide in Chinese
  flashing.md             Firmware flashing and toolchain guide
  sd-card-assets.md       SD-card behavior asset guide
  behavior-flash-skill.md Behavior asset checklist
  action-test.md          First-start validation checklist
  sdk.md                  Public SDK and protocol boundary
  versions.md             Version source-of-truth guide
  downloads.md            Release asset guide
  compatibility.md        Version compatibility matrix
  release-process.md      Release process and asset rules
  governance.md           Repository boundary notes

tools/
  ...             Release, asset generation, and firmware helper tools
```

## Project Documentation

- [ESP-IDF setup guide in Chinese](docs/esp-idf-setup_zh.md)
- [Flashing guide](docs/flashing.md)
- [SD-card behavior assets](docs/sd-card-assets.md)
- [Behavior asset field checklist](docs/behavior-flash-skill.md)
- [First-start validation](docs/action-test.md)
- [SDK and protocol boundary](docs/sdk.md)
- [Python SDK](python-sdk/README.md)
- [Version tracking](docs/versions.md)
- [Contribution guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Public Scope

Open in this repository:

- embedded firmware source
- Python SDK source as a root-level Git submodule
- PCB and mechanical publication files
- public flashing and release tooling
- public release documentation

Distributed as release assets when available, but not open as source here:

- Android application package
- server runtime package
- desktop application installer
- prebuilt firmware binaries

See [docs/downloads.md](docs/downloads.md) for the expected release asset types and current release status.

## Tech Stack

| Area | Main Technologies |
| --- | --- |
| ESP32-S3 firmware | ESP-IDF v5.2.1, FreeRTOS, LVGL |
| STM32F103 firmware | STM32 HAL, CMake, host-side C tests |
| Python SDK | Python package, examples, host-side tests |
| Hardware | EasyEDA Pro, Gerber, BOM, CPL, STEP |
| Release tools | Python, Windows flashing utilities |

## License

This repository is licensed under [GPL-3.0](LICENSE), unless a subproject or third-party component states otherwise in its own license file.
