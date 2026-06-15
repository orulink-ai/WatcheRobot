# WatcheRobot

WatcheRobot is the public entry point for the WatcheRobot project.

This repository contains the open firmware, hardware publication materials, public documentation, and release coordination files. The mobile app, server, and desktop app are distributed as release artifacts only; their source code is not included in this repository.

## Repository Layout

```text
firmware/
  esp32-s3/      ESP32-S3 firmware source
  stm32-f103/    STM32F103 firmware source

hardware/
  V1.0.0/        Hardware publication materials by hardware version

docs/
  downloads.md        Release download guide
  compatibility.md    Version compatibility matrix
  release-process.md  Release process and asset rules
  governance.md       Maintainer and contribution policy

tools/
  esp32-flasher/  Public guide for ESP32 release flashing
  win_flasher/    Windows ESP32 release ZIP flasher package
```

## Open And Closed Components

Open in this repository:

- ESP32-S3 firmware
- STM32F103 firmware
- public hardware files and 3D models
- flashing tools and public documentation

Distributed through GitHub Releases, but source code is not open here:

- Android app
- server runtime package
- desktop application installer

Download release artifacts from the [GitHub Releases](https://github.com/orulink-ai/WatcheRobot/releases) page. See [docs/downloads.md](docs/downloads.md) for the expected asset types.

## License Status

License terms are pending owner/legal confirmation. Until a `LICENSE` file is added, redistribution terms are not granted for newly published WatcheRobot project materials in this repository. Third-party components retain their original licenses as noted in their directories.

## Build Entry Points

ESP32-S3:

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

STM32 host tests:

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

Hardware files are organized by version under `hardware/Vx.y.z/`.
