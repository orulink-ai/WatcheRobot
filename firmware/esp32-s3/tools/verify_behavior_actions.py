#!/usr/bin/env python3
"""Validate behavior action keyframe scheduling.

This mirrors the firmware action parser enough to catch regressions where the
last keyframe becomes a short trailing servo move.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path


DEFAULT_X_DEG = 90
DEFAULT_Y_DEG = 120
SINGLE_KEYFRAME_DURATION_MS = 100


@dataclass(frozen=True)
class Keyframe:
    frame: int
    angle: int


@dataclass(frozen=True)
class MotionEvent:
    at_ms: int
    x_deg: int
    y_deg: int
    duration_ms: int


def frame_to_ms(frame_number: int, start_frame: int, fps: int) -> int:
    relative_frame = frame_number - start_frame
    if fps <= 0 or relative_frame <= 0:
        return 0
    return (relative_frame * 1000 + (fps // 2)) // fps


def dedup_keyframes(frames: list[Keyframe]) -> list[Keyframe]:
    deduped: list[Keyframe] = []
    for frame in sorted(frames, key=lambda item: item.frame):
        if deduped and deduped[-1].frame == frame.frame:
            deduped[-1] = frame
        else:
            deduped.append(frame)
    return deduped


def find_angle(frames: list[Keyframe], frame_number: int, fallback: int) -> int:
    if not frames:
        return fallback
    angle = frames[0].angle
    for frame in frames:
        if frame.frame > frame_number:
            break
        angle = frame.angle
    return angle


def parse_action(data: dict) -> tuple[list[MotionEvent], int, int]:
    fps = int(data.get("fps", 0))
    frame_start = int(data.get("frame_start", 0))
    frame_end = int(data.get("frame_end", frame_start))
    if fps <= 0:
        raise ValueError("fps must be positive")

    x_frames: list[Keyframe] = []
    y_frames: list[Keyframe] = []
    max_keyframe = 0
    for obj in data.get("animated_objects", []):
        for keyframe in obj.get("keyframe_data", []):
            frame_number = int(keyframe.get("frame_number", frame_start))
            angle_deg = round(float(keyframe["rotation_angle"]))
            axis = str(keyframe.get("active_axis", "")).lower()
            max_keyframe = max(max_keyframe, frame_number)
            if axis == "z":
                x_frames.append(Keyframe(frame_number, angle_deg))
            elif axis == "x":
                y_frames.append(Keyframe(frame_number, angle_deg))

    x_frames = dedup_keyframes(x_frames)
    y_frames = dedup_keyframes(y_frames)

    last_x = DEFAULT_X_DEG
    last_y = DEFAULT_Y_DEG
    if x_frames:
        last_x = x_frames[0].angle
        effective_start = x_frames[0].frame
    elif y_frames:
        effective_start = y_frames[0].frame
    else:
        effective_start = frame_start

    if y_frames:
        last_y = y_frames[0].angle
        effective_start = min(effective_start, y_frames[0].frame)
    effective_start = min(effective_start, frame_start)

    effective_end = max(frame_end, max_keyframe, effective_start)
    effective_end_boundary = effective_end + 1
    frame_numbers = sorted({frame.frame for frame in x_frames} | {frame.frame for frame in y_frames})

    events: list[MotionEvent] = []
    for index, current_frame in enumerate(frame_numbers):
        x_deg = find_angle(x_frames, current_frame, last_x)
        y_deg = find_angle(y_frames, current_frame, last_y)
        last_x = x_deg
        last_y = y_deg

        if len(frame_numbers) == 1:
            events.append(MotionEvent(0, x_deg, y_deg, SINGLE_KEYFRAME_DURATION_MS))
            break

        if index + 1 >= len(frame_numbers):
            continue

        next_frame = frame_numbers[index + 1]
        next_x_deg = find_angle(x_frames, next_frame, last_x)
        next_y_deg = find_angle(y_frames, next_frame, last_y)
        at_ms = frame_to_ms(current_frame, effective_start, fps)
        next_ms = frame_to_ms(next_frame, effective_start, fps)
        duration_ms = next_ms - at_ms
        if duration_ms <= 0:
            continue
        if events and events[-1].x_deg == next_x_deg and events[-1].y_deg == next_y_deg:
            continue
        events.append(MotionEvent(at_ms, next_x_deg, next_y_deg, duration_ms))

    total_duration_ms = frame_to_ms(effective_end_boundary, effective_start, fps)
    last_keyframe_ms = frame_to_ms(frame_numbers[-1], effective_start, fps) if frame_numbers else 0
    return events, total_duration_ms, last_keyframe_ms


def load_action(path: Path) -> tuple[list[MotionEvent], int, int]:
    with path.open("r", encoding="utf-8") as handle:
        return parse_action(json.load(handle))


def validate_action(path: Path) -> None:
    events, total_duration_ms, last_keyframe_ms = load_action(path)
    if not events:
        return
    for event in events:
        if event.duration_ms <= 0:
            raise AssertionError(f"{path.name}: invalid duration {event.duration_ms}ms at {event.at_ms}ms")
    if len(events) > 1 and events[-1].at_ms >= last_keyframe_ms:
        raise AssertionError(f"{path.name}: generated trailing event at final keyframe {last_keyframe_ms}ms")
    print(
        f"{path.name}: events={len(events)} total_ms={total_duration_ms} "
        f"last_event={events[-1].at_ms}+{events[-1].duration_ms}ms target=({events[-1].x_deg},{events[-1].y_deg})"
    )


def validate_synthetic_single_keyframe() -> None:
    events, _, _ = parse_action(
        {
            "frame_start": 0,
            "frame_end": 10,
            "fps": 10,
            "animated_objects": [
                {
                    "keyframe_data": [
                        {"frame_number": 5, "active_axis": "x", "rotation_angle": 95.0},
                    ],
                }
            ],
        }
    )
    if events != [MotionEvent(0, DEFAULT_X_DEG, 95, SINGLE_KEYFRAME_DURATION_MS)]:
        raise AssertionError(f"single-keyframe action produced unexpected events: {events}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--actions-dir", type=Path, default=Path("spiffs/actions"))
    args = parser.parse_args()

    validate_synthetic_single_keyframe()
    for path in sorted(args.actions_dir.glob("*.json")):
        validate_action(path)


if __name__ == "__main__":
    main()
