#!/usr/bin/env python3
"""Validate ESP-IDF size JSON against reviewed firmware resource budgets."""

import argparse
import json
import pathlib
import sys


def collect_metrics(size_summary):
    metrics = {key: value for key, value in size_summary.items() if isinstance(value, (int, float))}
    if "diram_data" in metrics and "diram_bss" in metrics:
        metrics["diram_data_bss"] = metrics["diram_data"] + metrics["diram_bss"]
    if "used_diram" in metrics and "used_iram" in metrics:
        metrics["internal_static_total"] = metrics["used_diram"] + metrics["used_iram"]
    if "used_iram" in metrics and "diram_text" in metrics:
        metrics["instruction_ram_total"] = metrics["used_iram"] + metrics["diram_text"]
    return metrics


def _maximum_for(rule):
    limits = []
    if "baseline" in rule and "max_growth" in rule:
        limits.append(rule["baseline"] + rule["max_growth"])
    if "hard_max" in rule:
        limits.append(rule["hard_max"])
    return min(limits) if limits else None


def evaluate_budget(size_summary, budget):
    if budget.get("schema_version") != 1:
        raise ValueError("resource budget schema_version must be 1")
    rules = budget.get("metrics")
    if not isinstance(rules, dict) or not rules:
        raise ValueError("resource budget must define non-empty metrics")

    metrics = collect_metrics(size_summary)
    violations = []
    checks = []
    for metric_name, rule in rules.items():
        if not isinstance(rule, dict):
            raise ValueError(f"budget rule for {metric_name} must be an object")
        if metric_name not in metrics:
            violations.append({"metric": metric_name, "kind": "missing"})
            continue

        actual = metrics[metric_name]
        maximum = _maximum_for(rule)
        minimum = rule.get("min")
        check = {
            "metric": metric_name,
            "actual": actual,
            "baseline": rule.get("baseline"),
            "max": maximum,
            "min": minimum,
        }
        checks.append(check)

        if maximum is not None and actual > maximum:
            violations.append(
                {"metric": metric_name, "kind": "max", "actual": actual, "limit": maximum}
            )
        if minimum is not None and actual < minimum:
            violations.append(
                {"metric": metric_name, "kind": "min", "actual": actual, "limit": minimum}
            )

    return {"metrics": metrics, "checks": checks, "violations": violations}


def _load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _format_bytes(value):
    return f"{int(value):,} B ({value / 1024:.1f} KiB)"


def print_report(report):
    violations_by_metric = {item["metric"]: item for item in report["violations"]}
    for check in report["checks"]:
        status = "FAIL" if check["metric"] in violations_by_metric else "PASS"
        details = [f"actual={_format_bytes(check['actual'])}"]
        if check["baseline"] is not None:
            delta = check["actual"] - check["baseline"]
            details.append(f"baseline={_format_bytes(check['baseline'])}")
            details.append(f"delta={delta:+,} B")
        if check["max"] is not None:
            details.append(f"max={_format_bytes(check['max'])}")
        if check["min"] is not None:
            details.append(f"min={_format_bytes(check['min'])}")
        print(f"[{status}] {check['metric']}: " + ", ".join(details))

    for violation in report["violations"]:
        if violation["kind"] == "missing":
            print(f"[FAIL] {violation['metric']}: metric missing from ESP-IDF size JSON", file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size-json", required=True, type=pathlib.Path)
    parser.add_argument("--budget", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    try:
        report = evaluate_budget(_load_json(args.size_json), _load_json(args.budget))
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"resource budget configuration error: {error}", file=sys.stderr)
        return 2

    print_report(report)
    if report["violations"]:
        print(f"Resource budget failed with {len(report['violations'])} violation(s).", file=sys.stderr)
        return 1
    print("Resource budget passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
