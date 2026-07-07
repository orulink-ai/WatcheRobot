# Contributing

Thanks for helping improve WatcheRobot. This repository is public so event participants can fork it, test changes, and open pull requests without requesting extra access.

## Fork and Branch

1. Fork `orulink-ai/WatcheRobot`.
2. Clone your fork.
3. Create a focused branch.

Recommended branch names:

| Work type | Branch pattern |
| --- | --- |
| Hackathon team work | `hackathon-2026/<team-name>` |
| Documentation | `docs/<topic>` |
| Firmware | `firmware/<topic>` |
| Hardware package | `hardware/<topic>` |
| Release docs or manifests | `release/<topic>` |

Keep one pull request focused on one change area whenever possible.

## Pull Request Titles

Use a short prefix:

- `docs: ...`
- `firmware: ...`
- `hardware: ...`
- `release: ...`
- `tools: ...`

Examples:

- `docs: add SD-card behavior asset checklist`
- `hardware: document spare parts for wireless charging base`
- `firmware: update ESP32 behavior state notes`

## What Not to Commit

Do not commit:

- release artifacts: `.bin`, `.zip`, `.exe`, `.msi`, `.dmg`, `.apk`, `.aab`
- local build outputs or generated firmware images
- Wi-Fi credentials, API keys, tokens, private keys, or `.env` files
- local machine paths, private serial-port notes, or event bench-only COM-port logs
- source code for closed app, server, or desktop packages

Release binaries belong in GitHub Releases, not Git history.

## Documentation Expectations

If your change affects setup, flashing, behavior assets, hardware, or public protocols, update the matching document:

- Quick Start: `README.md`
- Flashing: `docs/flashing.md`
- SD-card behavior assets: `docs/sd-card-assets.md`
- First action smoke test: `docs/action-test.md`
- SDK and protocol boundary: `docs/sdk.md`
- Versions and compatibility: `docs/versions.md`, `docs/compatibility.md`
- Hardware and BOM: `hardware/README.md`, `hardware/pcb/spares.md`

## Validation Before PR

Run the checks that apply to your change:

- Documentation-only changes: check links and run the repository policy scans when possible.
- ESP32 firmware changes: build with ESP-IDF v5.2.1.
- STM32 firmware changes: run `cmake --preset HostDebug`, `cmake --build --preset HostDebug`, and `ctest --preset HostDebug`.
- Flashing or behavior asset changes: complete the checklist in `docs/action-test.md`.

If a required tool is not available in your environment, state that clearly in the PR test plan.
