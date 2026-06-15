#!/usr/bin/env python3
"""
Copy generated AnimPack assets to an SD-card style target directory and verify
the copy matches the source exactly.

Default behavior:
  - source: build/generated/sdcard/anim
  - target: <target-root>/anim

The script removes the target anim directory before copying so the result is a
clean, deterministic mirror of the generated assets.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


PROJECT_VERSION = "V2.3.0"
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_SOURCE_DIR = PROJECT_ROOT / "build" / "generated" / "sdcard" / "anim"


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_target_anim_dir(target_root: Path) -> Path:
    if not target_root.exists():
        raise SystemExit(f"Target root does not exist: {target_root}")
    if not target_root.is_dir():
        raise SystemExit(f"Target root is not a directory: {target_root}")
    return target_root / "anim"


def resolve_source_dir(explicit_source_dir: str | None) -> Path:
    if explicit_source_dir is not None:
        source_dir = Path(explicit_source_dir).resolve()
        if not source_dir.is_dir():
            raise SystemExit(f"Source directory does not exist: {source_dir}")
        return source_dir

    if DEFAULT_SOURCE_DIR.is_dir():
        return DEFAULT_SOURCE_DIR.resolve()

    release_root = PROJECT_ROOT / "build"
    candidates = sorted(
        (path for path in release_root.glob("*/generated/sdcard/anim") if path.is_dir()),
        key=lambda path: str(path).lower(),
    )
    if not candidates:
        raise SystemExit(f"Unable to locate a generated anim source directory under {release_root}")

    return candidates[-1].resolve()


def verify_tree(source_dir: Path, target_dir: Path) -> None:
    source_files = sorted((path for path in source_dir.rglob("*") if path.is_file()), key=lambda path: str(path.relative_to(source_dir)).lower())
    target_files = sorted((path for path in target_dir.rglob("*") if path.is_file()), key=lambda path: str(path.relative_to(target_dir)).lower())

    source_names = [str(path.relative_to(source_dir)).replace("\\", "/") for path in source_files]
    target_names = [str(path.relative_to(target_dir)).replace("\\", "/") for path in target_files]
    if source_names != target_names:
        missing = sorted(set(source_names) - set(target_names))
        extra = sorted(set(target_names) - set(source_names))
        raise SystemExit(f"Verification failed: missing={missing} extra={extra}")

    for source_path, target_path in zip(source_files, target_files):
        if file_hash(source_path) != file_hash(target_path):
            raise SystemExit(f"Verification failed: hash mismatch for {source_path.relative_to(source_dir)}")


def copy_tree(source_dir: Path, target_dir: Path, clean: bool) -> None:
    if clean and target_dir.exists():
        if target_dir.name != "anim":
            raise SystemExit(f"Refusing to clean unexpected target directory: {target_dir}")
        shutil.rmtree(target_dir)

    target_dir.mkdir(parents=True, exist_ok=True)

    for item in source_dir.rglob("*"):
        relative = item.relative_to(source_dir)
        destination = target_dir / relative
        if item.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        default=None,
        help="Directory containing generated animpack assets",
    )
    parser.add_argument(
        "--target-root",
        required=True,
        help="SD-card root path. The script copies assets into <target-root>/anim",
    )
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="Keep the existing target anim directory contents before copying",
    )
    args = parser.parse_args()

    source_dir = resolve_source_dir(args.source_dir)
    target_root = Path(args.target_root).resolve()
    target_dir = resolve_target_anim_dir(target_root)

    copy_tree(source_dir, target_dir, clean=not args.no_clean)
    verify_tree(source_dir, target_dir)

    print(f"Copied {source_dir} -> {target_dir}")
    print("Verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
