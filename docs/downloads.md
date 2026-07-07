# Downloads

All release artifacts are published through GitHub Releases for `orulink-ai/WatcheRobot`.

## Current Status

No public GitHub Release is currently listed for this repository. Until the first activity package is published, participants should use the local build and SD-card asset generation fallbacks documented in:

- `README.md`
- `docs/flashing.md`
- `docs/sd-card-assets.md`

## Required Assets for the First Public Activity Release

The first public activity release should include:

- ESP32 firmware flash ZIP
- ESP32 SD-card behavior asset ZIP
- STM32 firmware package when available
- hardware package ZIP when available
- `SHA256SUMS.txt`
- release notes
- compatibility notes linked to `docs/compatibility.md`

Full product releases may additionally include:

- Android app APK
- server Windows package
- desktop Windows installer

Do not download app, server, or desktop source code from this repository; those components are distributed only as release artifacts.

## Artifact Rules

- Do not commit `.apk`, `.aab`, `.exe`, `.msi`, `.dmg`, `.zip`, `.bin`, `.elf`, `.map`, or `.hex` files to Git.
- Upload release artifacts to GitHub Releases.
- Record checksums in `SHA256SUMS.txt`.
- Update `docs/versions.md` and `docs/compatibility.md` for every public release.
