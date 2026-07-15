#!/usr/bin/env python3
"""Generate a compact C header with git build metadata."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


def run_git(repo_root: Path, args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repo_root), *args],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return result.stdout.strip()


def normalize_text(value: str, fallback: str, max_len: int) -> str:
    value = (value or "").strip()
    if not value:
        value = fallback
    clean = []
    for char in value[:max_len]:
        code = ord(char)
        if 32 <= code < 127 and char not in {'"', "\\"}:
            clean.append(char)
        else:
            clean.append("_")
    return "".join(clean) or fallback


def env_override(name: str) -> str:
    return os.environ.get(name, "").strip()


def bool_from_env(value: str) -> int | None:
    if value == "":
        return None
    if value.lower() in {"1", "true", "yes", "on", "dirty"}:
        return 1
    if value.lower() in {"0", "false", "no", "off", "clean"}:
        return 0
    return None


def collect_metadata(repo_root: Path) -> tuple[str, str, int]:
    branch = env_override("WATCHER_GIT_BRANCH") or env_override("GITHUB_HEAD_REF")
    if not branch:
        branch = run_git(repo_root, ["rev-parse", "--abbrev-ref", "HEAD"])
    if branch == "HEAD":
        branch = env_override("CI_COMMIT_REF_NAME") or "detached"

    commit = env_override("WATCHER_GIT_COMMIT") or run_git(repo_root, ["rev-parse", "--short=12", "HEAD"])

    dirty_override = bool_from_env(env_override("WATCHER_GIT_DIRTY"))
    if dirty_override is None:
        dirty = 1 if run_git(repo_root, ["status", "--porcelain"]) else 0
    else:
        dirty = dirty_override

    return (
        normalize_text(branch, "unknown", 63),
        normalize_text(commit, "unknown", 16),
        dirty,
    )


def render_header(branch: str, commit: str, dirty: int) -> str:
    return f"""#ifndef WATCHER_BUILD_INFO_H
#define WATCHER_BUILD_INFO_H

#define WATCHER_BUILD_GIT_BRANCH \"{branch}\"
#define WATCHER_BUILD_GIT_COMMIT \"{commit}\"
#define WATCHER_BUILD_GIT_DIRTY {dirty}

#endif /* WATCHER_BUILD_INFO_H */
"""


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    output = Path(args.output).resolve()
    branch, commit, dirty = collect_metadata(repo_root)
    write_if_changed(output, render_header(branch, commit, dirty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
