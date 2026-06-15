# GIF Animation Roadmap

## Baseline

The current branch baseline is complete and validated for:

- GIF source ingestion
- `animpack` generation
- SD-card sync workflow
- SD-backed boot animation
- Runtime GIF switching with visible text overlay

## Phase 1: Completed

- Replace PNG-sequence runtime playback with SD-backed `animpack` streaming.
- Move boot animation onto the SD animation path.
- Add a GIF source directory and offline conversion entrypoint.
- Add an SD sync utility for generated animation assets.
- Fix FATFS long file name support for `anim_manifest.bin` and `*.animpack`.
- Fix the transient blank frame / `No data` issue during animation switches.
- Restore the UI text overlay so text remains readable above the active GIF.

## Phase 2: Release Packaging Completed

- Promote the GIF / AnimPack branch baseline into the maintained GitHub Release track.
- Package firmware flash binaries and SD-card animation assets as separate
  GitHub Release assets.
- Refresh top-level release documentation so `V2.2.1` is the current
  baseline for validation and distribution.

## Phase 3: Next

- Refresh action data alongside the final animation pack so behavior playback
  matches the new GIF-authored state set.
- Add a single branch-level asset configuration file so FPS, loop policy, and
  optional aliases are not encoded only in script defaults.
- Improve runtime diagnostics for missing or malformed `animpack` files with
  clearer file-path and type-level logs.
- Add a lightweight smoke-test checklist for boot animation, standby text, and
  multi-state animation switching after each animation asset refresh.
- Decide whether `anim_meta.json` should remain optional or become a generated
  artifact in the toolchain.

## Phase 4: Robustness

- Add explicit SPI2 arbitration if the AI / SSCMA camera path is re-enabled in
  normal runtime scenarios.
- Add stronger asset validation, including frame-size mismatches, unsupported
  GIF inputs, and manifest / pack consistency checks.

## Phase 5: Future Optimization

- Evaluate optional per-frame independent compression if SD footprint becomes a
  practical problem.
- Consider authoring-time metadata such as transition hints or per-state text
  positioning only after the current baseline stays stable.
- Revisit animation timing normalization if product wants different per-state
  feel without editing the original GIF frame delays.

## Out Of Scope For The Current Baseline

- Runtime GIF decoding on-device
- Global first-frame warm cache for every animation type
- Cross-frame delta compression in the runtime pack format
- High-FPS optimization as the primary goal over stable playback
