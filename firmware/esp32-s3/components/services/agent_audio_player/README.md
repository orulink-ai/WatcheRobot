# agent_audio_player

Queued PCM playback service for `agent.app` Realtime audio deltas.

## Purpose

- Buffer decoded `response.output_audio.delta` chunks in PSRAM.
- Play 24 kHz 16-bit mono PCM through `hal_audio`.
- Support stream completion and barge-in abort without writing audio from the WebSocket callback.

## Notes

- The service owns only the playback queue and worker task.
- The foreground app remains responsible for starting/stopping the service and updating UI state.
