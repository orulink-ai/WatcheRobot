# Animation Controller V2 Architecture

## Goal

Animation Controller V2 makes the displayed expression converge on the robot's real business state. Normal animation
transitions are driven by player facts (`COMMITTED`, `CYCLE_COMPLETED`, `COMPLETED`, or `FAILED`), never by guessed
asset durations.

This refactor changes the ESP32 animation control plane only. The existing AnimPack v2 format, SD streaming worker,
PSRAM ring buffering, generation checks, per-frame timing, and direct-LCD renderer remain the playback data plane.

## Ownership

```text
Agent / Voice / WS / Power
             |
             v
Behavior State Service       owns business intent and state transitions
             |
             v
Animation Controller         owns tickets, arbitration, priority, and lifecycle
             |
             v
Anim Player                  owns asynchronous preparation and frame playback
          /     \
         v       v
Anim Storage   HAL Display   own AnimPack I/O and the physical display surface
```

The dependency direction is one-way:

```text
behavior_state_service -> anim_service -> hal_display
```

`hal_display` must not depend on `anim_service`, start animations, or contain animation preemption policy. Business
modules must not include the private player header or call player start/stop functions directly.

## Request lifecycle

Every accepted request receives a non-zero ticket and follows this lifecycle:

```text
ACCEPTED -> PREPARING -> COMMITTED -> CYCLE_COMPLETED* -> COMPLETED
                                                    \-> PREEMPTED
                                                    \-> CANCELLED
                                                    \-> FAILED
```

`COMMITTED` means the first correct frame is visible, not merely that an SD read was queued. Each accepted ticket gets
exactly one terminal event: `COMPLETED`, `PREEMPTED`, `CANCELLED`, or `FAILED`.

Timeouts are health checks. A preparation watchdog emits `FAILED(PREPARE_STALLED)` and follows the configured failure
transition; it must never impersonate `COMPLETED`.

## Arbitration

Priorities, from low to high, are `AMBIENT`, `BEHAVIOR`, `INTERACTION`, and `SYSTEM`.

- `SYSTEM` may preempt every request.
- A higher priority request preempts a lower priority preemptible request.
- Same-priority preemptible requests use latest-wins; the displaced ticket terminates as `PREEMPTED`.
- `standby_start` and `standby_end` are protected after `COMMITTED`. Non-system requests update the deferred final
  target and wait for the protected request to complete.
- Lower-priority deferred requests coalesce by source, owner epoch, and priority. Coalescing never silently discards an
  accepted ticket.
- Queue and ticket storage are fixed-capacity. Queue-full rejection happens before `ACCEPTED` and does not create a
  ticket.

## Behavior contract

Each behavior state owns at most one primary animation. State-to-state animation changes use lifecycle events.
`repeat_count` requests an exact number of full cycles; zero uses the asset default. `on_animation_complete` and
`on_animation_failed` select the next state.

Motion and sound timelines may still use elapsed time. Product idle time may also use a timer. An animation must not
switch to another animation because a hard-coded approximation of its duration elapsed.

The initial standby flow plays `standby` for two complete cycles and then enters `standby_loop`. A failed wake
transition enters `listening` so recording is never blocked; a failed sleep transition enters `standby_loop`.

## Failure behavior

- Pending load failure preserves the last valid visible frame.
- If no valid frame exists, the display shows a built-in static error surface that does not depend on the SD card.
- Player failures distinguish invalid resource, missing surface, memory exhaustion, SD open/read failure, corrupt pack,
  stalled preparation, render failure, and service shutdown.
- Stale worker generations are ignored and cannot commit to a newer ticket.
- Event values are created under the player lock and delivered after releasing it. Controller and Behavior callbacks
  must never execute while the player mutex is held.

## Observability

Structured lifecycle logs include ticket, source, owner epoch, priority, desired type, preparing type, visible type,
phase, failure reason, latency, frame index, queue depth, and memory snapshot. Debug commands provide status, explicit
play/cancel, and deterministic switch stress.

The release gate requires zero wrong commits, orphan events, normal-path fallback timeouts, black frames, draw failures,
or missing terminal events in the core sleep-to-speech journey.
