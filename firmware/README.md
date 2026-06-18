# Firmware

This directory contains the public embedded firmware source for WatcheRobot.

Firmware is organized by MCU / board target because each target has its own SDK, build system, board configuration, and validation path.

## Targets

| Target | Directory | Purpose | Build entry |
| --- | --- | --- | --- |
| ESP32-S3 | `esp32-s3/` | Main ESP32-S3 application firmware, services, protocols, assets, and release flashing flow. | `idf.py build` |
| STM32F103 | `stm32-f103/` | STM32F103 co-processor firmware, local peripheral control, protocol implementation, and host tests. | `cmake --preset HostDebug` |

## ESP32-S3 Build

Use ESP-IDF v5.2.1:

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

Release flash ZIPs and SD-card animation asset packages belong in GitHub Releases, not in this Git repository.

## STM32F103 Host Tests

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

## Public Release Rules

- Source code and public validation instructions live in this directory.
- Prebuilt firmware binaries and flash packages are uploaded to GitHub Releases.
- Firmware release notes must state compatible app, server, desktop, hardware, and model versions in `docs/compatibility.md`.
