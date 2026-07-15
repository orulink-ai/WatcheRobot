#!/usr/bin/env python3
"""Reconcile resource-only animation metadata with the firmware Registry.

This tool intentionally does not own application state transitions, end behavior,
priority, or preemption. Those policies remain in the embedded application layer.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
FIRMWARE_ROOT = SCRIPT_DIR.parent
DEFAULT_REGISTRY = (
    FIRMWARE_ROOT / "components" / "services" / "anim_service" / "animation_registry.json"
)
CODEGEN = SCRIPT_DIR / "generate_animation_registry.py"
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
ALLOWED_SNAPSHOT_FIELDS = {"name"}


@dataclass(frozen=True)
class AnimationResourceMetadata:
    name: str


@dataclass(frozen=True)
class SyncPlan:
    new_types: tuple[str, ...]


def parse_metadata_entry(value: str) -> AnimationResourceMetadata:
    return validate_metadata(value)


def validate_metadata(name: Any) -> AnimationResourceMetadata:
    if not isinstance(name, str) or not NAME_RE.fullmatch(name) or len(name.encode("utf-8")) >= 24:
        raise ValueError(f"invalid canonical animation name: {name!r}")
    return AnimationResourceMetadata(name=name)


def load_metadata_snapshot(path: Path) -> list[AnimationResourceMetadata]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema_version") != 1:
        raise ValueError("animation metadata schema_version must be 1")
    animations = payload.get("animations")
    if not isinstance(animations, list):
        raise ValueError("animation metadata animations must be a list")

    result: list[AnimationResourceMetadata] = []
    known_names: set[str] = set()
    for entry in animations:
        if not isinstance(entry, dict):
            raise ValueError("animation metadata entries must be objects")
        unsupported = sorted(set(entry) - ALLOWED_SNAPSHOT_FIELDS)
        if unsupported:
            raise ValueError(
                "resource metadata must not contain app behavior fields; unsupported fields: "
                + ", ".join(unsupported)
            )
        metadata = validate_metadata(entry.get("name"))
        if metadata.name in known_names:
            raise ValueError(f"duplicate animation metadata: {metadata.name}")
        known_names.add(metadata.name)
        result.append(metadata)
    return result


def build_sync_plan(registry: dict[str, Any], metadata: list[AnimationResourceMetadata]) -> SyncPlan:
    registered = {entry["name"]: entry for entry in registry["animations"]}
    new_types: list[str] = []
    for item in metadata:
        entry = registered.get(item.name)
        if entry is None:
            new_types.append(item.name)
    return SyncPlan(tuple(sorted(new_types)))


def new_registry_entry(item: AnimationResourceMetadata, animation_id: int) -> dict[str, Any]:
    hyphen_name = item.name.replace("_", "-")
    aliases = [hyphen_name] if hyphen_name != item.name else []
    source_candidates = [f"{item.name}.gif"]
    if hyphen_name != item.name:
        source_candidates.append(f"{hyphen_name}.gif")
    source_candidates.append(f"watcher-{hyphen_name}.gif")
    underscored_watcher = f"watcher-{item.name}.gif"
    if underscored_watcher not in source_candidates:
        source_candidates.append(underscored_watcher)
    return {
        "id": animation_id,
        "name": item.name,
        "enum": item.name.upper(),
        "aliases": aliases,
        "source_candidates": source_candidates,
        "legacy_directories": [f"watcher-{hyphen_name}"],
        "default_loop": False,
        "encoding": "auto",
        "required": False,
    }


def append_new_types(registry: dict[str, Any], metadata: list[AnimationResourceMetadata]) -> None:
    animations = registry["animations"]
    registered = {entry["name"] for entry in animations}
    metadata_by_name = {item.name: item for item in metadata}
    for name in sorted(set(metadata_by_name) - registered):
        animations.append(new_registry_entry(metadata_by_name[name], len(animations)))


def render_registry(registry: dict[str, Any]) -> str:
    rendered = json.dumps(registry, ensure_ascii=False, indent=2)
    scalar_array_pattern = re.compile(r"\[\n(?:(?![\[\]{}]).)*?\]", re.S)

    def compact_scalar_array(match: re.Match[str]) -> str:
        values = json.loads(match.group(0))
        if isinstance(values, list) and all(not isinstance(value, (list, dict)) for value in values):
            return json.dumps(values, ensure_ascii=False)
        return match.group(0)

    return scalar_array_pattern.sub(compact_scalar_array, rendered) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument(
        "--animation",
        action="append",
        default=[],
        metavar="NAME",
        help="Inline canonical resource name; can be repeated",
    )
    parser.add_argument("--apply-new-types", action="store_true", help="Append new IDs without reordering existing IDs")
    parser.add_argument(
        "--format-registry",
        action="store_true",
        help="Rewrite Registry with the repository's compact scalar-array formatting",
    )
    args = parser.parse_args()

    try:
        metadata = load_metadata_snapshot(args.metadata.resolve()) if args.metadata else []
        metadata.extend(parse_metadata_entry(value) for value in args.animation)
        if not metadata and not args.format_registry:
            raise ValueError("provide --metadata or at least one --animation")
        if len({item.name for item in metadata}) != len(metadata):
            raise ValueError("animation metadata names must be unique")
        registry_path = args.registry.resolve()
        registry = json.loads(registry_path.read_text(encoding="utf-8"))
        plan = build_sync_plan(registry, metadata)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Animation metadata sync failed: {exc}", file=sys.stderr)
        return 2

    print("=== Animation resource metadata reconciliation ===")
    print(f"registry: {registry_path}")
    print(f"new_types: {', '.join(plan.new_types) if plan.new_types else 'none'}")
    changed = args.format_registry
    if args.apply_new_types and plan.new_types:
        append_new_types(registry, metadata)
        changed = True
    if not changed:
        print("mode: dry-run")
        return 0

    registry_path.write_text(render_registry(registry), encoding="utf-8")
    completed = subprocess.run([sys.executable, str(CODEGEN)], cwd=FIRMWARE_ROOT, check=False)
    if completed.returncode != 0:
        return completed.returncode
    print("mode: applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
