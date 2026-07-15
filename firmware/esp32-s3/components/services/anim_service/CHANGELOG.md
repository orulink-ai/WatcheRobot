# Changelog

All notable changes to the Animation Service component will be documented in this file.

## [Unreleased]

### Changed
- Animation Registry is now the sole source for stable IDs, names, aliases,
  default loop policy, encoding and fallback FPS.
- Manifest v2 and AnimPack headers are authoritative for generated resource
  frame counts, loop policy and timing.
- Removed the obsolete UI emoji projection and runtime JSON metadata override.

## [2.1.0] - 2024-03-13

### Added
- RGB565 cache system for fast frame switching (<1ms latency)
- `anim_cache_*` API for PSRAM-based frame caching
- `emoji_anim_prefetch_type()` for proactive cache loading
- `emoji_anim_get_fps()` / `emoji_anim_set_fps()` runtime FPS control
- Kconfig options for animation parameters
- Performance logging (configurable via `WATCHER_ANIM_DEBUG_PERF`)

### Changed
- Default frame rate increased from ~6.7fps (150ms interval) to 30fps (33ms interval)
- Maximum frames per type increased from 10 to 18
- Animation now uses RGB565 cache by default for smooth playback
- Timer reuses existing handle instead of recreating on each start

### Fixed
- Frame switching latency reduced from ~150ms (PNG decode) to <1ms (cached RGB565)

## [2.0.0] - 2024-02-XX

### Added
- Initial release with PNG-based animation playback
- Support for 7 animation types
- LVGL timer-based frame cycling
- SPIFFS-based image loading

### Notes
- Frame rate limited to ~6.7fps due to PNG decode overhead
