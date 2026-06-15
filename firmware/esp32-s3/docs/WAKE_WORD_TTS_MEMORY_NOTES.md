# Wake Word + TTS Memory Notes

This note records the current memory guardrails for running WakeNet wake-word detection together with WebSocket TTS playback on the ESP32-S3 target.

## Runtime Constraints

- Internal and DMA-capable heap are the limiting resources; PSRAM is available but cannot replace every I2S/LVGL/AFE allocation.
- TTS audio arrives in bursty WebSocket binary messages. The local playback worker drains at the 24 kHz I2S rate, so the receive path must apply bounded backpressure.
- Wake-word detection must stop before TTS playback and resume after TTS. AFE reset is only safe after the fetch/feed path is idle and the wake-word state lock is acquired.
- Implicit local status SFX should not compete with cloud TTS on the shared
  audio path. Explicit WebSocket/desktop expression `sound_file` requests are
  allowed to trigger local SFX.

## Guardrails

- Text WebSocket frames are bounded by `CONFIG_WATCHER_WS_TEXT_MAX_PAYLOAD_BYTES` before allocation and JSON parsing.
- TTS binary frames are bounded by `CONFIG_WATCHER_WS_TTS_MAX_PAYLOAD_BYTES`.
- TTS frame slots are stored in PSRAM, with queue depth controlled by `CONFIG_WATCHER_WS_TTS_QUEUE_DEPTH`.
- TTS enqueue backpressure is capped per inbound payload by `CONFIG_WATCHER_WS_TTS_ENQUEUE_TIMEOUT_MS`; do not let one large payload block the WebSocket receive callback per chunk.
- Wake-word stop skips AFE buffer reset when fetch is still active or the wake-word state lock is busy.
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
- `Wake word fetch still active ... skip AFE reset` is expected during tight TTS handoff races; it should not be followed by crashes or repeated wake-word failure.
- `Ready idle deferred under memory pressure` is expected after TTS when animation memory is fragmented; the device should still accept wake word or button recording.

## Regression Checklist

- Build: `idf.py build`.
- Flash and monitor on the S3 target after touching WakeNet, TTS queueing, behavior state, SFX, or display fallback code.
- For no-wake-word validation, build with
  `SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.no-wake.defaults'` and
  confirm `srmodels.bin` is the 4-byte placeholder.
- Exercise button recording and wake-word recording in the same boot.
- Run at least two consecutive wake-word conversations after TTS playback.
- Test one long TTS answer that produces queue pressure.
- Confirm the UI does not remain logically stuck in `happy` when recording starts under low memory.
- Confirm oversized text and TTS payloads are rejected without heap exhaustion.
