# GIF to AnimPack Toolchain

This toolchain converts a folder of GIF animation sources into SD-card ready
animation assets:

- `release/V2.4.1/sdcard/resource_manifest.json`
- `release/V2.4.1/sdcard/anim/anim_manifest.bin`
- `release/V2.4.1/sdcard/anim/<type>.animpack`

## Source Layout

Place GIF sources in:

- `firmware/s3/assets/gif/`

Supported source names are the canonical animation names:

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

Legacy names such as `watcher-boot.gif` remain accepted by the converter, and
legacy PNG sequence folders are still supported as a fallback during the
transition period.

## Quick Command

From `firmware/s3`:

```powershell
python tools/generate_animation_registry.py --check
python tools/generate_anim_assets.py --input-dir assets/gif --output-dir release/V2.4.1/sdcard/anim --clean
```

If you only want the default project paths, the command can be shortened to:

```powershell
python tools/generate_anim_assets.py
```

Useful options:

```powershell
python tools/generate_anim_assets.py --fps 10 --clean
python tools/generate_anim_assets.py --input-dir assets/gif --output-dir release/V2.4.1/sdcard/anim
python tools/generate_anim_assets.py --lv-color-16-swap
python tools/generate_anim_assets.py --resource-version res-2026.07.08.1
```

## Copy To SD

To mirror the generated assets onto an SD-card root such as `F:\`:

```powershell
python tools/sync_anim_sdcard.py --target-root F:\
```

This copies the generated assets into `F:\anim` and verifies the result with
hash comparisons. If you omit `--source-dir`, the script uses the latest
generated `release/*/sdcard/anim` directory it can find.

The SD-card root should end up with this layout:

```text
<sd-root>/
  resource_manifest.json
  anim/
    anim_manifest.bin
    boot.animpack
    happy.animpack
    ...
```

## End-To-End Workflow

1. Put GIF sources into `firmware/s3/assets/gif/`.
2. Run `python tools/generate_anim_assets.py`.
3. Mirror the generated output with `python tools/sync_anim_sdcard.py --target-root <drive>:\`.
4. Reinsert the SD card into the device.
5. Flash firmware if runtime code changed.
6. Boot the device and confirm:
   - boot animation starts
   - the main UI text overlay is visible
   - switching states no longer flashes white or shows `No data`

## Output Rules

- The output directory is recreated when `--clean` is enabled.
- Each GIF is expanded into a full-frame RGB565 `animpack`.
- The manifest stores pack path, dimensions, frame count, and timing metadata.
- Animation types without a source GIF are skipped instead of generating empty
  placeholder outputs.
- Numeric ID、canonical name、alias、源文件候选、默认 loop 与 encoding 只在
  `components/services/anim_service/animation_registry.json` 维护。
- `--extra-anim-type` 只能引用 Registry 中的 canonical name；未注册类型会
  直接失败。

## Runtime Expectations

- The firmware reads `anim_manifest.bin` from `/sdcard/anim/anim_manifest.bin`.
- About Device reads the SD resource bundle version from
  `/sdcard/resource_manifest.json`. If the file is missing or invalid, the
  device reports `Legacy / Unversioned`.
- `boot.animpack` must exist for the boot intro path.
- Manifest v2 提供每个资源的 FPS、frame count 和 loop；逐帧 delay 位于
  AnimPack。缺省值来自 Registry，设备端不再加载额外的 JSON 动画配置。
- The current branch requires FATFS long file name support because
  `anim_manifest.bin` and `*.animpack` exceed 8.3 naming.
- The current `V2.4.1` release bundle packages the current firmware baseline
  together with the shipped GIF-derived asset set.

## Troubleshooting

- `Anim manifest missing`
  - Confirm the SD card contains `anim/anim_manifest.bin` at the card root.
  - Confirm the generated files were copied from `release/V2.4.1/sdcard/anim/`.
- `No SD animation manifest available under /sdcard/anim`
  - Usually means the generated files were copied to the wrong directory or the
    wrong SD card was inserted.
- `sdmmc_init_spi_crc ... returned 0x106`
  - On the current board bring-up branch this warning is tolerated; the custom
    SDSPI mount path continues without `CMD59 CRC_ON_OFF`.
- `No data` during animation switching
  - This was a descriptor lifetime bug in the switch commit path and is fixed on
    the current branch baseline.

## Notes

- This toolchain is for offline asset generation only.
- The firmware runtime consumes SD-backed `animpack` files and does not decode
  GIF files on-device.
- The SD resource bundle version is package-level metadata. It is not the
  firmware version and does not version individual animation files.
- For a branch-level overview and roadmap, see:
  - `GIF_ANIMATION_BRANCH_GUIDE.md`
  - `GIF_ANIMATION_ROADMAP.md`
