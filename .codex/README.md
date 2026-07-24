# Codex Workspace Notes

This directory is reserved for Codex-assisted firmware workflow runtime files.
Public flashing templates live under `tools/flashing/`.

## Local-Only Files

Create machine-specific device maps outside the repository, then point every worktree at it with `CODEX_DEVICE_MAP_PATH` when needed.

- `$env:USERPROFILE\.watche-robot\device-map.toml`
  Maps logical device aliases like `s3-a` to the local serial port on this machine.
- `.codex/local/lanes/`
  Runtime lane metadata emitted by `tools/run-lane.ps1`.
- `.codex/local/logs/`
  Optional transcripts and session logs emitted by `tools/run-lane.ps1`.

The device map should live outside individual worktrees so every checkout sees the same alias-to-port mapping. Start from `tools/flashing/device-map.example.toml`.

## Tracked Templates

- `tools/flashing/device-map.example.toml`
  Device alias template. Copy it to `$env:USERPROFILE\.watche-robot\device-map.toml` and fill in local ports.
- `tools/flashing/lanes.example.yaml`
  Shared lane table example. Keep only device aliases here, never raw COM ports.
