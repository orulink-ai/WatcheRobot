# Wake Word + TTS Memory Notes

This note records the current memory guardrails for running WakeNet wake-word detection together with WebSocket TTS playback on the ESP32-S3 target.

## Runtime Constraints

- Internal and DMA-capable heap are the limiting resources; PSRAM is available but cannot replace every I2S/LVGL/AFE allocation.
- TTS audio arrives in bursty WebSocket binary messages. The local playback worker drains at the 24 kHz I2S rate, so the receive path must apply bounded backpressure.
- Wake-word runtime is sleep-standby scoped: Voice start only configures the lifecycle, sleep standby resumes WakeNet/AFE, and recording/TTS/active voice paths release WakeNet, AFE, the detection task, and the input buffer.
- TTS completion idles the shared audio path but does not immediately resume WakeNet; wake-word resume is deferred until Voice returns to sleep standby.
- On ESP32-S3 flash-mmap builds, the ESP-SR model list is intentionally retained across runtime restarts; do not expect active-state release to match the no-wake firmware heap watermark.
- Implicit local status SFX should not compete with cloud TTS on the shared
  audio path. Explicit WebSocket/desktop expression `sound_file` requests are
  allowed to trigger local SFX.

## Guardrails

- Text WebSocket frames are bounded by `CONFIG_WATCHER_WS_TEXT_MAX_PAYLOAD_BYTES` before allocation and JSON parsing.
- TTS binary frames are bounded by `CONFIG_WATCHER_WS_TTS_MAX_PAYLOAD_BYTES`.
- TTS frame slots are stored in PSRAM, with queue depth controlled by `CONFIG_WATCHER_WS_TTS_QUEUE_DEPTH`.
- TTS enqueue backpressure is capped per inbound payload by `CONFIG_WATCHER_WS_TTS_ENQUEUE_TIMEOUT_MS`; do not let one large payload block the WebSocket receive callback per chunk.
- Wake-word release is serialized by the lifecycle helper; feed/release/resume races must go through the lifecycle lock instead of directly touching the WakeNet context.
- Low-heap wake-word resume failures degrade only wake-word availability; button recording and cloud TTS must remain available.
- `behavior_state_set_with_resources()` uses explicit empty-string overrides:
  - `anim_id == ""` keeps the current animation and applies a text-only state/display update.
  - `sound_id == ""` suppresses local SFX for that state request.
- `evt.ai.status.sound_file` is preserved when non-empty so desktop expression
  presets can play their requested local SFX; omitted `sound_file` still maps to
  `sound_id == ""` to suppress implicit state-default sounds.
- Low-memory listening UI first attempts full listening state, then text-only listening state, and finally skips UI refresh if heap is critically fragmented.
- Ready-idle standby variants defer animation changes while internal/DMA largest blocks are below the configured thresholds; persistent pressure falls back to text-only standby instead of forcing an animation handoff.

## Log Signals

- `MEM_MON ... critical(internal_largest|dma_largest)` means the device is still running but largest contiguous internal/DMA blocks are below the warning threshold.
- `tts playback queue pressure` means inbound TTS is faster than playback. Small drops can be acceptable for stress tests, but sustained drops should be treated as a server pacing or queue-budget problem.
- `evt=voice_wake_resource stage=standby_init|released|resume_skip|init_fail` records internal free heap, internal largest block, DMA largest block, and PSRAM free for wake lifecycle decisions.
- `ESP-SR mmap model cache retained; AFE/task/input buffer are released` means the runtime resources were released while the mmap model cache remains process-scoped.
- `Ready idle deferred under memory pressure` is expected after TTS when animation memory is fragmented; the device should still accept button recording, and wake-word resume should occur once sleep standby has enough heap.

## Regression Checklist

- Build: `idf.py build`.
- Flash and monitor on the S3 target after touching WakeNet, TTS queueing, behavior state, SFX, or display fallback code.
- For no-wake-word validation, build with
  `SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.no-wake.defaults'` and
  confirm `srmodels.bin` is the 4-byte placeholder.
- Exercise button recording and wake-word recording in the same boot.
- Run at least three consecutive wake-word conversations through sleep standby: standby resume -> wake -> recording -> TTS -> release -> sleep standby resume.
- Confirm TTS completion does not immediately resume WakeNet; `standby_init` should appear when Voice enters sleep standby.
- Track internal free heap, internal largest block, and DMA largest block over at least 10 wake/release cycles; there should be no monotonic loss trend.
- Test one long TTS answer that produces queue pressure.
- Confirm the UI does not remain logically stuck in `happy` when recording starts under low memory.
- Confirm oversized text and TTS payloads are rejected without heap exhaustion.
