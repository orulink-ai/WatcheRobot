# ESP32 Resource Lifecycle and Regression Testing

This document describes the application resource model introduced for the S3
firmware and the repeatable tests that protect it.

## Runtime resource model

Each `watcher_app_t` declares an explicit resource set. The runtime reconciles
directly to the target set when switching apps; it does not transition through
a global `OFF` state first.

Current resource bits cover:

- Wi-Fi station
- BLE and provisioning
- cloud transport
- audio
- MCU runtime
- App.Center

Wi-Fi is platform infrastructure and is always retained. BLE and provisioning
therefore run as `wifi+ble` and `wifi+ble+provisioning`, rather than taking the
station offline.

Resource application is compensating-transaction based. If applying the next
app's resources fails, the previous resource set and app are restored. A failed
close similarly reapplies the previous set and reopens the app.

Apps marked `WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE` can defer destruction by
returning an error from `on_destroy`. The runtime records a pending destroy,
retries it from `watcher_app_tick()`, and prevents the app from reopening until
the previous background work is idle.

## Memory policy

- App.Center is disabled in the default product configuration with
  `CONFIG_WATCHER_APP_CENTER_ENABLE=n`. A compatibility stub keeps dependent
  APIs linkable. Dedicated App.Center builds can enable the complete source.
- Behavior catalog and action arrays use explicit PSRAM allocation. Reloads
  parse a complete temporary states/actions bundle and only swap it into the
  live context after the full load succeeds.
- Main and SFX task stack sizes are driven by observed high-water marks. The
  Launcher stable snapshot logs main, Behavior, SFX, and power-button HWM.
- Launcher screen caching uses internal-free, largest-block, and DMA-largest
  thresholds. Voice and Agent targets require an additional 40 KiB total and
  16 KiB contiguous reserve before the Launcher screen may be retained.

## One-command host regression

Run all resource-related host suites from the ESP32 repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\firmware\s3\tools\run-resource-host-tests.ps1
```

The script configures, builds, and executes:

- main runtime, lifecycle, cache policy, App.Center and static architecture
  tests;
- Behavior parser, scheduler, PSRAM failure and motion-sequence tests;
- SFX scheduling/startup tests;
- MCU motion service tests.
- Provision Manager state-machine and App adapter contract tests.

The current suite contains 18 CTest targets: main 11, Behavior 3, SFX 2, MCU
motion 1, and Provision Manager 1.

## Static resource budget CI

The firmware build generates ESP-IDF size JSON and validates it with:

```powershell
python .\firmware\s3\tools\check_resource_budget.py `
  --size-json .\firmware\s3\build\resource-size.json `
  --budget .\firmware\s3\tools\resource_budget.json
```

The reviewed budget applies three independent constraints where relevant:

- a checked-in baseline for visible growth accounting;
- a maximum amount that one PR may grow beyond that baseline;
- an absolute hard ceiling that cannot be bypassed by accumulated small growth.

The gate currently covers static internal data/BSS, total static internal RAM,
instruction RAM, non-RAM flash, and aggregate linked image size. ESP-IDF 5.2.1
classifies some instruction sections differently on Windows and Linux, so the
checker uses cross-platform derived totals instead of raw `used_iram` and
`used_diram` fields. Increasing a baseline is an explicit source change and must
include map evidence and a resource owner in the PR description.

Static IRAM currently has only one byte of linker margin. This is existing debt,
not an acceptable target. CI allows zero IRAM growth until a dedicated cleanup
restores meaningful headroom.

## Firmware build verification

Default product build:

```powershell
Set-Location .\firmware\s3
idf.py -B build-resource-opt `
  -D SDKCONFIG=sdkconfig.no-wake `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.no-wake.defaults" `
  build
```

App.Center compatibility must also be checked periodically with
`CONFIG_WATCHER_APP_CENTER_ENABLE=y` in a dedicated build directory. Restore the
default disabled configuration before producing the product artifact.

## Hardware-in-loop acceptance

Use Launcher-after-settle as the memory baseline. Record all of the following:

- internal free bytes;
- largest internal block;
- largest DMA-capable block;
- PSRAM free bytes;
- task stack HWM;
- active app and explicit resource set.

For every app, compare `Launcher -> app -> Launcher` after both Launcher samples
have settled. A measured cycle passes when internal-free, largest-block, and
DMA-largest losses are each no more than 12 KiB and no runtime fault occurs.

The July 2026 COM8 verification produced:

- cold Launcher: 155 KiB internal free, 84 KiB largest internal/DMA block;
- full-feature warm Launcher after Voice initialization: 125 KiB internal free,
  58 KiB largest internal/DMA block;
- Settings, BLE, Provisioning, Client, and warmed Voice cycles recovered within
  the 12 KiB contract;
- no panic, assertion, watchdog, heap-corruption, or brownout event.

Treat the warm Launcher numbers as the steady-state product baseline. The higher
cold values are useful for detecting startup regressions but should not be used
to classify the one-time Voice runtime initialization as a repeated leak.
