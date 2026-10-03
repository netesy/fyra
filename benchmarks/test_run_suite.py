#!/usr/bin/env python3
import unittest
import tempfile
import importlib.util
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "benchmark_run_suite", Path(__file__).with_name("run_suite.py")
)
run_suite = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(run_suite)


class RuntimeStatisticsTest(unittest.TestCase):
    def test_distribution_is_preserved(self):
        result = run_suite.summarize_runtimes([0.10, 0.20, 0.30], "checksum")
        self.assertAlmostEqual(result["median"], 0.20)
        self.assertAlmostEqual(result["min"], 0.10)
        self.assertAlmostEqual(result["max"], 0.30)
        self.assertAlmostEqual(result["mean"], 0.20)
        self.assertEqual(result["samples"], 3)
        self.assertEqual(result["output"], "checksum")

    def test_stable_regression_is_reported(self):
        candidate = run_suite.summarize_runtimes([1.10, 1.10, 1.10])
        reference = run_suite.summarize_runtimes([1.00, 1.00, 1.00])
        self.assertEqual(run_suite.classify_runtime(candidate, reference), "regressed")

    def test_noise_prevents_false_ranking(self):
        candidate = run_suite.summarize_runtimes([0.80, 1.00, 1.20])
        reference = run_suite.summarize_runtimes([0.90, 1.00, 1.10])
        self.assertEqual(
            run_suite.classify_runtime(candidate, reference),
            "noisy/inconclusive",
        )

    def test_stack_direction_includes_implicit_push_pop(self):
        with tempfile.NamedTemporaryFile("w", suffix=".s") as asm:
            asm.write("pushq %rbp\nmovq -8(%rbp), %rax\naddq %rax, -16(%rbp)\npopq %rbp\n")
            asm.flush()
            result = run_suite.analyze_assembly(asm.name)
        self.assertEqual(result["stack_reading_ops"], 2)
        self.assertEqual(result["stack_writing_ops"], 1)
        self.assertEqual(result["stack_reading_ops_including_implicit"], 3)
        self.assertEqual(result["stack_writing_ops_including_implicit"], 2)

    def test_catalog_validation_detects_missing_and_duplicate_entries(self):
        duplicates, categories, metadata = run_suite.validate_benchmark_catalog(
            ["arithmetic", "arithmetic", "unknown"]
        )
        self.assertTrue(duplicates)
        self.assertEqual(categories, ["unknown"])
        self.assertEqual(metadata, ["unknown"])

    def test_checksum_mismatch_is_a_hard_failure(self):
        self.assertTrue(run_suite.checksums_match("checksum: 1", "checksum: 1"))
        self.assertFalse(run_suite.checksums_match("checksum: 1", "checksum: 2"))
        self.assertFalse(run_suite.checksums_match("", ""))

    def test_vector_profitability_uses_runtime_distribution(self):
        vector = run_suite.summarize_runtimes([0.50, 0.50, 0.50])
        scalar = run_suite.summarize_runtimes([1.00, 1.00, 1.00])
        self.assertEqual(run_suite.classify_vector_profitability(vector, scalar), "profitable")
        noisy = run_suite.summarize_runtimes([0.4, 1.0, 1.6])
        self.assertEqual(
            run_suite.classify_vector_profitability(noisy, scalar),
            "neutral/inconclusive",
        )

    def test_every_canonical_benchmark_has_coverage_metadata(self):
        for name in run_suite.BENCHMARK_CATEGORIES:
            self.assertIn(name, run_suite.BENCHMARK_METADATA)
            self.assertTrue(run_suite.BENCHMARK_METADATA[name]["categories"])
            self.assertTrue(run_suite.BENCHMARK_METADATA[name]["features"])


if __name__ == "__main__":
    unittest.main()
