import importlib.util
import json
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("check_resource_budget.py")
BUDGET_PATH = pathlib.Path(__file__).with_name("resource_budget.json")
SPEC = importlib.util.spec_from_file_location("check_resource_budget", MODULE_PATH)
RESOURCE_BUDGET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESOURCE_BUDGET)


class ResourceBudgetTests(unittest.TestCase):
    def setUp(self):
        self.size_summary = {
            "diram_data": 24_000,
            "diram_bss": 22_000,
            "diram_text": 108_000,
            "used_diram": 154_000,
            "used_iram": 16_000,
            "iram_remain": 1,
            "used_flash_non_ram": 3_140_000,
            "total_size": 3_290_000,
        }
        self.budget = {
            "schema_version": 1,
            "metrics": {
                "diram_data_bss": {
                    "baseline": 46_000,
                    "max_growth": 4_096,
                    "hard_max": 65_536,
                },
                "internal_static_total": {
                    "baseline": 170_000,
                    "max_growth": 4_096,
                    "hard_max": 180_000,
                },
                "instruction_ram_total": {"hard_max": 124_000},
                "total_size": {
                    "baseline": 3_290_000,
                    "max_growth": 65_536,
                    "hard_max": 3_600_000,
                },
            },
        }

    def test_derives_static_internal_data_and_bss(self):
        metrics = RESOURCE_BUDGET.collect_metrics(self.size_summary)
        self.assertEqual(46_000, metrics["diram_data_bss"])
        self.assertEqual(170_000, metrics["internal_static_total"])
        self.assertEqual(124_000, metrics["instruction_ram_total"])

    def test_windows_and_linux_idf_size_layouts_produce_same_derived_totals(self):
        linux_summary = dict(self.size_summary)
        linux_summary.update(
            {
                "diram_text": 0,
                "used_diram": 46_000,
                "used_iram": 124_000,
                "iram_remain": 238_000,
            }
        )
        windows_metrics = RESOURCE_BUDGET.collect_metrics(self.size_summary)
        linux_metrics = RESOURCE_BUDGET.collect_metrics(linux_summary)
        self.assertEqual(windows_metrics["internal_static_total"], linux_metrics["internal_static_total"])
        self.assertEqual(windows_metrics["instruction_ram_total"], linux_metrics["instruction_ram_total"])

    def test_accepts_values_within_growth_and_hard_limits(self):
        report = RESOURCE_BUDGET.evaluate_budget(self.size_summary, self.budget)
        self.assertEqual([], report["violations"])
        self.assertEqual(46_000, report["metrics"]["diram_data_bss"])

    def test_rejects_growth_beyond_baseline_allowance(self):
        self.size_summary["diram_bss"] += 4_097
        report = RESOURCE_BUDGET.evaluate_budget(self.size_summary, self.budget)
        self.assertEqual("diram_data_bss", report["violations"][0]["metric"])
        self.assertEqual("max", report["violations"][0]["kind"])
        self.assertEqual(50_096, report["violations"][0]["limit"])

    def test_hard_max_is_tighter_than_growth_allowance(self):
        self.budget["metrics"]["diram_data_bss"]["baseline"] = 64_000
        self.size_summary["diram_bss"] = 42_000
        report = RESOURCE_BUDGET.evaluate_budget(self.size_summary, self.budget)
        violation = report["violations"][0]
        self.assertEqual(65_536, violation["limit"])
        self.assertEqual(66_000, violation["actual"])

    def test_rejects_instruction_ram_growth(self):
        self.size_summary["diram_text"] += 1
        report = RESOURCE_BUDGET.evaluate_budget(self.size_summary, self.budget)
        violation = next(item for item in report["violations"] if item["metric"] == "instruction_ram_total")
        self.assertEqual("max", violation["kind"])

    def test_missing_required_metric_is_a_violation(self):
        del self.size_summary["used_diram"]
        report = RESOURCE_BUDGET.evaluate_budget(self.size_summary, self.budget)
        violation = next(item for item in report["violations"] if item["metric"] == "internal_static_total")
        self.assertEqual("missing", violation["kind"])

    def test_repository_static_internal_limits_are_consistent(self):
        with BUDGET_PATH.open("r", encoding="utf-8") as handle:
            repository_budget = json.load(handle)

        metrics = repository_budget["metrics"]
        diram_rule = metrics["diram_data_bss"]
        iram_rule = metrics["instruction_ram_total"]
        internal_rule = metrics["internal_static_total"]
        self.assertEqual(diram_rule["baseline"] + iram_rule["baseline"], internal_rule["baseline"])
        self.assertEqual(diram_rule["hard_max"] + iram_rule["hard_max"], internal_rule["hard_max"])


if __name__ == "__main__":
    unittest.main()
