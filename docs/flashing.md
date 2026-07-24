# Firmware Flashing Guide

This guide is the public flashing entry for WatcheRobot firmware. Use release assets when they are available. If no release asset exists yet, use the local build fallback commands.

## Toolchain Matrix

| Target | Required tools | Notes |
| --- | --- | --- |
| ESP32-S3 firmware | ESP-IDF v5.2.1, Python 3.11+, USB serial driver | Arduino IDE is not the supported build path for this repository. |
| ESP32 release ZIP flashing | Python 3.11+, `esptool`, `pyserial`, `rich` | Install with `python -m pip install -r tools/win_flasher/requirements.txt`. |
| STM32F103 host tests | CMake 3.22+, Ninja, C compiler | Used for protocol and device-logic host tests. |
| STM32F103 board flashing | ST-LINK or compatible probe, OpenOCD or vendor tooling | Board flashing is hardware-bench dependent and not yet packaged as a one-command public flow. |

For detailed Windows/macOS ESP-IDF v5.2.1 installation and activation steps in Chinese, see [ESP-IDF setup guide](esp-idf-setup_zh.md).

## Serial Drivers and Ports

Windows:

- Install the USB serial driver required by the board or USB-UART adapter.
- Open Device Manager and confirm the `COMx` port.
- Use explicit ports in examples, such as `--port COM7`.

macOS:

- Install a driver only if your adapter does not appear automatically.
- Ports usually appear as `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- Use `ls /dev/cu.*` to list candidate ports.

Linux:

- Ports usually appear as `/dev/ttyUSB*` or `/dev/ttyACM*`.
- Add your user to `dialout` or the equivalent serial group if permission is denied.
- Re-login after changing group membership.

## ESP32-S3 Release ZIP Flashing

When a GitHub Release contains an ESP32 firmware flash ZIP, use the helper:

```bash
python -m pip install -r tools/win_flasher/requirements.txt
python -m tools.win_flasher list-ports
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-v0.3.2-esp32s3.zip --port COM7 --monitor
```

The helper expects a ZIP that contains `flash_args.txt`, `bootloader.bin`, `partition-table.bin`, and an app firmware segment named `WatcheRobot-S3.bin`.

For interactive Windows flashing, you can also run:

```powershell
tools\flash-release.cmd --zip .\WatcheRobot-S3-v0.3.2-esp32s3.zip --port COM7 --monitor
```

If you want `list-releases` to discover downloaded ZIP files automatically, put them under `.local/release-zips/<version>/`.

## ESP32-S3 Local Build Fallback

Use this path when release assets are not published yet or when developing firmware:

Install and activate ESP-IDF v5.2.1 first. Windows users should run these commands from `ESP-IDF 5.2 CMD`; macOS users should run `. ./export.sh` from their ESP-IDF v5.2.1 directory before entering the repository.

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Replace `<PORT>` with `COM7`, `/dev/cu.usbserial-*`, or `/dev/ttyUSB0`.

On Windows, the repository also includes a wrapper that resolves ESP-IDF and records bounded monitor output:

```powershell
powershell -ExecutionPolicy Bypass -File .\firmware\esp32-s3\tools\flash-monitor.ps1 -Port COM7
```

For repeated multi-device or automated flashing, copy `tools\flashing\device-map.example.toml` to a local-only path such as `$env:USERPROFILE\.watche-robot\device-map.toml`, fill in local ports, and run:

```powershell
$env:CODEX_DEVICE_MAP_PATH = "$env:USERPROFILE\.watche-robot\device-map.toml"
tools\run-lane.cmd -Firmware s3 -Feature smoke -DeviceAlias s3-a
```

## STM32F103 Host Tests

These tests do not flash hardware, but they are the public validation path for STM32 protocol and device logic:

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

Known issue: some open pull requests currently fail STM32 host build because of source-level integration issues. Treat CI as the source of truth before release.

## STM32F103 Board Flashing

Board flashing depends on the physical probe and bench setup.

Minimum public expectations:

- MCU target: `STM32F103C8Tx`
- Debug probe: ST-LINK or compatible SWD probe
- Firmware project: `firmware/stm32-f103`
- Local debug UART: `USART1 @ 115200 8N1`
- ESP32 co-processor link: `USART2 @ 921600 8N1`

Do not use private `.vscode` tasks or local COM-port notes as public instructions. If a repeatable public STM32 flashing package is published later, add it to GitHub Releases and document it here.

## Common Problems

| Symptom | Check |
| --- | --- |
| Port not found | Replug USB, check driver, confirm the OS-specific port name. |
| Permission denied on Linux | Add user to `dialout` or run with a temporary udev rule. |
| Flash ZIP rejected | Confirm the ZIP contains `flash_args.txt` and required binaries. |
| ESP-IDF command missing | Activate ESP-IDF v5.2.1 environment before running `idf.py`. |
| Monitor shows unreadable text | Confirm baud rate and target port. |
