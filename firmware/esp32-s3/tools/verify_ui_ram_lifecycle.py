#!/usr/bin/env python3
"""Verify launcher/voice/settings RAM lifecycle from ESP32 serial logs.

The firmware emits two complementary diagnostics:
- MEM_MONITOR lines carry internal heap values for a stage.
- UI_DIAG app_ui_diag_core lines carry LVGL screen counts for the same stage.

This tool pairs each voice/settings stable sample with the nearest previous and
next launcher stable sample. A passing cycle means:
- entering voice/settings lowers internal free RAM relative to launcher;
- returning to launcher raises internal free RAM relative to the app;
- Voice app transport stop does not lower internal free RAM before UI cleanup;
- repeated launcher samples do not drift down beyond a small tolerance;
- LVGL screen count does not grow across the app round-trip.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MEM_RE = re.compile(
    r"\[(?P<stage>[A-Za-z0-9_]+)\]\s+"
    r"(?P<status>\w+)(?:\([^)]*\))?\s+"
    r"int\{free=(?P<free>\d+)KB\s+largest=(?P<largest>\d+)KB\s+min=(?P<min>\d+)KB\}.*?"
    r"ctx\{app=(?P<app>[^ ]+)\s+resource=(?P<resource>[^}]+)\}"
)
UI_CORE_RE = re.compile(
    r"evt=app_ui_diag_core\s+stage=(?P<stage>[A-Za-z0-9_]+)\s+"
    r"screen_count=(?P<screen_count>\d+).*?"
    r"launcher_children=(?P<launcher_children>\d+)"
)
STALE_DISCOVERY_RE = re.compile(r"Ignoring stale discovery result")
DISCOVERY_WAIT_TIMEOUT_RE = re.compile(r"Discovery task still exiting")
APP_STABLE_STAGES = {"voice_stable_3s", "settings_stable_3s"}


@dataclass
class Sample:
    seq: int
    stage: str
    internal_free_kb: int | None = None
    internal_largest_kb: int | None = None
    app: str | None = None
    resource: str | None = None
    screen_count: int | None = None
    launcher_children: int | None = None


@dataclass
class CheckResult:
    ok: bool
    detail: str


@dataclass
class ParsedLog:
    samples: list[Sample]
    lifecycle_issues: list[str]


@dataclass
class Evaluation:
    stable_apps: list[Sample]
    results: list[CheckResult]
    sample_count: int


def iter_lines(paths: list[str]) -> Iterable[str]:
    for path in paths:
        if path == "-":
            yield from sys.stdin
            continue
        with Path(path).open("r", encoding="utf-8", errors="replace") as handle:
            yield from handle


def parse_lines(lines: Iterable[str]) -> ParsedLog:
    samples: list[Sample] = []
    pending_by_stage: dict[str, Sample] = {}
    lifecycle_issues: list[str] = []

    def new_sample(stage: str) -> Sample:
        sample = Sample(seq=len(samples), stage=stage)
        samples.append(sample)
        return sample

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if STALE_DISCOVERY_RE.search(line):
            lifecycle_issues.append(f"line {line_number}: stale discovery result reached foreground flow")
        if DISCOVERY_WAIT_TIMEOUT_RE.search(line):
            lifecycle_issues.append(f"line {line_number}: discovery task did not stop within close timeout")

        mem = MEM_RE.search(line)
        if mem:
            stage = mem.group("stage")
            sample = new_sample(stage)
            sample.internal_free_kb = int(mem.group("free"))
            sample.internal_largest_kb = int(mem.group("largest"))
            sample.app = mem.group("app")
            sample.resource = mem.group("resource")
            pending_by_stage[stage] = sample
            continue

        ui = UI_CORE_RE.search(line)
        if ui:
            stage = ui.group("stage")
            sample = pending_by_stage.pop(stage, None)
            if sample is None:
                sample = new_sample(stage)
            sample.screen_count = int(ui.group("screen_count"))
            sample.launcher_children = int(ui.group("launcher_children"))

    return ParsedLog(samples=samples, lifecycle_issues=lifecycle_issues)


def parse_log(paths: list[str]) -> ParsedLog:
    return parse_lines(iter_lines(paths))


def parse_samples(paths: list[str]) -> list[Sample]:
    return parse_log(paths).samples


def previous_launcher_for_cycle(samples: list[Sample], index: int) -> tuple[Sample | None, str | None]:
    for sample in reversed(samples[:index]):
        if sample.stage == "launcher_stable_3s" and sample.internal_free_kb is not None:
            return sample, None
        if sample.stage in APP_STABLE_STAGES and sample.internal_free_kb is not None:
            return None, f"missing previous launcher_stable_3s after {sample.stage}"
    return None, "missing previous launcher_stable_3s"


def next_launcher_for_cycle(samples: list[Sample], index: int) -> tuple[Sample | None, str | None]:
    for sample in samples[index + 1 :]:
        if sample.stage == "launcher_stable_3s" and sample.internal_free_kb is not None:
            return sample, None
        if sample.stage in APP_STABLE_STAGES and sample.internal_free_kb is not None:
            return None, f"missing next launcher_stable_3s before {sample.stage}"
    return None, "missing next launcher_stable_3s"


def voice_transport_stop_for_cycle(samples: list[Sample], start_index: int, end_index: int) -> Sample | None:
    for sample in samples[start_index + 1 : end_index]:
        if sample.stage == "after_voice_transport_stop" and sample.internal_free_kb is not None:
            return sample
    return None


def check_cycle(
    samples: list[Sample],
    app_sample: Sample,
    min_delta_kb: int,
    max_launcher_drop_kb: int,
    max_screen_growth: int,
) -> list[CheckResult]:
    results: list[CheckResult] = []
    index = app_sample.seq
    prev_launcher, prev_error = previous_launcher_for_cycle(samples, index)
    next_launcher, next_error = next_launcher_for_cycle(samples, index)

    if prev_launcher is None:
        return [CheckResult(False, f"{app_sample.stage}: {prev_error}")]
    if next_launcher is None:
        return [CheckResult(False, f"{app_sample.stage}: {next_error}")]
    if app_sample.internal_free_kb is None:
        return [CheckResult(False, f"{app_sample.stage}: missing MEM_MONITOR heap sample")]

    enter_delta = prev_launcher.internal_free_kb - app_sample.internal_free_kb
    exit_delta = next_launcher.internal_free_kb - app_sample.internal_free_kb
    launcher_drift = next_launcher.internal_free_kb - prev_launcher.internal_free_kb

    results.append(
        CheckResult(
            enter_delta >= min_delta_kb,
            f"{app_sample.stage}: enter drop={enter_delta:+d}KB "
            f"launcher_before={prev_launcher.internal_free_kb}KB app={app_sample.internal_free_kb}KB",
        )
    )
    results.append(
        CheckResult(
            exit_delta >= min_delta_kb,
            f"{app_sample.stage}: exit rise={exit_delta:+d}KB "
            f"app={app_sample.internal_free_kb}KB launcher_after={next_launcher.internal_free_kb}KB",
        )
    )
    results.append(
        CheckResult(
            launcher_drift >= -max_launcher_drop_kb,
            f"{app_sample.stage}: launcher drift={launcher_drift:+d}KB "
            f"allowed >= -{max_launcher_drop_kb}KB",
        )
    )

    if prev_launcher.screen_count is not None and next_launcher.screen_count is not None:
        screen_growth = next_launcher.screen_count - prev_launcher.screen_count
        results.append(
            CheckResult(
                screen_growth <= max_screen_growth,
                f"{app_sample.stage}: launcher screen_count growth={screen_growth:+d} "
                f"allowed <= +{max_screen_growth}",
            )
        )
    else:
        results.append(CheckResult(False, f"{app_sample.stage}: missing launcher screen_count sample"))

    if app_sample.stage == "voice_stable_3s":
        transport_stop = voice_transport_stop_for_cycle(samples, index, next_launcher.seq)
        if transport_stop is None:
            results.append(
                CheckResult(False, f"{app_sample.stage}: missing after_voice_transport_stop before launcher return")
            )
        else:
            transport_context_ok = (
                transport_stop.app == "voice.app"
                and transport_stop.resource is not None
                and "voice_stopped" in transport_stop.resource
            )
            results.append(
                CheckResult(
                    transport_context_ok,
                    f"{app_sample.stage}: voice transport stop context "
                    f"app={transport_stop.app} resource={transport_stop.resource}",
                )
            )
            transport_delta = transport_stop.internal_free_kb - app_sample.internal_free_kb
            results.append(
                CheckResult(
                    transport_delta >= 0,
                    f"{app_sample.stage}: voice transport stop delta={transport_delta:+d}KB "
                    f"voice={app_sample.internal_free_kb}KB transport_stop={transport_stop.internal_free_kb}KB",
                )
            )

    return results


def evaluate(
    parsed: ParsedLog,
    *,
    min_delta_kb: int,
    max_launcher_drop_kb: int,
    max_screen_growth: int,
    allow_missing_apps: bool,
) -> Evaluation:
    samples = parsed.samples
    stable_apps = [
        sample
        for sample in samples
        if sample.stage in ("voice_stable_3s", "settings_stable_3s")
        and sample.internal_free_kb is not None
    ]
    observed_stages = {sample.stage for sample in stable_apps}

    all_results: list[CheckResult] = []
    for issue in parsed.lifecycle_issues:
        all_results.append(CheckResult(False, f"transport lifecycle issue: {issue}"))

    if not stable_apps:
        all_results.append(CheckResult(False, "no voice_stable_3s or settings_stable_3s MEM_MONITOR samples found"))
    elif not allow_missing_apps:
        required_stages = {"voice_stable_3s", "settings_stable_3s"}
        missing_stages = sorted(required_stages - observed_stages)
        if missing_stages:
            all_results.append(
                CheckResult(False, f"missing required app lifecycle sample(s): {', '.join(missing_stages)}")
            )

    for app_sample in stable_apps:
        all_results.extend(
            check_cycle(
                samples,
                app_sample,
                min_delta_kb=min_delta_kb,
                max_launcher_drop_kb=max_launcher_drop_kb,
                max_screen_growth=max_screen_growth,
            )
        )

    return Evaluation(stable_apps=stable_apps, results=all_results, sample_count=len(samples))


def mem_line(stage: str, free_kb: int, app: str, resource: str) -> str:
    return (
        f"I MEM_MON: [{stage}] ok int{{free={free_kb}KB largest=64KB min=50KB}} "
        "dma{free=1KB largest=1KB min=1KB} 8bit{free=1KB largest=1KB min=1KB} "
        f"psram{{free=1KB largest=1KB min=1KB}} stack_hwm=1 ctx{{app={app} resource={resource}}}"
    )


def ui_line(stage: str, screen_count: int) -> str:
    return (
        f"W UI_DIAG: evt=app_ui_diag_core stage={stage} screen_count={screen_count} "
        "active=0x1 active_children=5 launcher_screen=0x1 launcher_children=5"
    )


def good_lifecycle_lines() -> list[str]:
    return [
        mem_line("launcher_stable_3s", 120, "launcher", "wifi"),
        ui_line("launcher_stable_3s", 4),
        mem_line("voice_stable_3s", 100, "voice.app", "wifi/voice_ready"),
        ui_line("voice_stable_3s", 5),
        mem_line("after_voice_transport_stop", 104, "voice.app", "wifi/voice_stopped"),
        mem_line("launcher_stable_3s", 121, "launcher", "wifi"),
        ui_line("launcher_stable_3s", 4),
        mem_line("settings_stable_3s", 102, "settings.app", "wifi"),
        ui_line("settings_stable_3s", 6),
        mem_line("launcher_stable_3s", 121, "launcher", "wifi"),
        ui_line("launcher_stable_3s", 4),
    ]


def run_self_test_case(name: str, lines: list[str], expected_failure: str | None) -> bool:
    evaluation = evaluate(
        parse_lines(lines),
        min_delta_kb=1,
        max_launcher_drop_kb=8,
        max_screen_growth=0,
        allow_missing_apps=False,
    )
    failures = [result.detail for result in evaluation.results if not result.ok]

    if expected_failure is None:
        ok = not failures
    else:
        ok = any(expected_failure in failure for failure in failures)

    print(f"SELFTEST {'PASS' if ok else 'FAIL'}: {name}")
    if not ok:
        print("  expected:", expected_failure if expected_failure is not None else "no failures")
        print("  actual:", failures if failures else "no failures")
    return ok


def run_self_tests() -> int:
    good = good_lifecycle_lines()
    cases = [
        ("complete lifecycle passes", good, None),
        (
            "stale discovery is rejected",
            ["I MAIN: Ignoring stale discovery result generation 1 (current 3)"] + good,
            "stale discovery result reached foreground flow",
        ),
        (
            "missing voice transport stop is rejected",
            [line for line in good if "after_voice_transport_stop" not in line],
            "missing after_voice_transport_stop before launcher return",
        ),
        (
            "wrong transport stop context is rejected",
            [
                mem_line("after_voice_transport_stop", 104, "launcher", "wifi")
                if "after_voice_transport_stop" in line
                else line
                for line in good
            ],
            "voice transport stop context",
        ),
        (
            "crossed app cycle is rejected",
            [
                line
                for line in good
                if "launcher_stable_3s] ok int{free=121KB" not in line
                and "stage=launcher_stable_3s screen_count=4 active=0x1" not in line
            ]
            + [mem_line("launcher_stable_3s", 121, "launcher", "wifi"), ui_line("launcher_stable_3s", 4)],
            "missing next launcher_stable_3s before settings_stable_3s",
        ),
    ]

    return 0 if all(run_self_test_case(*case) for case in cases) else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", help="Serial log file(s), or '-' for stdin")
    parser.add_argument("--self-test", action="store_true", help="Run built-in verifier self-tests")
    parser.add_argument("--min-delta-kb", type=int, default=1, help="Minimum RAM direction delta")
    parser.add_argument(
        "--max-launcher-drop-kb",
        type=int,
        default=8,
        help="Allowed launcher_stable_3s drop between app entry and return",
    )
    parser.add_argument(
        "--max-screen-growth",
        type=int,
        default=0,
        help="Allowed launcher screen_count growth across a round-trip",
    )
    parser.add_argument(
        "--allow-missing-apps",
        action="store_true",
        help="Allow logs that cover only voice or only settings; useful for partial debugging",
    )
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)

    if args.self_test:
        return run_self_tests()

    if not args.logs:
        print("FAIL: no log files provided; pass --self-test to run verifier self-tests")
        return 1

    evaluation = evaluate(
        parse_log(args.logs),
        min_delta_kb=args.min_delta_kb,
        max_launcher_drop_kb=args.max_launcher_drop_kb,
        max_screen_growth=args.max_screen_growth,
        allow_missing_apps=args.allow_missing_apps,
    )

    failed = [result for result in evaluation.results if not result.ok]
    for result in evaluation.results:
        prefix = "PASS" if result.ok else "FAIL"
        print(f"{prefix}: {result.detail}")

    print(
        f"SUMMARY: cycles={len(evaluation.stable_apps)} checks={len(evaluation.results)} "
        f"failed={len(failed)} samples={evaluation.sample_count}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
