#!/usr/bin/env python3
"""
Estimate animation compression options against the current GIF sources.

The report focuses on bytes the device would need to read per frame payload.
It intentionally excludes common animpack headers and frame table descriptors so
the ratios map directly to the current SD read bottleneck.
"""

from __future__ import annotations

import argparse
import csv
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))

from generate_anim_assets import ANIM_TYPES  # noqa: E402
from generate_anim_assets import DEFAULT_INPUT_DIR  # noqa: E402
from generate_anim_assets import load_source_frames  # noqa: E402
from generate_anim_assets import rgba_to_rgb565  # noqa: E402


FULL_FRAME_TAG_BYTES = 1
DELTA_RECT_HEADER_BYTES = 1 + 2 * 4
SPAN_DELTA_HEADER_BYTES = 1 + 2
SPAN_HEADER_BYTES = 2 * 3
TILE_DELTA_HEADER_BYTES = 1 + 2
TILE_REF_BYTES = 2
INDEXED_HEADER_BYTES = 2
PALETTE_ENTRY_BYTES = 2


@dataclass
class AnimEstimate:
    anim_type: str
    source: str
    width: int
    height: int
    frames: int
    raw: int
    dirty_rect: int
    span_delta: int
    tile8: int
    tile16: int
    indexed8_exact: int | None
    indexed8_lossy: int
    zlib1_full: int
    avg_dirty_pixels_pct: float
    avg_tile8_pixels_pct: float
    avg_tile16_pixels_pct: float
    max_colors: int


def fmt_bytes(value: int | None) -> str:
    if value is None:
        return "-"
    if value >= 1024 * 1024:
        return f"{value / (1024 * 1024):.2f}MB"
    return f"{value / 1024:.1f}KB"


def fmt_ratio(raw: int, value: int | None) -> str:
    if value is None or raw <= 0:
        return "-"
    return f"{value / raw:.2%}"


def changed_bbox(prev_payload: bytes, payload: bytes, width: int) -> tuple[int, int, int, int, int] | None:
    min_x = width
    min_y = 1 << 30
    max_x = -1
    max_y = -1
    changed_pixels = 0

    for offset in range(0, len(payload), 2):
        if payload[offset] == prev_payload[offset] and payload[offset + 1] == prev_payload[offset + 1]:
            continue
        pixel = offset // 2
        x = pixel % width
        y = pixel // width
        if x < min_x:
            min_x = x
        if x > max_x:
            max_x = x
        if y < min_y:
            min_y = y
        if y > max_y:
            max_y = y
        changed_pixels += 1

    if changed_pixels == 0:
        return None
    return min_x, min_y, max_x - min_x + 1, max_y - min_y + 1, changed_pixels


def estimate_span_delta(prev_payload: bytes, payload: bytes, width: int, height: int) -> tuple[int, int]:
    size = SPAN_DELTA_HEADER_BYTES
    changed_pixels = 0
    for y in range(height):
        row_start = y * width * 2
        x = 0
        while x < width:
            offset = row_start + x * 2
            if payload[offset] == prev_payload[offset] and payload[offset + 1] == prev_payload[offset + 1]:
                x += 1
                continue

            run_start = x
            while x < width:
                offset = row_start + x * 2
                if payload[offset] == prev_payload[offset] and payload[offset + 1] == prev_payload[offset + 1]:
                    break
                x += 1

            run_len = x - run_start
            changed_pixels += run_len
            size += SPAN_HEADER_BYTES + run_len * 2
    return size, changed_pixels


def estimate_tile_delta(prev_payload: bytes, payload: bytes, width: int, height: int, tile_size: int) -> tuple[int, int]:
    size = TILE_DELTA_HEADER_BYTES
    covered_pixels = 0
    for tile_y in range(0, height, tile_size):
        tile_h = min(tile_size, height - tile_y)
        for tile_x in range(0, width, tile_size):
            tile_w = min(tile_size, width - tile_x)
            changed = False
            for y in range(tile_y, tile_y + tile_h):
                start = (y * width + tile_x) * 2
                end = start + tile_w * 2
                if payload[start:end] != prev_payload[start:end]:
                    changed = True
                    break
            if changed:
                covered_pixels += tile_w * tile_h
                size += TILE_REF_BYTES + tile_w * tile_h * 2
    return size, covered_pixels


def estimate_indexed_exact(payload: bytes, pixel_count: int) -> tuple[int | None, int]:
    colors = {payload[offset : offset + 2] for offset in range(0, len(payload), 2)}
    color_count = len(colors)
    if color_count > 256:
        return None, color_count
    return INDEXED_HEADER_BYTES + color_count * PALETTE_ENTRY_BYTES + pixel_count, color_count


