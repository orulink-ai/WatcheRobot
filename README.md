<div align="center">

<p><strong>English</strong> | <a href="README_zh.md">简体中文</a></p>

<img src="docs/images/watcher-robot-render.png" alt="WatcheRobot render" width="720">

<p>Open firmware, hardware manufacturing files, mechanical model assets, and release tooling for the WatcheRobot desktop robot.</p>

<p>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.2.1-green" alt="ESP-IDF v5.2.1">
  <img src="https://img.shields.io/badge/Hardware-Gerber%20%7C%20BOM%20%7C%20CPL-orange" alt="Hardware: Gerber, BOM, CPL">
</p>

</div>

---

## Overview

WatcheRobot is a desktop robot project built around an ESP32-S3 main controller, an STM32F103 co-processor, custom PCB modules, SD-card behavior assets, and a mechanical assembly package.

This repository is the public reproduction package for hackathon participants and open-source developers. It contains the materials needed to inspect the hardware, build or flash the embedded firmware, prepare SD-card behavior assets, validate the first motion, and contribute changes through GitHub.

Product applications and server-side runtime packages are distributed through GitHub Releases when available; their source code is not part of this repository.

## 10-Minute Quick Start

The shortest participant path is:

1. Clone or fork the repository.
2. Install the minimal tools for your operating system.
3. Download release assets when they are published, or build the firmware locally as a fallback.
4. Flash ESP32-S3 and prepare the SD-card behavior assets.
5. Power on the robot and run the first behavior smoke test.
6. Create a branch and open a PR if you change docs, firmware, or hardware files.

### 1. Clone

```bash
git clone https://github.com/orulink-ai/WatcheRobot.git
cd WatcheRobot
```

Fork the repository first if you plan to submit a pull request. Branch naming is documented in [CONTRIBUTING.md](CONTRIBUTING.md).

### 2. Install Tools

| Platform | Minimum tools |
| --- | --- |
| Windows | Git, Python 3.11+, USB serial driver for your board, ESP-IDF v5.2.1 if building locally |
| macOS | Git, Python 3.11+, serial driver if required by your USB adapter, ESP-IDF v5.2.1 if building locally |
| Linux | Git, Python 3.11+, `dialout` or equivalent serial permission, ESP-IDF v5.2.1 if building locally |

Use [docs/flashing.md](docs/flashing.md) for driver notes, ports, ESP32 flashing, STM32 flashing, and platform differences.

### 3. Get Firmware and Behavior Assets

Release assets are the preferred path for event participants. The first public activity release is not published yet, so local build and asset generation are the fallback.

ESP32-S3 local build fallback:

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

STM32F103 host-side test fallback:

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

### 4. Flash and Prepare SD Card

ESP32 release ZIP flashing uses the Windows helper when a release ZIP is available:

```bash
python -m pip install -r tools/win_flasher/requirements.txt
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-V2.3.0-esp32s3.zip --port COM7
```

For macOS/Linux, or for local ESP-IDF builds, use `idf.py flash monitor` from `firmware/esp32-s3`.

SD-card behavior assets are documented in [docs/sd-card-assets.md](docs/sd-card-assets.md). The event checklist version is [docs/behavior-flash-skill.md](docs/behavior-flash-skill.md).

### 5. Run the First Action

Use [docs/action-test.md](docs/action-test.md) to check:

- ESP32 boot logs and display startup
- SD-card `anim/` assets
- BLE or WebSocket command path
- one servo motion
- one LED behavior
- one touch event when hardware is available

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

docs/
  flashing.md             Firmware flashing and toolchain guide
  sd-card-assets.md       SD-card behavior asset guide
  behavior-flash-skill.md Event-ready behavior asset checklist
  action-test.md          First behavior smoke test
  sdk.md                  Public SDK and protocol boundary
  versions.md             Version source-of-truth guide
  downloads.md            Release asset guide
  compatibility.md        Version compatibility matrix
  release-process.md      Release process and asset rules
  governance.md           Repository boundary notes

tools/
  esp32-flasher/  ESP32 release flashing command reference
  win_flasher/    Windows ESP32 release ZIP flasher package
```

## Participant Documents

- [Flashing guide](docs/flashing.md)
- [SD-card behavior assets](docs/sd-card-assets.md)
- [Behavior asset field checklist](docs/behavior-flash-skill.md)
- [First action smoke test](docs/action-test.md)
- [SDK and protocol boundary](docs/sdk.md)
- [Version tracking](docs/versions.md)
- [Contribution guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Public Scope

Open in this repository:

- embedded firmware source
- PCB and mechanical publication files
- public flashing and release tooling
- public release documentation

Distributed as release assets when available, but not open as source here:

- Android application package
- server runtime package
- desktop application installer
- prebuilt firmware binaries

See [docs/downloads.md](docs/downloads.md) for the expected release asset types and current release status.

## Known Documentation Gaps

- GitHub Releases are not published yet, so some Quick Start paths use local build fallbacks.
- Cross-end protocol contract coverage is tracked in [GitHub issue #5](https://github.com/orulink-ai/WatcheRobot/issues/5).
- The hardware BOM files include model, specification, vendor, supplier part number, and alternate-part columns. Purchase links and event spare-part quantities are tracked in [hardware/pcb/spares.md](hardware/pcb/spares.md).

## Tech Stack

| Area | Main Technologies |
| --- | --- |
| ESP32-S3 firmware | ESP-IDF v5.2.1, FreeRTOS, LVGL |
| STM32F103 firmware | STM32 HAL, CMake, host-side C tests |
| Hardware | EasyEDA Pro, Gerber, BOM, CPL, STEP |
| Release tools | Python, Windows flashing utilities |

## License

This repository is licensed under [GPL-3.0](LICENSE), unless a subproject or third-party component states otherwise in its own license file.
