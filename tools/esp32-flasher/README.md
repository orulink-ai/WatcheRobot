# ESP32 Flasher Command Reference

The full flashing flow is documented in `docs/flashing.md`. This file is only the command reference for the Python Windows release ZIP helper exposed as `tools.win_flasher`.

## Install Dependencies

```powershell
python -m pip install -r tools/win_flasher/requirements.txt
```

## List Local Release ZIPs

```powershell
python -m tools.win_flasher list-releases
```

By default, the helper scans `.local/release-zips/<version>/`. Release ZIPs are downloaded from GitHub Releases; they are not stored in this repository.

## List Serial Ports

```powershell
python -m tools.win_flasher list-ports
```

## Flash a Downloaded Release ZIP

```powershell
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-v0.3.2-esp32s3.zip --port COM7 --monitor
```

For macOS/Linux or local ESP-IDF builds, use `idf.py -p <PORT> flash monitor` from `firmware/esp32-s3`.

## Windows Shortcut

```powershell
tools\flash-release.cmd --zip .\WatcheRobot-S3-v0.3.2-esp32s3.zip --port COM7 --monitor
```

## Codex Lane Flashing

For repeated bench work with named device aliases:

```powershell
tools\run-lane.cmd -Firmware s3 -Feature smoke -DeviceAlias s3-a
```

Copy `tools\flashing\device-map.example.toml` to a local-only path such as `$env:USERPROFILE\.watche-robot\device-map.toml`, fill in the local COM ports, and set `CODEX_DEVICE_MAP_PATH` before running the lane command.