def analyze_anim(input_dir: Path, anim_type: str, default_delay_ms: int, swap_bytes: bool) -> AnimEstimate | None:
    frames = load_source_frames(input_dir, anim_type, default_delay_ms)
    if not frames:
        return None

    width, height = frames[0][0].size
    pixel_count = width * height
    payloads = [rgba_to_rgb565(frame, swap_bytes) for frame, _ in frames]
    raw_frame_size = pixel_count * 2
    raw = raw_frame_size * len(payloads)

    dirty_rect = raw_frame_size + FULL_FRAME_TAG_BYTES
    span_delta = raw_frame_size + FULL_FRAME_TAG_BYTES
    tile8 = raw_frame_size + FULL_FRAME_TAG_BYTES
    tile16 = raw_frame_size + FULL_FRAME_TAG_BYTES
    indexed8_exact = 0
    indexed8_exact_possible = True
    indexed8_lossy = 0
    zlib1_full = 0
    dirty_changed_pixels = 0
    tile8_covered_pixels = 0
    tile16_covered_pixels = 0
    max_colors = 0

    for index, payload in enumerate(payloads):
        zlib1_full += len(zlib.compress(payload, level=1))

        indexed_size, color_count = estimate_indexed_exact(payload, pixel_count)
        max_colors = max(max_colors, color_count)
        if indexed_size is None:
            indexed8_exact_possible = False
        else:
            indexed8_exact += indexed_size
        indexed8_lossy += INDEXED_HEADER_BYTES + 256 * PALETTE_ENTRY_BYTES + pixel_count

        if index == 0:
            continue

        prev_payload = payloads[index - 1]
        bbox = changed_bbox(prev_payload, payload, width)
        if bbox is None:
            dirty_rect += DELTA_RECT_HEADER_BYTES
        else:
            _, _, rect_w, rect_h, changed_pixels = bbox
            dirty_rect += DELTA_RECT_HEADER_BYTES + rect_w * rect_h * 2
            dirty_changed_pixels += changed_pixels

        span_size, span_pixels = estimate_span_delta(prev_payload, payload, width, height)
        span_delta += span_size
        tile8_size, tile8_pixels = estimate_tile_delta(prev_payload, payload, width, height, 8)
        tile16_size, tile16_pixels = estimate_tile_delta(prev_payload, payload, width, height, 16)
        tile8 += tile8_size
        tile16 += tile16_size
        tile8_covered_pixels += tile8_pixels
        tile16_covered_pixels += tile16_pixels

    delta_frame_count = max(1, len(payloads) - 1)
    return AnimEstimate(
        anim_type=anim_type,
        source=str(input_dir),
        width=width,
        height=height,
        frames=len(payloads),
        raw=raw,
        dirty_rect=dirty_rect,
        span_delta=span_delta,
        tile8=tile8,
        tile16=tile16,
        indexed8_exact=indexed8_exact if indexed8_exact_possible else None,
        indexed8_lossy=indexed8_lossy,
        zlib1_full=zlib1_full,
        avg_dirty_pixels_pct=(dirty_changed_pixels / (delta_frame_count * pixel_count)) * 100.0,
        avg_tile8_pixels_pct=(tile8_covered_pixels / (delta_frame_count * pixel_count)) * 100.0,
        avg_tile16_pixels_pct=(tile16_covered_pixels / (delta_frame_count * pixel_count)) * 100.0,
        max_colors=max_colors,
    )


def best_scheme(estimate: AnimEstimate) -> tuple[str, int]:
    candidates = {
        "dirty_rect": estimate.dirty_rect,
        "span_delta": estimate.span_delta,
        "tile8": estimate.tile8,
        "tile16": estimate.tile16,
        "indexed8_lossy": estimate.indexed8_lossy,
        "zlib1_full": estimate.zlib1_full,
    }
    if estimate.indexed8_exact is not None:
        candidates["indexed8_exact"] = estimate.indexed8_exact
    name, size = min(candidates.items(), key=lambda item: item[1])
    return name, size


