# ESP32 Flasher

The Windows flasher package is available as `tools.win_flasher`.

Install dependencies:

```powershell
python -m pip install -r tools/win_flasher/requirements.txt
```

Flash a downloaded release ZIP:

```powershell
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-V2.3.0-esp32s3.zip --port COM7
```

Release ZIPs are downloaded from GitHub Releases; they are not stored in this repository.
