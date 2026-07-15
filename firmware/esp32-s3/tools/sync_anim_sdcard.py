#!/usr/bin/env python3
"""
Copy generated AnimPack assets and the optional behavior catalog to an SD-card
style target directory and verify the copy matches the source exactly.

Default behavior:
  - source: release/<PROJECT_VER>/sdcard/anim
  - target: <target-root>/anim

The script removes the target anim directory before copying so the result is a
clean, deterministic mirror of the generated assets.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import stat
import time
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_PRODUCT = "WatcheRobot-S3"
DEFAULT_BUNDLE_ID = "default-sd-resources"
REGISTRY_MODULE_PATH = SCRIPT_DIR / "animation_registry_generated.py"


def load_generated_registry():
    spec = importlib.util.spec_from_file_location("animation_registry_generated", REGISTRY_MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load generated animation registry: {REGISTRY_MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_REGISTRY = load_generated_registry()


@dataclass(frozen=True)
class SourceBundle:
    root_dir: Path
    anim_dir: Path
    behavior_dir: Path | None
    manifest_bytes: bytes
    legacy_unversioned: bool


def read_project_version() -> str:
    cmake_lists = PROJECT_ROOT / "CMakeLists.txt"
    if not cmake_lists.is_file():
        raise SystemExit(f"Unable to locate CMakeLists.txt for PROJECT_VER: {cmake_lists}")

    match = re.search(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', cmake_lists.read_text(encoding="utf-8"))
    if match is None:
        raise SystemExit(f"Unable to parse PROJECT_VER from {cmake_lists}")
    return match.group(1)


def default_source_dir() -> Path:
    return PROJECT_ROOT / "release" / read_project_version() / "sdcard" / "anim"


def default_resource_version(today: date | None = None) -> str:
    if today is None:
        today = date.today()
    return f"res-{today:%Y.%m.%d}.1"


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hash_anim_bundle(anim_dir: Path) -> str:
    digest = hashlib.sha256()
    files = [anim_dir / "anim_manifest.bin"]
    files.extend(sorted(anim_dir.glob("*.animpack"), key=lambda path: path.name.lower()))

    for path in files:
        if not path.is_file():
            continue
        rel = path.relative_to(anim_dir.parent).as_posix().encode("utf-8")
        digest.update(rel)
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def synthesize_resource_manifest(anim_dir: Path, resource_version: str, bundle_id: str) -> bytes:
    manifest = {
        "schema_version": 1,
        "product": DEFAULT_PRODUCT,
        "bundle_id": bundle_id,
        "bundle_version": resource_version,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
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
                "format": "animpack-v2",
                "count": len([path for path in anim_dir.glob("*.animpack") if path.is_file()]),
                "bundle_sha256": hash_anim_bundle(anim_dir),
            }
        },
    }
    return (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def resolve_target_anim_dir(target_root: Path) -> Path:
    if not target_root.exists():
        raise SystemExit(f"Target root does not exist: {target_root}")
    if not target_root.is_dir():
        raise SystemExit(f"Target root is not a directory: {target_root}")
    return target_root / "anim"


def resolve_source_anim_dir(explicit_source_dir: str | None) -> Path:
    if explicit_source_dir is not None:
        source_dir = Path(explicit_source_dir).resolve()
        if not source_dir.is_dir():
            raise SystemExit(f"Source directory does not exist: {source_dir}")
        if (source_dir / "anim").is_dir():
            return (source_dir / "anim").resolve()
        return source_dir

    current_source_dir = default_source_dir()
    if current_source_dir.is_dir():
        return current_source_dir.resolve()

    release_root = PROJECT_ROOT / "release"
    candidates = sorted(
        (path for path in release_root.glob("*/sdcard/anim") if path.is_dir()),
        key=lambda path: str(path).lower(),
    )
    if not candidates:
        raise SystemExit(f"Unable to locate a generated anim source directory under {release_root}")

    return candidates[-1].resolve()


def resolve_source_bundle(explicit_source_dir: str | None, resource_version: str | None, bundle_id: str) -> SourceBundle:
    anim_dir = resolve_source_anim_dir(explicit_source_dir)
    if not (anim_dir / "anim_manifest.bin").is_file():
        raise SystemExit(f"Source anim directory is missing anim_manifest.bin: {anim_dir}")

    root_dir = anim_dir.parent
    manifest_path = root_dir / "resource_manifest.json"
    if manifest_path.is_file():
        behavior_dir = root_dir / "behavior"
        return SourceBundle(root_dir=root_dir, anim_dir=anim_dir,
                            behavior_dir=behavior_dir if (behavior_dir / "states.json").is_file() else None,
                            manifest_bytes=manifest_path.read_bytes(),
                            legacy_unversioned=False)

    version = resource_version or default_resource_version()
    return SourceBundle(root_dir=root_dir, anim_dir=anim_dir, behavior_dir=None,
                        manifest_bytes=synthesize_resource_manifest(anim_dir, version, bundle_id),
                        legacy_unversioned=True)


def verify_tree(source_dir: Path, target_dir: Path) -> None:
    source_files = sorted(
        (path for path in source_dir.rglob("*") if path.is_file()),
        key=lambda path: str(path.relative_to(source_dir)).lower(),
    )
    target_files = sorted(
        (path for path in target_dir.rglob("*") if path.is_file()),
        key=lambda path: str(path.relative_to(target_dir)).lower(),
    )

    source_names = [str(path.relative_to(source_dir)).replace("\\", "/") for path in source_files]
    target_names = [str(path.relative_to(target_dir)).replace("\\", "/") for path in target_files]
    if source_names != target_names:
        missing = sorted(set(source_names) - set(target_names))
        extra = sorted(set(target_names) - set(source_names))
        raise SystemExit(f"Verification failed: missing={missing} extra={extra}")

    for source_path, target_path in zip(source_files, target_files):
        if file_hash(source_path) != file_hash(target_path):
            raise SystemExit(f"Verification failed: hash mismatch for {source_path.relative_to(source_dir)}")


def verify_bundle(source: SourceBundle, target_root: Path) -> None:
    target_manifest = target_root / "resource_manifest.json"
    target_anim_dir = target_root / "anim"

    if not target_manifest.is_file():
        raise SystemExit("Verification failed: missing resource_manifest.json")
    if target_manifest.read_bytes() != source.manifest_bytes:
        raise SystemExit("Verification failed: hash mismatch for resource_manifest.json")
    verify_tree(source.anim_dir, target_anim_dir)
    if source.behavior_dir is not None:
        verify_tree(source.behavior_dir, target_root / "behavior")


def make_writable_and_retry(function, path, exc_info) -> None:
    try:
        os.chmod(path, stat.S_IWRITE)
        function(path)
    except OSError:
        raise exc_info[1]


def remove_path_with_retry(path: Path) -> None:
    last_error: OSError | None = None
    for _ in range(5):
        try:
            if path.is_dir():
                shutil.rmtree(path, onerror=make_writable_and_retry)
            else:
                try:
                    os.chmod(path, stat.S_IWRITE)
                except OSError:
                    pass
                path.unlink()
            return
        except OSError as error:
            last_error = error
            time.sleep(0.25)

    raise SystemExit(f"Failed to remove target path after retries: {path}: {last_error}")


def clean_target_contents(target_dir: Path) -> None:
    for item in target_dir.iterdir():
        remove_path_with_retry(item)


def copy_tree(source_dir: Path, target_dir: Path, clean: bool) -> None:
    if clean and target_dir.exists():
        if target_dir.name not in {"anim", "behavior"}:
            raise SystemExit(f"Refusing to clean unexpected target directory: {target_dir}")
        clean_target_contents(target_dir)

    target_dir.mkdir(parents=True, exist_ok=True)

    for item in source_dir.rglob("*"):
        relative = item.relative_to(source_dir)
        destination = target_dir / relative
        if item.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, destination)


def copy_bundle(source: SourceBundle, target_root: Path, clean: bool) -> None:
    target_anim_dir = resolve_target_anim_dir(target_root)

    copy_tree(source.anim_dir, target_anim_dir, clean=clean)
    if source.behavior_dir is not None:
        copy_tree(source.behavior_dir, target_root / "behavior", clean=clean)
    (target_root / "resource_manifest.json").write_bytes(source.manifest_bytes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        default=None,
        help="Directory containing generated sdcard bundle or animpack assets",
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
    parser.add_argument(
        "--resource-version",
        default=None,
        help="Resource bundle version to use when syncing a legacy unversioned anim source",
    )
    parser.add_argument(
        "--bundle-id",
        default=DEFAULT_BUNDLE_ID,
        help="Stable resource bundle id to use when syncing a legacy unversioned anim source",
    )
    args = parser.parse_args()

    source = resolve_source_bundle(args.source_dir, args.resource_version, args.bundle_id)
    target_root = Path(args.target_root).resolve()
    target_manifest = target_root / "resource_manifest.json"
    target_anim_dir = resolve_target_anim_dir(target_root)
    target_was_legacy = target_anim_dir.is_dir() and not target_manifest.is_file()

    copy_bundle(source, target_root, clean=not args.no_clean)
    verify_bundle(source, target_root)

    if target_was_legacy:
        print("Target previously had Legacy / Unversioned SD resources")
    if source.legacy_unversioned:
        print("Source was Legacy / Unversioned; wrote generated resource_manifest.json")
    print(f"Copied {source.anim_dir} -> {target_anim_dir}")
    if source.behavior_dir is not None:
        print(f"Copied {source.behavior_dir} -> {target_root / 'behavior'}")
    print(f"Wrote {target_manifest}")
    print("Verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
