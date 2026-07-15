# realtime_client

OpenAI Realtime-compatible WebSocket client for `agent.app`.

## Purpose

- Connect to the local Hugging Face `speech-to-speech` endpoint at `/v1/realtime`.
- Send `session.update` and `input_audio_buffer.append` events.
- Decode transcription, assistant text, audio delta, response completion, and error events.

## Notes

- This component only owns protocol transport and event callbacks.
- UI, microphone lifecycle, and playback queue ownership stay in `app_main` and `agent_audio_player`.
- Turn ending is implemented by appending trailing silence for server VAD compatibility with the HF Realtime server.
