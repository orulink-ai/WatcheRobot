# WatcheRobot ESP32-S3 V3.1 Release Guide

This guide prepares the Animation Controller V2 and Watcher SDK baseline as the
`V3.1` ESP32-S3 release candidate.

## Release Coordinates

- Source lineage: `codex/animation-controller-v2`
- Release-prep branch: `codex/esp32-v3.1-release-prep`
- Target version: `V3.1`
- Previous formal package in this lineage: `V2.4.1`
- ESP-IDF: `v5.2.1`

The release-prep branch may be pushed before hardware smoke. Do not create an
immutable `V3.1` tag or publish a GitHub Release until every promotion gate is
recorded as passing.

## Scope

V3.1 packages the following product changes:

- Ticket-driven, single-writer Animation Service with a generated Registry.
- Fixed animation frame pools, lifecycle recovery, prepare watchdogs, metrics,
  and bounded queue/stack memory.
- Event-driven Agent standby-entry, standby-loop, standby-exit, and listening
  transitions tied to committed display frames.
- Watcher SDK capability APIs and Python SDK device entry for behavior,
  animation, motion, audio, light, microphone, and camera control.
- Versioned SD resource metadata containing Animation Registry count and
  fingerprint compatibility fields.
- Provisioning and application resource lifecycle hardening.

## Automated Gates

Run from the repository root:

```powershell
firmware\s3\tools\run-resource-host-tests.ps1 -Configuration Debug

cmake -S firmware\s3\components\sdk\watcher_sdk\test_support\host `
  -B firmware\s3\components\sdk\watcher_sdk\test_support\host\build-release
cmake --build firmware\s3\components\sdk\watcher_sdk\test_support\host\build-release --config Debug
ctest --test-dir firmware\s3\components\sdk\watcher_sdk\test_support\host\build-release `
  -C Debug --output-on-failure

python -m pytest tools\tests tools\win_flasher\tests

Push-Location tools\esp32-debug-console
npm test
Pop-Location
```

Build the release firmware with the installed ESP-IDF Python environment:

```powershell
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.2_py3.11_env'
. 'C:\Espressif\frameworks\esp-idf-v5.2.1\export.ps1'
Set-Location firmware\s3
$project = (Get-Location).Path
idf.py -B build-v3.1-release-no-wake `
  -D "SDKCONFIG=$project\sdkconfig.no-wake" `
  -D "SDKCONFIG_DEFAULTS=$project\sdkconfig.defaults;$project\sdkconfig.no-wake.defaults" `
  build
```

The build must report `App "WatcheRobot-S3" version: V3.1` and generate:

- `build-v3.1-release-no-wake\bootloader\bootloader.bin`
- `build-v3.1-release-no-wake\partition_table\partition-table.bin`
- `build-v3.1-release-no-wake\ota_data_initial.bin`
- `build-v3.1-release-no-wake\WatcheRobot-S3.bin`
- `build-v3.1-release-no-wake\srmodels\srmodels.bin`
- `build-v3.1-release-no-wake\storage.bin`
- `release\V3.1\sdcard\anim\anim_manifest.bin`
- `release\V3.1\sdcard\resource_manifest.json`

## Package Layout

The firmware ZIP uses the dual-OTA partition layout:

```text
0x0      bootloader.bin
0x8000   partition-table.bin
0xf000   ota_data_initial.bin
0x20000  WatcheRobot-S3.bin
0x820000 srmodels.bin
0x870000 storage.bin
```

Expected release files:

- `firmware/s3/release/V3.1/WatcheRobot-S3-V3.1-esp32s3.zip`
- `firmware/s3/release/V3.1/WatcheRobot-S3-V3.1-sdcard-anim.zip`
- `firmware/s3/release/V3.1/RELEASE_NOTES.md`
- `firmware/s3/release/V3.1/CHANGELOG.md`
- `firmware/s3/release/V3.1/SHA256SUMS.txt`

Validate discovery and parsing before hardware use:

```powershell
python -m tools.win_flasher list-releases
python -m tools.win_flasher flash `
  --zip firmware\s3\release\V3.1\WatcheRobot-S3-V3.1-esp32s3.zip `
  --port COMx
```

## Hardware Promotion Gates

After flashing the packaged ZIP and copying the SD bundle to the card, capture
at least 60 seconds of serial output and verify:

- Boot reports `V3.1` and completes without watchdog, stack, heap, mount, or
  display failures.
- SD resource bundle loads 35 animation entries and reports the expected
  Registry fingerprint relationship.
- Launcher opens Python SDK, shows a pairing code, accepts one SDK command, and
  returns without leaked resources.
- Agent entry, standby loop, wake transition, listening first frame, recording,
  and exit complete without animation stalls.
- One motion command and one LED command complete through the MCU path.
- Phone Control, Settings, and App.Center open and return successfully.

## Publish And Rollback

Push the release-prep branch for review. Create the annotated `V3.1` tag and
GitHub Release only after the hardware gates pass and the uploaded assets match
`SHA256SUMS.txt`.

If a gate fails, keep the branch as a release candidate, fix the issue in a new
commit, rebuild both ZIPs, and regenerate all checksums. Never replace assets
behind an already published immutable tag.