def write_csv(path: Path, estimates: list[AnimEstimate]) -> None:
    fieldnames = [
        "anim_type",
        "width",
        "height",
        "frames",
        "raw_bytes",
        "dirty_rect_bytes",
        "span_delta_bytes",
        "tile8_bytes",
        "tile16_bytes",
        "indexed8_exact_bytes",
        "indexed8_lossy_bytes",
        "zlib1_full_bytes",
        "best_scheme",
        "best_bytes",
        "best_ratio",
        "avg_dirty_pixels_pct",
        "avg_tile8_pixels_pct",
        "avg_tile16_pixels_pct",
        "max_colors",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for estimate in estimates:
            best_name, best_size = best_scheme(estimate)
            writer.writerow(
                {
                    "anim_type": estimate.anim_type,
                    "width": estimate.width,
                    "height": estimate.height,
                    "frames": estimate.frames,
                    "raw_bytes": estimate.raw,
                    "dirty_rect_bytes": estimate.dirty_rect,
                    "span_delta_bytes": estimate.span_delta,
                    "tile8_bytes": estimate.tile8,
                    "tile16_bytes": estimate.tile16,
                    "indexed8_exact_bytes": estimate.indexed8_exact if estimate.indexed8_exact is not None else "",
                    "indexed8_lossy_bytes": estimate.indexed8_lossy,
                    "zlib1_full_bytes": estimate.zlib1_full,
                    "best_scheme": best_name,
                    "best_bytes": best_size,
                    "best_ratio": best_size / estimate.raw,
                    "avg_dirty_pixels_pct": estimate.avg_dirty_pixels_pct,
                    "avg_tile8_pixels_pct": estimate.avg_tile8_pixels_pct,
                    "avg_tile16_pixels_pct": estimate.avg_tile16_pixels_pct,
                    "max_colors": estimate.max_colors,
                }
            )


def print_report(estimates: list[AnimEstimate]) -> None:
    total_raw = sum(estimate.raw for estimate in estimates)
    totals = {
        "dirty_rect": sum(estimate.dirty_rect for estimate in estimates),
        "span_delta": sum(estimate.span_delta for estimate in estimates),
        "tile8": sum(estimate.tile8 for estimate in estimates),
        "tile16": sum(estimate.tile16 for estimate in estimates),
        "indexed8_lossy": sum(estimate.indexed8_lossy for estimate in estimates),
        "zlib1_full": sum(estimate.zlib1_full for estimate in estimates),
    }
    exact_values = [estimate.indexed8_exact for estimate in estimates]
    if all(value is not None for value in exact_values):
        totals["indexed8_exact"] = sum(value for value in exact_values if value is not None)

    print("# Animation compression estimate")
    print()
    print(f"Animations: {len(estimates)}")
    print(f"Raw total: {fmt_bytes(total_raw)}")
    print()
    print("| Scheme | Total | Ratio vs raw |")
    print("|---|---:|---:|")
    for name, value in sorted(totals.items(), key=lambda item: item[1]):
        print(f"| {name} | {fmt_bytes(value)} | {fmt_ratio(total_raw, value)} |")

    print()
    print("| Anim | Frames | Raw | Best | Best ratio | dirty | span | tile8 | tile16 | indexed exact | zlib1 | max colors |")
    print("|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for estimate in estimates:
        best_name, best_size = best_scheme(estimate)
        print(
            f"| {estimate.anim_type} | {estimate.frames} | {fmt_bytes(estimate.raw)} | "
            f"{best_name} {fmt_bytes(best_size)} | {fmt_ratio(estimate.raw, best_size)} | "
            f"{fmt_ratio(estimate.raw, estimate.dirty_rect)} | {fmt_ratio(estimate.raw, estimate.span_delta)} | "
            f"{fmt_ratio(estimate.raw, estimate.tile8)} | {fmt_ratio(estimate.raw, estimate.tile16)} | "
            f"{fmt_ratio(estimate.raw, estimate.indexed8_exact)} | {fmt_ratio(estimate.raw, estimate.zlib1_full)} | "
            f"{estimate.max_colors} |"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", default=str(DEFAULT_INPUT_DIR), help="Directory containing GIF sources")
    parser.add_argument("--fps", type=int, default=10, help="Default FPS for legacy frame sources")
    parser.add_argument("--csv", type=Path, help="Optional CSV output path")
    parser.add_argument("--lv-color-16-swap", action="store_true", help="Match generated animpack byte order")
    args = parser.parse_args()

    input_dir = Path(args.input_dir).resolve()
    if not input_dir.is_dir():
        raise SystemExit(f"Input directory does not exist: {input_dir}")

    default_delay_ms = max(1, int(round(1000 / max(args.fps, 1))))
    estimates = [
        estimate
        for anim_type in ANIM_TYPES
        if (estimate := analyze_anim(input_dir, anim_type, default_delay_ms, args.lv_color_16_swap)) is not None
    ]
    if not estimates:
        raise SystemExit(f"No animation sources found under {input_dir}")

    print_report(estimates)
    if args.csv is not None:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        write_csv(args.csv, estimates)
        print()
        print(f"CSV written: {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
