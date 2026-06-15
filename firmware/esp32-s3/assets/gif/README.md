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

Legacy names such as `watcher-boot.gif` are still accepted for compatibility.

Typical workflow from `firmware/esp32-s3`:

```powershell
python tools/generate_anim_assets.py
python tools/sync_anim_sdcard.py --target-root F:\
```

Generated output is written to:

- `build/generated/sdcard/anim/`

Current release note:

- The runtime pipeline is already integrated into the current release flow.
- The current packaged animation set includes 29 generated types.
- `custom1`, `custom2`, `custom3`, and `standby1` through `standby4` are packaged in the current release.
- Extended WebSocket-callable animations such as `disconnect`, `sad`, `blink`,
  `upgrade`, `fondle_love`, and `fondle_anger` are also packaged in the current release.

For the full branch guide and roadmap, see:

- `docs/GIF_ANIMATION_BRANCH_GUIDE.md`
- `docs/GIF_ANIMPACK_TOOLCHAIN.md`
- `docs/GIF_ANIMATION_ROADMAP.md`
