# SD-Card Behavior Assets

WatcheRobot behavior animations are stored on SD card as generated AnimPack assets. GIF files are authoring sources; the firmware runtime reads generated files from the SD-card `anim/` directory.

## Expected SD Layout

```text
<sd-card-root>/
  anim/
    anim_manifest.bin
    boot.animpack
    happy.animpack
    standby.animpack
    ...
```

The exact set depends on the source animation folder and release version.

## Preferred Release Path

When a GitHub Release provides an SD-card behavior asset ZIP:

1. Format the SD card using the filesystem required by the released firmware.
2. Extract the ZIP at the SD-card root.
3. Confirm `anim/anim_manifest.bin` exists.
4. Insert the card before booting the robot.
5. Run the smoke test in `docs/action-test.md`.

No public Release asset is currently published, so use the local generation fallback below.

## Local Generation Fallback

Install Pillow:

```bash
python -m pip install Pillow
```

Generate AnimPack assets from GIF sources:

```bash
cd firmware/esp32-s3
python tools/generate_anim_assets.py --input-dir assets/gif --output-dir release/V2.3.0/sdcard/anim --fps 10
```

If the firmware is built by ESP-IDF, generated assets may also appear under:

```text
firmware/esp32-s3/build/generated/sdcard/anim
```

## Sync to SD Card

Use the sync helper so the target `anim/` directory is cleaned, copied, and hash-verified:

Windows example:

```bash
cd firmware/esp32-s3
python tools/sync_anim_sdcard.py --source-dir release/V2.3.0/sdcard/anim --target-root E:\
```

macOS/Linux example:

```bash
cd firmware/esp32-s3
python tools/sync_anim_sdcard.py --source-dir release/V2.3.0/sdcard/anim --target-root /Volumes/WATCHER_SD
```

Use `--no-clean` only when you intentionally want to preserve existing files. The default clean copy is safer for repeatable setup.

## Verification Checklist

- `anim/anim_manifest.bin` exists.
- At least one `.animpack` exists for `boot`, `standby`, or another expected behavior.
- The sync script completed without a hash mismatch.
- The SD card is safely ejected before inserting it into the robot.
- The first action smoke test can trigger a behavior without missing-asset errors.
