#!/usr/bin/env python3
"""
Generate SD-card animation assets from a folder of GIF sources or legacy PNG sequences.

Outputs:
  - anim_manifest.bin (v2)
  - one <type>.animpack per animation

The firmware runtime treats GIF as an offline authoring format only. At build
time we expand each frame into a self-contained RGB565 payload that can be
streamed directly into a ring buffer on-device.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import struct
import sys
from datetime import date, datetime
from pathlib import Path

try:
    from PIL import Image
    from PIL import ImageSequence
except ImportError as exc:  # pragma: no cover - runtime dependency check
    raise SystemExit("Pillow is required. Install it with: python -m pip install Pillow") from exc


PROJECT_VERSION = "V2.4.1"
DEFAULT_PRODUCT = "WatcheRobot-S3"
DEFAULT_BUNDLE_ID = "default-sd-resources"
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_INPUT_DIR = PROJECT_ROOT / "assets" / "gif"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "release" / PROJECT_VERSION / "sdcard" / "anim"
DEFAULT_BEHAVIOR_CATALOG = PROJECT_ROOT / "spiffs" / "behavior" / "states.json"

REGISTRY_MODULE_PATH = SCRIPT_DIR / "animation_registry_generated.py"


def load_generated_registry():
    spec = importlib.util.spec_from_file_location("animation_registry_generated", REGISTRY_MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load generated animation registry: {REGISTRY_MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_REGISTRY = load_generated_registry()
DEFAULT_FPS = _REGISTRY.DEFAULT_FPS
ANIMATIONS = _REGISTRY.ANIMATIONS
ANIMATION_BY_NAME = _REGISTRY.ANIMATION_BY_NAME
ANIM_TYPES = [entry["name"] for entry in ANIMATIONS]
NON_LOOP_ANIM_TYPES = {entry["name"] for entry in ANIMATIONS if not entry["default_loop"]}

NAME_LEN = 24
PATH_LEN = 96
MANIFEST_MAGIC = b"ANIM"
MANIFEST_VERSION = 2
PACK_MAGIC = b"ANPK"
PACK_VERSION = 2
FRAME_DESC_FMT = "<IIHH"
FRAME_DESC_SIZE = struct.calcsize(FRAME_DESC_FMT)
MANIFEST_ENTRY_FMT = f"<HHHHHB3x{NAME_LEN}s{PATH_LEN}s"
PACK_HEADER_FMT = "<4sHHHHBBHIII"
FRAME_FLAG_INDEXED8 = 0x0001
RAW_FRAME_ANIM_TYPES = {entry["name"] for entry in ANIMATIONS if entry["encoding"] == "rgb565"}
LEGACY_IMPORT_MAP = {
    legacy_dir: entry["name"] for entry in ANIMATIONS for legacy_dir in entry["legacy_directories"]
}
GIF_CANDIDATES = {entry["name"]: list(entry["source_candidates"]) for entry in ANIMATIONS}


def encode_c_string(value: str, size: int) -> bytes:
    data = value.encode("utf-8")
    if len(data) >= size:
        raise ValueError(f"Value too long for fixed field ({size} bytes): {value}")
    return data + b"\0" * (size - len(data))


def default_resource_version(today: date | None = None) -> str:
    if today is None:
        today = date.today()
    return f"res-{today:%Y.%m.%d}.1"


def hash_anim_bundle(output_dir: Path) -> str:
    digest = hashlib.sha256()
    files = [output_dir / "anim_manifest.bin"]
    files.extend(sorted(output_dir.glob("*.animpack"), key=lambda path: path.name.lower()))

    for path in files:
        if not path.is_file():
            continue
        rel = path.relative_to(output_dir.parent).as_posix().encode("utf-8")
        digest.update(rel)
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stage_behavior_catalog(sdcard_dir: Path, source_path: Path = DEFAULT_BEHAVIOR_CATALOG) -> Path:
    if not source_path.is_file():
        raise ValueError(f"Behavior catalog does not exist: {source_path}")
    target_path = sdcard_dir / "behavior" / "states.json"
    target_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, target_path)
    return target_path


def write_resource_manifest(
    output_dir: Path,
    bundle_id: str,
    bundle_version: str,
    generated_at: str | None = None,
) -> Path:
    sdcard_dir = output_dir.parent
    manifest_path = sdcard_dir / "resource_manifest.json"
    behavior_path = sdcard_dir / "behavior" / "states.json"
    anim_count = len([path for path in output_dir.glob("*.animpack") if path.is_file()])
    if generated_at is None:
        generated_at = datetime.now().astimezone().isoformat(timespec="seconds")

    manifest = {
        "schema_version": 1,
        "product": DEFAULT_PRODUCT,
        "bundle_id": bundle_id,
        "bundle_version": bundle_version,
        "generated_at": generated_at,
        "compatibility": {
            "animation_registry": {
                "count": _REGISTRY.REGISTRY_COUNT,
                "fingerprint": _REGISTRY.REGISTRY_FINGERPRINT,
                "id_policy": "contiguous-append-only",
            }
        },
        "contents": {
            "anim": {
                "path": "anim/anim_manifest.bin",
                "format": f"animpack-v{PACK_VERSION}",
                "count": anim_count,
                "bundle_sha256": hash_anim_bundle(output_dir),
            }
        },
    }
    if behavior_path.is_file():
        manifest["contents"]["behavior"] = {
            "path": "behavior/states.json",
            "format": "behavior-states-v1",
            "sha256": file_sha256(behavior_path),
        }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def extract_frame_index(stem: str) -> int:
    matches = re.findall(r"(\d+)", stem)
    if not matches:
        return 0
    return int(matches[-1])


def rgba_to_rgb565(image: Image.Image, swap_bytes: bool) -> bytes:
    rgba = image.convert("RGBA")
    payload = bytearray()
    rgba_bytes = rgba.tobytes()
    for offset in range(0, len(rgba_bytes), 4):
        r = rgba_bytes[offset]
        g = rgba_bytes[offset + 1]
        b = rgba_bytes[offset + 2]
        a = rgba_bytes[offset + 3]
        if a != 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        payload += struct.pack(">H" if swap_bytes else "<H", value)
    return bytes(payload)


def encode_indexed8_exact(rgb565_payload: bytes) -> bytes | None:
    palette: list[bytes] = []
    palette_index: dict[bytes, int] = {}
    indices = bytearray()

    for offset in range(0, len(rgb565_payload), 2):
        color = rgb565_payload[offset : offset + 2]
        index = palette_index.get(color)
        if index is None:
            if len(palette) >= 256:
                return None
            index = len(palette)
            palette_index[color] = index
            palette.append(color)
        indices.append(index)

    return struct.pack("<H", len(palette)) + b"".join(palette) + bytes(indices)


def load_gif_frames(path: Path, default_delay_ms: int) -> list[tuple[Image.Image, int]]:
    frames: list[tuple[Image.Image, int]] = []
    with Image.open(path) as image:
        fallback_delay = image.info.get("duration", default_delay_ms) or default_delay_ms
        for frame in ImageSequence.Iterator(image):
            delay_ms = frame.info.get("duration", fallback_delay) or default_delay_ms
            if delay_ms <= 0:
                delay_ms = default_delay_ms
            frames.append((frame.convert("RGBA").copy(), int(delay_ms)))
    return frames


def load_legacy_png_frames(path: Path, default_delay_ms: int) -> list[tuple[Image.Image, int]]:
    source_frames = sorted(path.glob("*.png"), key=lambda candidate: extract_frame_index(candidate.stem))
    frames: list[tuple[Image.Image, int]] = []
    for frame in source_frames:
        with Image.open(frame) as image:
            frames.append((image.convert("RGBA").copy(), default_delay_ms))
    return frames


def resolve_gif_source(import_dir: Path, anim_type: str) -> Path | None:
    candidate_names = GIF_CANDIDATES.get(anim_type, [])
    alias_stems = {anim_type.lower()}
    alias_stems.update(Path(candidate_name).stem.lower() for candidate_name in candidate_names)

    for candidate_name in candidate_names:
        candidate = import_dir / candidate_name
        if candidate.is_file():
            return candidate

    matches = [candidate for candidate in import_dir.rglob("*.gif") if candidate.stem.lower() in alias_stems]
    if not matches:
        return None

    matches.sort(key=lambda path: str(path.relative_to(import_dir)).lower())
    return matches[0]


def load_source_frames(import_dir: Path, anim_type: str, default_delay_ms: int) -> list[tuple[Image.Image, int]]:
    gif_source = resolve_gif_source(import_dir, anim_type)
    if gif_source is not None:
        return load_gif_frames(gif_source, default_delay_ms)

    legacy_dirs = [name for name, mapped_type in LEGACY_IMPORT_MAP.items() if mapped_type == anim_type]
    for legacy_dir in legacy_dirs:
        candidate = import_dir / legacy_dir
        if candidate.is_dir():
            frames = load_legacy_png_frames(candidate, default_delay_ms)
            if frames:
                return frames

    return []


def animation_source_exists(import_dir: Path, anim_type: str) -> bool:
    if resolve_gif_source(import_dir, anim_type) is not None:
        return True

    legacy_dirs = [name for name, mapped_type in LEGACY_IMPORT_MAP.items() if mapped_type == anim_type]
    return any(any((import_dir / legacy_dir).glob("*.png")) for legacy_dir in legacy_dirs)


def write_animpack(
    output_dir: Path,
    anim_type: str,
    frames: list[tuple[Image.Image, int]],
    default_fps: int,
    swap_bytes: bool,
    loop: bool,
) -> dict[str, int | str]:
    width, height = frames[0][0].size
    raw_payloads = [rgba_to_rgb565(frame, swap_bytes) for frame, _ in frames]
    frame_data_size = len(raw_payloads[0])
    if any(len(payload) != frame_data_size for payload in raw_payloads):
        raise ValueError(f"Frame size mismatch in {anim_type}")

    encoded_frames: list[tuple[bytes, int]] = []
    use_raw_frames = anim_type in RAW_FRAME_ANIM_TYPES
    for payload in raw_payloads:
        indexed_payload = None if use_raw_frames else encode_indexed8_exact(payload)
        if indexed_payload is not None:
            encoded_frames.append((indexed_payload, FRAME_FLAG_INDEXED8))
        else:
            encoded_frames.append((payload, 0))

    pack_name = f"{anim_type}.animpack"
    pack_path = output_dir / pack_name
    toc_offset = struct.calcsize(PACK_HEADER_FMT)
    payload_offset = toc_offset + len(frames) * FRAME_DESC_SIZE
    default_delay_ms = max(1, int(round(1000 / max(default_fps, 1))))

    descriptors = []
    payload_offset_cursor = 0
    for (payload, flags), (_, delay_ms) in zip(encoded_frames, frames):
        descriptors.append(struct.pack(FRAME_DESC_FMT, payload_offset_cursor, len(payload), delay_ms, flags))
        payload_offset_cursor += len(payload)

    header = struct.pack(
        PACK_HEADER_FMT,
        PACK_MAGIC,
        PACK_VERSION,
        width,
        height,
        len(frames),
        1 if loop else 0,
        0,
        default_delay_ms,
        toc_offset,
        payload_offset,
        frame_data_size,
    )

    with pack_path.open("wb") as handle:
        handle.write(header)
        for descriptor in descriptors:
            handle.write(descriptor)
        for payload, _ in encoded_frames:
            handle.write(payload)

    return {
        "pack_name": pack_name,
        "width": width,
        "height": height,
        "frame_count": len(frames),
        "fps": default_fps,
        "indexed8_frames": sum(1 for _, flags in encoded_frames if flags & FRAME_FLAG_INDEXED8),
        "raw_bytes": sum(len(payload) for payload in raw_payloads),
        "pack_payload_bytes": payload_offset_cursor,
    }


def build_manifest(
    import_dir: Path,
    output_dir: Path,
    default_fps: int,
    swap_bytes: bool,
    clean: bool,
    extra_anim_types: list[str] | None = None,
) -> dict[str, int]:
    unknown_types = sorted(set(extra_anim_types or []) - set(ANIM_TYPES))
    if unknown_types:
        raise ValueError(
            "Animation type(s) not registered in animation_registry.json: " + ", ".join(unknown_types)
        )

    missing_required = [
        entry["name"]
        for entry in ANIMATIONS
        if entry["required"] and not animation_source_exists(import_dir, entry["name"])
    ]
    if missing_required:
        raise ValueError(
            "Required animation source(s) missing: " + ", ".join(missing_required)
        )

    if clean and output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    default_delay_ms = max(1, int(round(1000 / max(default_fps, 1))))

    anim_types = list(ANIM_TYPES)

    manifest_entries = []
    manifest_counts: dict[str, int] = {}
    for anim_type in anim_types:
        frames = load_source_frames(import_dir, anim_type, default_delay_ms)
        if not frames:
            if ANIMATION_BY_NAME[anim_type]["required"]:
                raise ValueError(f"Required animation source contains no frames: {anim_type}")
            continue

        registry_entry = ANIMATION_BY_NAME[anim_type]
        type_id = registry_entry["id"]
        should_loop = registry_entry["default_loop"]
        pack_info = write_animpack(output_dir, anim_type, frames, default_fps, swap_bytes, should_loop)
        entry = struct.pack(
            MANIFEST_ENTRY_FMT,
            type_id,
            pack_info["width"],
            pack_info["height"],
            pack_info["fps"],
            pack_info["frame_count"],
            1 if should_loop else 0,
            encode_c_string(anim_type, NAME_LEN),
            encode_c_string(str(pack_info["pack_name"]), PATH_LEN),
        )
        manifest_entries.append(entry)
        manifest_counts[anim_type] = int(pack_info["frame_count"])

    manifest_path = output_dir / "anim_manifest.bin"
    manifest_path.write_bytes(
        struct.pack("<4sHH", MANIFEST_MAGIC, MANIFEST_VERSION, len(manifest_entries)) + b"".join(manifest_entries)
    )

    print(f"Wrote {manifest_path}")
    print(f"Generated {len(manifest_entries)} manifest entries in {output_dir}")
    for anim_type in anim_types:
        if anim_type in manifest_counts:
            print(f"  {anim_type}: {manifest_counts[anim_type]} frame(s)")
    return manifest_counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        "--import-dir",
        dest="input_dir",
        default=str(DEFAULT_INPUT_DIR),
        help="Directory containing GIF sources or legacy PNG animation folders",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Where generated animpack assets should be written",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=DEFAULT_FPS,
        help=f"Default FPS stored in manifest entries (Registry default: {DEFAULT_FPS})",
    )
    parser.add_argument(
        "--clean",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Remove the output directory contents before generating new assets",
    )
    parser.add_argument(
        "--lv-color-16-swap",
        action="store_true",
        help="Write RGB565 payloads in LVGL-native swapped 16-bit byte order",
    )
    parser.add_argument(
        "--resource-version",
        default=None,
        help="SD resource bundle version written to resource_manifest.json",
    )
    parser.add_argument(
        "--bundle-id",
        default=DEFAULT_BUNDLE_ID,
        help="Stable SD resource bundle id written to resource_manifest.json",
    )
    parser.add_argument(
        "--extra-anim-type",
        action="append",
        default=[],
        help="Additional animation type to include in anim_manifest.bin; repeat for multiple Feishu-defined types",
    )
    args = parser.parse_args()

    import_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    resource_version = args.resource_version or default_resource_version()

    if not import_dir.is_dir():
        raise SystemExit(f"Import directory does not exist: {import_dir}")

    build_manifest(import_dir, output_dir, args.fps, args.lv_color_16_swap, args.clean, args.extra_anim_type)
    stage_behavior_catalog(output_dir.parent)
    manifest_path = write_resource_manifest(output_dir, args.bundle_id, resource_version)
    if args.resource_version:
        print(f"Resource bundle version: {resource_version}")
    else:
        print(f"Resource bundle version: {resource_version} (auto)")
    print(f"Wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
