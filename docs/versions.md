# Version Tracking

Use this document to find the source of truth for each public WatcheRobot component.

## Current Sources

| Component | Current public source | Version signal | Notes |
| --- | --- | --- | --- |
| WatcheRobot repository package | Root `VERSION` | `0.1.0` | Current public source package version for this repository. |
| Repository default branch | GitHub `main` | Git commit SHA | Public release readiness is gated by CI and release assets. |
| ESP32-S3 firmware | `firmware/esp32-s3/CMakeLists.txt` | `PROJECT_VER`, currently `V2.3.0` on this branch | `origin/dev` may carry newer firmware work that is not merged to `main`. |
| ESP32 behavior assets | `firmware/esp32-s3/tools/generate_anim_assets.py` | `PROJECT_VERSION`, currently `V2.3.0` on this branch | Must match the firmware release package when publishing. |
| STM32F103 firmware | `firmware/stm32-f103/` | Snapshot and Git SHA | No public semantic firmware version is finalized in this repository yet. |
| Hardware PCB package | filenames under `hardware/pcb/` | dated exports, mostly 2026-06-11 to 2026-06-18 | BOM, CPL, schematic, layout, and Gerber files should be kept in sync by board. |
| Mechanical model | `hardware/3d-models/exports/` | dated STEP filename | The current STEP is close to GitHub's 100 MB single-file limit. |
| App, Server, Desktop | GitHub Release assets when available | release asset version | Source code is not part of this repository. |

## Release Version Rules

- Every public release must update `docs/compatibility.md`.
- Firmware releases must state the ESP32 firmware version, STM32 snapshot, behavior asset package version, and hardware compatibility.
- Full product releases must include App, Server, Desktop, ESP32, STM32, behavior asset, and hardware compatibility notes.
- Release artifacts must include `SHA256SUMS.txt`.

## Known Current State

- Current public source package version is `0.1.0`.
- No public GitHub Release is currently listed.
- `docs/compatibility.md` records the open compatibility matrix format.
- Cross-end protocol contract gaps are tracked in [GitHub issue #5](https://github.com/orulink-ai/WatcheRobot/issues/5).
