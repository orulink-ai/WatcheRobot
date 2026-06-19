# GIF Animation Branch Guide

This branch is the current source of truth for the GIF-based animation stack on
`WatcheRobot-S3`.

## Scope

- GIF is an offline authoring format only.
- The build pipeline converts GIF assets into SD-card-ready `animpack` files
  plus `anim_manifest.bin`.
- The runtime streams `animpack` frames from `/sdcard/anim` into a 3-frame ring
  buffer instead of decoding GIF or caching entire PNG sequences in RAM.

## Current Status

The branch has been brought to a hardware-validated baseline.

- SD-backed boot animation works.
- Runtime animation switching works from `animpack` assets on SD.
- UI text now renders above the active GIF animation.
- The transient blank frame / `No data` issue during GIF switches is fixed.
- FATFS long file name support is enabled so `anim_manifest.bin` and
  `*.animpack` resolve correctly.
- The current board-specific SDSPI path tolerates SD cards that reject
  `CMD59 CRC_ON_OFF` in SPI mode.
- The current packaged release contains 28 generated animation types.
- `custom1`, `custom2`, `custom3`, `standby1` through `standby4`, and the
  extended WebSocket-callable expressions are included in the current generated
  asset set.

## Runtime Layout

Authoring and generated assets live in these locations:

- Source GIFs: `firmware/s3/assets/gif/`
- Generated assets: `firmware/s3/release/V2.2.1/sdcard/anim/`
- Device runtime path: `/sdcard/anim/`

The runtime expects at least:

- `/sdcard/anim/anim_manifest.bin`
- `/sdcard/anim/boot.animpack`

If these files are missing or invalid, boot stops on the fatal boot error path.

## Document Map

Use these documents together:

- `GIF_ANIMATION_REFACTOR_PLAN.md`
  - architecture baseline and refactor intent
- `GIF_ANIMPACK_TOOLCHAIN.md`
  - day-to-day asset generation and SD sync workflow
- `GIF_ANIMATION_ROADMAP.md`
  - next milestones after the current validated baseline
- `assets/gif/README.md`
  - quick reminder for where GIF source files belong

## Validated Baseline

The current branch has been validated against this baseline:

- Boot path mounts SD, loads `anim_manifest.bin`, and starts the boot intro from
  `boot.animpack`.
- After boot, the main UI loads, the text overlay is visible, and animation
  transitions no longer flash white or show `No data`.
- Generated assets can be mirrored to an SD-card root with
  `tools/sync_anim_sdcard.py`.
- Release packaging now produces both a firmware flash bundle and an SD-card
  animation bundle for `V2.2.1`.

## Daily Workflow

From `firmware/s3`:

```powershell
python tools/generate_anim_assets.py
python tools/sync_anim_sdcard.py --target-root F:\
```

Then reinsert the SD card into the device and flash firmware if runtime code has
changed.

## Notes

- `anim_meta.json` is optional. The current runtime will log and continue with
  defaults when it is absent.
- The branch still uses fixed full-frame RGB565 payloads. This favors
  predictability and switch stability over SD-card footprint.
- SPI2 arbitration is not required for the current validated use case while the
  AI camera path remains inactive, but it remains a future roadmap item.
