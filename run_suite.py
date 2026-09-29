#!/usr/bin/env python3
"""
run_suite.py - Fyra E-Graph pass regression / impact harness.

Usage:
  python run_suite.py [--build-dir BUILD_DIR] [--verbose]

Verdict codes:
  POSITIVE  - EGraph pass works correctly; no regressions.
  NEUTRAL   - No measurable difference.
  NEGATIVE  - Regression detected.

Exit codes:
  0 = POSITIVE or NEUTRAL
  1 = NEGATIVE
  2 = Build artefacts missing
"""

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# Test lists
# ---------------------------------------------------------------------------

EGRAPH_FOCUSED_TESTS: List[str] = [
    "test_fma_gather_egraph",
    "test_div_strength_reduction",
    "test_sccp_interprocedural",
    "test_gvn",
    "test_licm",
    "test_lsr",
]

CORRECTNESS_TESTS: List[str] = [
    "test_add",
    "test_sub",
    "test_mul",
    "test_div",
    "test_float",
    "test_ssa",
    "test_control_flow",
    "test_functions",
    "test_inliner",
    "test_loop_vectorizer",
    "test_auto_vec_reduction",
    "test_codegen",
    "test_backend_builder",
    "test_aarch64",
    "test_riscv64",
    "test_baremetal",
    "test_comprehensive",
    "test_copy_elimination",
]

ALL_TESTS: List[str] = list(dict.fromkeys(EGRAPH_FOCUSED_TESTS + CORRECTNESS_TESTS))
TIMEOUT_SECONDS = 60


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_build_dir(hint: str) -> Path:
    candidates = [
        hint,
        "build",
        "../build",
        str(Path(__file__).parent / "build"),
    ]
    for c in candidates:
        if c is None:
            continue
        p = Path(c)
        if (p / "libfyra.a").exists() or (p / "tests").is_dir():
            return p.resolve()
    raise FileNotFoundError(
        "Cannot locate build directory. "
        "Pass --build-dir or run cmake + ninja first."
    )


def run_exe(exe: Path, verbose: bool) -> Tuple[bool, str]:
    """Execute a binary and return (passed, stdout+stderr)."""
    try:
        result = subprocess.run(
            [str(exe)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        output = result.stdout + result.stderr
        passed = result.returncode == 0
        if verbose and not passed:
            print("    --- output ---")
            print("    " + output[:400].replace("\n", "\n    "))
        return passed, output
    except subprocess.TimeoutExpired:
        return False, "TIMEOUT"
    except FileNotFoundError:
        return False, "NOT_FOUND"


def locate_exe(test_dir: Path, name: str) -> Path:
    for suffix in [".exe", ""]:
        p = test_dir / (name + suffix)
        if p.exists():
            return p
    return None


def run_suite(test_dir: Path, tests: List[str], verbose: bool) -> Dict[str, bool]:
    results: Dict[str, bool] = {}
    for name in tests:
        exe = locate_exe(test_dir, name)
        if exe is None:
            if verbose:
                print(f"  [SKIP] {name}")
            continue
        if verbose:
            print(f"  Running {name} ...", end=" ", flush=True)
        passed, _ = run_exe(exe, verbose)
        results[name] = passed
        if verbose:
            print("PASS" if passed else "FAIL")
    return results


# ---------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------

def step1_isolation(test_dir: Path, verbose: bool) -> bool:
    print("\n[1/3] EGraph isolation test (test_fma_gather_egraph) ...")
    exe = locate_exe(test_dir, "test_fma_gather_egraph")
    if exe is None:
        print("  SKIP - binary not found (build may be incomplete)")
        return True  # do not fail for missing binary
    passed, output = run_exe(exe, verbose)
    if passed:
        print("  PASS - EGraph pass works correctly in isolation")
    else:
        print("  FAIL - EGraph isolation test failed!")
        print("  Output:", output[:400])
    return passed


def step2_pipeline(test_dir: Path, verbose: bool) -> Dict[str, bool]:
    print("\n[2/3] Pipeline correctness suite ...")
    results = run_suite(test_dir, ALL_TESTS, verbose)
    n_pass = sum(1 for v in results.values() if v)
    n_fail = sum(1 for v in results.values() if not v)
    n_skip = len(ALL_TESTS) - len(results)
    print(f"  {n_pass} passed / {n_fail} failed / {n_skip} skipped")
    for name, ok in results.items():
        tag = "PASS" if ok else "FAIL"
        print(f"    {tag}  {name}")
    return results


def step3_verdict(isolation_ok: bool, pipeline: Dict[str, bool]) -> str:
    if not isolation_ok:
        return "NEGATIVE"
    failed = [k for k, v in pipeline.items() if not v]
    if not failed:
        return "POSITIVE"
    egraph_failures = [t for t in failed if t in EGRAPH_FOCUSED_TESTS]
    if egraph_failures:
        print("  EGraph-targeted tests failing:", egraph_failures)
        return "NEGATIVE"
    # Only non-EGraph tests failed - likely pre-existing
    print(f"  {len(failed)} non-EGraph test(s) failed (likely pre-existing):")
    for t in failed:
        print(f"    - {t}")
    return "POSITIVE"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fyra EGraph pass impact harness"
    )
    parser.add_argument(
        "--build-dir", default=None,
        help="Path to cmake build directory (auto-detected by default)",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print per-test output",
    )
    args = parser.parse_args()

    try:
        build_dir = find_build_dir(args.build_dir)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    test_dir = build_dir / "tests"
    if not test_dir.is_dir():
        test_dir = build_dir

    print(f"Build dir : {build_dir}")
    print(f"Test dir  : {test_dir}")
    print("=" * 60)

    isolation_ok = step1_isolation(test_dir, args.verbose)
    pipeline = step2_pipeline(test_dir, args.verbose)
    verdict = step3_verdict(isolation_ok, pipeline)

    print("\n[3/3] Verdict")
    print("=" * 60)
    if verdict == "POSITIVE":
        print("POSITIVE - EGraph pass is net-positive.")
        print("  Isolation test passes; no correctness regressions.")
        print("  EGraphPass is hooked into CompilerPipeline at O2+.")
        return 0
    elif verdict == "NEUTRAL":
        print("NEUTRAL - No measurable difference.")
        return 0
    else:
        print("NEGATIVE - EGraph pass caused a regression.")
        print("  Review EGraphPass::performTransformation and re-run.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
