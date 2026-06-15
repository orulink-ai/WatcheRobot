# GIF Animation Refactor Plan

This document remains the architecture baseline for the refactor.

Current release note:

- `V2.2.1` is the current packaged release baseline for this runtime architecture.
- The current release bundle contains 28 generated animation types.
- `custom1`, `custom2`, `custom3`, `standby1` through `standby4`, and the
  extended WebSocket-callable expressions are present in the packaged
  GIF-derived asset set.

For the current validated branch state, day-to-day workflow, and next steps, see
also:

- `GIF_ANIMATION_BRANCH_GUIDE.md`
- `GIF_ANIMPACK_TOOLCHAIN.md`
- `GIF_ANIMATION_ROADMAP.md`

## Current Implementation Status

- GIF-to-`animpack` generation is implemented and in active use.
- SD-backed boot animation is implemented.
- Runtime playback now streams `animpack` frames from SD instead of relying on
  full PNG-sequence hot caches.
- FATFS long file name support is enabled so the runtime can open
  `anim_manifest.bin` and `*.animpack`.
- The UI text overlay and the transient blank-frame / `No data` switch bug have
  both been fixed on this branch.

## Summary

- GIF is the source asset format only. The firmware will not decode GIF files at runtime.
- The build pipeline converts GIF assets into SD-card streamable `animpack` files plus `anim_manifest.bin` v2 metadata.
- Runtime playback removes the existing global warm first-frame cache and full-type hot cache model. The new steady-state model is a 3-frame ring buffer for the active animation only.
- Animation switching keeps the old stream playing until the new stream has prefetched at least 2 frames, with a target of 3 frames before committing the generation switch.
- SD card storage is a hard dependency for the animation system. Boot animation also moves onto the new SD-backed path.
- The design target is stable playback at 10 to 15 FPS rather than chasing 30 FPS.

## Implementation Changes

### Asset and Build Pipeline

- Add a new offline conversion tool that takes GIF inputs and emits:
  - one `animpack` file per animation type
  - `anim_manifest.bin` v2
- The converter performs all GIF-specific processing offline:
  - palette expansion
  - transparency compositing
  - disposal handling
- Runtime frame payloads are stored as complete self-contained RGB565 frames in LVGL-native byte order.
- `animpack` layout contains:
  - a fixed header
  - a frame table of contents with `offset`, `size`, and `delay_ms`
  - frame payload data stored sequentially in playback order
- Frames should be stored with predictable sequential layout, ideally using 512-byte alignment to match SD-card-friendly access patterns.

### Storage and Catalog Layer

- Replace the current PNG path catalog model in `anim_storage` with an SD-backed pack catalog model rooted at `/sdcard/anim`.
- Update catalog metadata to describe:
  - pack path
  - width and height
  - frame count
  - loop behavior
  - per-frame or default timing
- Remove reliance on arrays of PNG frame paths in runtime catalog structures.
- Add internal stream-oriented types such as:
  - `animpack_header_t`
  - `animpack_frame_desc_t`
  - `anim_stream_handle_t`
  - `anim_ring_slot_t`

### Runtime Playback Model

- Keep the top-level animation API as stable as possible:
  - `emoji_anim_init`
  - `emoji_anim_start`
  - `emoji_anim_stop`
  - `emoji_anim_show_static`
  - `emoji_anim_get_type`
  - `emoji_anim_is_switch_pending`
- Replace full-animation hot-cache construction with:
  - a producer that streams frames from `animpack`
  - a consumer that advances LVGL display objects from a 3-frame ring buffer
- Default steady-state runtime memory should only include the active ring buffer, not preloaded frames for inactive animation types.
- `emoji_anim_show_static(type, 0)` should open the corresponding pack and read the first frame on demand instead of depending on a resident warm cache.
- Switching logic should be:
  - keep old animation visible
  - open the new pack in the background
  - prefill at least 2 frames, target 3
  - atomically switch generation
  - release the old stream and its ring buffer

### Boot and Failure Handling

- Change startup order to:
  - minimal display init
  - SD mount
  - animation manifest load
  - boot animpack prefetch
  - boot animation playback
  - remaining system init
- If SD mount fails, the manifest is invalid, or boot assets are missing or corrupt:
  - show a built-in static error page
  - stop the normal startup path
- Runtime animation-load failures should prefer preserving the currently active animation when possible.

### Bus Arbitration and Scheduling

- SD card and SSCMA share `SPI2`; LCD is on `SPI3`.
- The main contention risk is SD reads versus SSCMA transfers, not SD reads versus LCD flushes.
- Add a unified `SPI2` arbitration layer for SD-backed animation streaming and SSCMA traffic.
- The animation producer must use sequential reads and avoid seek-heavy playback behavior.
- When the ring buffer reaches a low-water state or `SPI2` is heavily contended:
  - defer animation switching first
  - tolerate reduced effective playback FPS if necessary
  - never block the LVGL main thread on storage I/O

### Configuration and Limits

- Add configuration items for:
  - `WATCHER_ANIM_RING_FRAMES` with default `3`
  - `WATCHER_ANIM_TARGET_FPS` with default in the `10` to `15` range
  - `WATCHER_ANIM_SD_MOUNT_REQUIRED`
  - `WATCHER_ANIM_PACK_VERSION`
  - optional max-pack-size or max-frame-count guards
- The previous warm-budget and hot-budget settings should no longer drive the primary playback path.
- Remove the practical runtime dependency on `CONFIG_WATCHER_ANIM_MAX_FRAMES_PER_TYPE=24`; duration should be limited by pack size and explicit engineering limits instead of resident frame cache capacity.

## Public APIs / Types

### Stable External APIs

- Keep existing behavior-oriented calls available to upper layers:
  - `emoji_anim_init`
  - `emoji_anim_start`
  - `emoji_anim_stop`
  - `emoji_anim_show_static`
  - `emoji_anim_get_type`
  - `emoji_anim_is_switch_pending`

### Internal Interface Changes

- Replace hot-cache-oriented internals such as:
  - `anim_hot_build_type`
  - `anim_hot_commit_prepared`
  - `anim_hot_get_frame_count`
  - `anim_hot_get_frame`
- Introduce stream-oriented internals such as:
  - `anim_stream_open`
  - `anim_stream_prime`
  - `anim_stream_read_next`
  - `anim_stream_close`

## Test Plan

- Boot path:
  - verify normal boot animation when SD and manifest are valid
  - verify static error behavior when SD mount or boot asset load fails
- Playback stability:
  - verify continuous playback for the main runtime animation states
  - verify no crashes or underrun-induced corruption during long playback runs
- Switch behavior:
  - verify old animation stays visible until new animation has prefetched enough frames
  - verify no black frame, stale generation, or partially initialized display content
- Concurrency:
  - verify SD-backed animation playback while SSCMA traffic is active on `SPI2`
  - verify LVGL remains responsive and is never blocked by animation storage reads
- State transitions:
  - verify `voice_service` freeze and resume behavior still works without leaks
  - verify `emoji_anim_show_static(type, 0)` works without a global first-frame cache
- Resource integrity:
  - verify corrupt `animpack`, malformed manifest, oversized files, and inconsistent timing metadata fail predictably

## Assumptions

- GIF is only an offline asset source and is never decoded directly on-device at runtime.
- The v1 runtime format uses full self-contained RGB565 frames, not delta frames and not runtime transparency reconstruction.
- There is no global resident first-frame cache for all animation types.
- The default ring buffer depth is 3 frames.
- SD card storage is mandatory for the new animation system, including boot animation.
- Stable playback at 10 to 15 FPS is the primary design goal.
