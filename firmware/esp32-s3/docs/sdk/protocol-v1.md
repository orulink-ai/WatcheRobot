# SDK Control Protocol v1

`sdk.control.app` is the only foreground controller for the Python SDK path. It displays a new temporary six-digit
code on each session, discovers a desktop gateway on `37021/UDP`, then connects to the announced WebSocket port
(`8766` by default).

The protocol reuses the Watcher `{type, code, data}` envelope, `command_id`, `sys.ack`/`sys.nack`, WebSocket
ping/pong, and WSPK media packets. Authentication negotiates protocol `1.0`, device ID, firmware version, and these
capabilities: `behavior`, `animation`, `motion`, `audio`, `audio.stream`, `light`, `microphone`, and
`camera.capture`, plus `input.back_touch`, `input.screen_touch`, and `input.roller` for physical input events.

ACK means accepted. A finite operation reports `starting`, `running`, and one terminal state using
`evt.sdk.operation` and `operation_id`. Repeated `command_id` values return the cached response rather than executing
the command twice.

Protocol parsing is strict at the firmware boundary. Oversized identifiers are rejected instead of truncated;
pairing codes must be exactly six digits; numeric fields, enum values, camera dimensions, and media formats must be
within the documented v1 range. A parseable invalid command receives `sys.nack` with `invalid_argument` or
`unsupported_command`. If the bounded command queue is full, the device returns `command_queue_full`.

For `ctrl.motion.move_to`, `completed` is emitted only after the STM32 reports the matching `MOTION_DONE` command
sequence. `stopped` and `interrupted` become `cancelled`; MCU rejection, fault, or a missing terminal event become
`failed`. This confirms completion of the STM32 execution timeline, not closed-loop verification that the mechanism
physically reached the requested angle.

`ctrl.camera.capture` acknowledges a media session before the blocking HX6538 capture work starts. A cold camera
session discards two warm-up frames with a 500 ms sensor-settle interval so auto exposure can converge, then sends
one JPEG WSPK image frame. When width and height are omitted, the device uses the `640x480` sensor profile.

Host audio streaming is an authenticated data plane. The host first sends `ctrl.audio.stream.begin` with a non-zero
`stream_id`, exact PCM byte count, SHA256, and the fixed v1 format (S16LE/24 kHz/mono). Only that stream is accepted
by the SDK App's WSPK guard. Binary frames use the 16-byte header, monotonic sequence, and FIRST/LAST flags. The
existing TTS queue remains the single playback executor; the SDK App owns only authorization, preemption, and
lifecycle adaptation.

`evt.audio.buffer_status` correlates terminal status to the originating non-zero stream ID. Python uses device queue
statistics for a bounded send window. Stop, replacement, disconnect, session reset, or App close revoke the stream
and clear playback. Queue/sequence errors, a short hardware write, or SHA mismatch are not successful completion.

Authenticated physical input is forwarded as `evt.sdk.input`:

- rear touch: `source=back_touch`, `action=press|release|long_press`, `touch_id`, and the STM32 timestamp;
- screen: `source=screen_touch`, `action=tap`, display `x`/`y`, and the ESP32 timestamp;
- roller: `source=roller`, `action=rotate`, signed accumulated `delta`, and the ESP32 timestamp.

The SDK App uses a bounded newest-event queue, so input never blocks hardware/event tasks. When no authenticated SDK
session owns rear touch, the existing local fondle Behavior remains active. Roller short click still owns local App
exit and long hold still owns system shutdown; only rotation is forwarded to Python.

On disconnect or app close, the SDK context is destroyed, all Jobs are cancelled, media is closed, outputs are
stopped, and a new pairing code is generated. v1 is plain `ws://` for trusted LANs; TLS, durable trust, remote wake,
continuous video, and resource upload are deferred.

For message examples and the complete command table, see the Python SDK's `docs/protocol-v1.md`.
