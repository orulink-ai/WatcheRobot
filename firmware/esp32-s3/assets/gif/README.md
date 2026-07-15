# GIF Sources

Drop animation source GIFs here for the `generate_anim_assets.py` toolchain.

Recommended canonical filenames:

- `boot.gif`
- `happy.gif`
- `error.gif`
- `bluetooth.gif`
- `speaking.gif`
- `listening.gif`
- `processing.gif`
- `standby.gif`
- `thinking.gif`
- `custom1.gif`
- `custom2.gif`
- `custom3.gif`
- `standby1.gif`
- `standby2.gif`
- `standby3.gif`
- `standby4.gif`
- `disconnect.gif`
- `shock.gif`
- `sunglasses.gif`
- `sad.gif`
- `get.gif`
- `smile.gif`
- `recharge.gif`
- `speechless.gif`
- `concentration.gif`
- `fondle_love.gif`
- `fondle_anger.gif`
- `blink.gif`
- `upgrade.gif`
- `standby_start.gif`
- `standby_loop.gif`
- `standby_end.gif`
- `music.gif`

Legacy names such as `watcher-boot.gif` are still accepted for compatibility.

Typical workflow from `firmware/s3`:

```powershell
python tools/generate_anim_assets.py
python tools/sync_anim_sdcard.py --target-root F:\
```

Generated output is written to:

- `release/V2.4.1/sdcard/resource_manifest.json`
- `release/V2.4.1/sdcard/anim/`

Use `--resource-version res-YYYY.MM.DD.N` when a specific SD resource bundle
version should be written. If omitted, the generator writes an automatic daily
version such as `res-2026.07.08.1`.

Current release note:

- The runtime pipeline is already integrated into the current release flow.
- The current packaged animation set includes 33 generated types.
- `standby_start`, `standby_loop`, and `standby_end` support the Voice App sleep/wake expression flow.
- `music` is packaged for the new music expression.
- `custom1`, `custom2`, `custom3`, and `standby1` through `standby4` are packaged in the current release.
- Extended WebSocket-callable animations such as `disconnect`, `sad`, `blink`,
  `upgrade`, `fondle_love`, and `fondle_anger` are also packaged in the current release.

For the full branch guide and roadmap, see:

- `docs/GIF_ANIMATION_BRANCH_GUIDE.md`
- `docs/GIF_ANIMPACK_TOOLCHAIN.md`
- `docs/GIF_ANIMATION_ROADMAP.md`
