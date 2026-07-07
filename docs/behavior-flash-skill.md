# Behavior Asset Field Checklist

Use this checklist at an event bench when preparing or replacing a WatcheRobot SD card.

## Inputs

- WatcheRobot repository checkout
- Python 3.11+
- `Pillow` installed
- SD card mounted on the host machine
- GIF behavior sources in `firmware/esp32-s3/assets/gif`
- Target firmware version noted in `docs/versions.md`

## Procedure

1. Confirm the SD card mount path.
   - Windows example: `E:\`
   - macOS example: `/Volumes/WATCHER_SD`
   - Linux example: `/media/$USER/WATCHER_SD`

2. Generate behavior assets.

```bash
cd firmware/esp32-s3
python tools/generate_anim_assets.py --input-dir assets/gif --output-dir release/V2.3.0/sdcard/anim --fps 10
```

3. Confirm generated output.

```text
release/V2.3.0/sdcard/anim/anim_manifest.bin
release/V2.3.0/sdcard/anim/*.animpack
```

4. Sync to the SD card.

```bash
python tools/sync_anim_sdcard.py --source-dir release/V2.3.0/sdcard/anim --target-root <SD_CARD_ROOT>
```

5. Safely eject the SD card.

6. Insert the SD card into the robot before boot.

7. Run `docs/action-test.md`.

## Pass Criteria

- Sync script finishes without missing, extra, or hash-mismatch errors.
- SD card contains `anim/anim_manifest.bin`.
- Robot boot does not report missing animation assets.
- At least one behavior can be triggered during the first action smoke test.

## Failure Notes

If an animation is missing, regenerate from the source GIF directory and sync with the default clean behavior. If the sync script cannot find the generated source directory, pass `--source-dir` explicitly.
