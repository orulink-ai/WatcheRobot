# Compatibility

Use this document to record known-good version sets. Every GitHub Release should update this table before publication.

## Current Public Matrix

| WatcheRobot release | App | Server | Desktop | ESP32-S3 firmware | SD-card behavior assets | STM32F103 firmware | Hardware |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Not yet published | Not provided | Not provided | Not provided | `V2.3.0` baseline on this branch | `V2.3.0` asset generator baseline | STM32 snapshot in `firmware/stm32-f103/` | PCB package dated 2026-06-11 to 2026-06-18 |

## Release Checklist

Before publishing a release:

- Set the WatcheRobot release tag.
- Record App, Server, and Desktop asset versions if those assets are included.
- Record ESP32 firmware version from `firmware/esp32-s3/CMakeLists.txt`.
- Record SD-card behavior asset version from the generation path or release ZIP.
- Record STM32 firmware Git SHA or package version.
- Record hardware package version and any required assembly notes.
- Add upgrade order and incompatibilities when needed.

See `docs/versions.md` for the version source-of-truth map.
