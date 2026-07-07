# ESP32 Flasher Command Reference

The full participant flashing flow is documented in `docs/flashing.md`. This file is only the command reference for the Python Windows release ZIP helper exposed as `tools.win_flasher`.

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
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-V2.3.0-esp32s3.zip --port COM7 --monitor
```

For macOS/Linux or local ESP-IDF builds, use `idf.py -p <PORT> flash monitor` from `firmware/esp32-s3`.
