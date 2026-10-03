#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import math
import statistics
import json
import csv
import re
import shutil
import argparse

# Categories describe the workload, not an expected optimization outcome.  Keep
# this mapping explicit so canonical results remain stable as files are added.
BENCHMARK_CATEGORIES = {
    "arithmetic": "scalar_integer",
    "int_widths": "scalar_integer",
    "bitwise_hash": "real_world_microkernels",
    "loops": "loops",
    "realistic_dot_product": "vector",
    "reg_pressure": "register_pressure",
    "simd_loop_liveness": "vector",
    "tail_recursion": "calls",
}

BENCHMARK_METADATA = {
    "arithmetic": {"categories": ["integer_arithmetic"], "features": ["mixed_ops", "spill_materialization"]},
    "bitwise_hash": {"categories": ["bit_manipulation", "hashing"], "features": ["xor", "shift", "multiply", "dependency_chain"]},
    "branch_state": {"categories": ["branch_heavy", "parser_state_machine"], "features": ["data_dependent_branch", "nested_condition", "early_exit"]},
    "int_widths": {"categories": ["integer_arithmetic", "reductions", "vector"], "features": ["sext", "zext", "i64_reduction", "scalar_epilogue"]},
    "loops": {"categories": ["loops"], "features": ["scev_closed_form", "asymptotic"]},
    "nested_loops": {"categories": ["nested_loops", "reductions"], "features": ["runtime_bounds", "multiple_inductions"]},
    "realistic_dot_product": {"categories": ["fp_reductions", "vector"], "features": ["dot_product", "horizontal_reduction"]},
    "reg_pressure": {"categories": ["high_gpr_pressure"], "features": ["long_live_ranges", "spill_materialization"]},
    "simd_loop_liveness": {"categories": ["high_simd_pressure", "vector"], "features": ["vector_liveness", "register_native_simd"]},
    "tail_recursion": {"categories": ["calls"], "features": ["tail_call_optimization", "recursion"]},
}

# Primary summary category. Coverage metadata above may intentionally associate
# one workload with several compiler behaviors.
BENCHMARK_CATEGORIES.update({
    "branch_state": "control_flow",
    "nested_loops": "loops",
})

BENCHMARKS_DIR = os.path.dirname(os.path.abspath(__file__))
CORPUS_C_DIR = os.path.join(BENCHMARKS_DIR, "corpus", "c")
CORPUS_FYRA_DIR = os.path.join(BENCHMARKS_DIR, "corpus", "fyra")
BUILD_DIR = os.path.abspath(os.path.join(BENCHMARKS_DIR, "..", "build"))
# Choose binary name based on OS
if sys.platform.startswith("win"):
    binary_name = "fyra_compiler.exe"
else:
    binary_name = "fyra_compiler"
FYRA_BIN = os.path.join(BUILD_DIR, binary_name)

import tempfile
msys_tmp = "C:/msys64/tmp" if os.path.exists("C:/msys64/tmp") else tempfile.gettempdir()
os.environ["TMPDIR"] = msys_tmp
os.environ["TMP"] = msys_tmp
os.environ["TEMP"] = msys_tmp

def geomean(iterable):
    vals = [x for x in iterable if x > 0]
    if not vals:
        return 0.0
    return math.exp(sum(math.log(x) for x in vals) / len(vals))

def analyze_assembly(asm_file):
    if not os.path.exists(asm_file):
        return {
            "total": 0, "loads": 0, "stores": 0, "moves": 0,
            "branches": 0, "calls": 0, "frame_size": 0,
            "vector_instrs": 0, "assembly_stack_stores": 0,
            "assembly_stack_loads": 0, "stack_reading_ops": 0,
            "stack_writing_ops": 0,
            "stack_reading_ops_including_implicit": 0,
            "stack_writing_ops_including_implicit": 0,
            "max_vector_width": 0
        }

    total = 0
    loads = 0
    stores = 0
    moves = 0
    branches = 0
    calls = 0
    frame_size = 0
    vector_instrs = 0
    spills = 0
    reloads = 0
    stack_reading_ops = 0
    stack_writing_ops = 0
    implicit_stack_reads = 0
    implicit_stack_writes = 0
    max_vector_width = 0

    vector_op_prefixes = (
        'v', 'padd', 'psub', 'pmul', 'pand', 'por', 'pxor', 'psll', 'psrl', 'psra',
        'pinsr', 'pextr', 'movdqu', 'movaps', 'movups', 'movdqa', 'movd', 'movq',
        'addps', 'subps', 'mulps', 'divps', 'addpd', 'subpd', 'mulpd', 'divpd'
    )

    with open(asm_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('.') or line.startswith('#') or line.endswith(':'):
                if ('subq' in line and '%rsp' in line) or ('sub' in line and 'rsp' in line):
                    m = re.search(r'\$(\d+)', line) or re.search(r', (\d+)', line)
                    if m:
                        frame_size = int(m.group(1))
                continue

            total += 1
            parts = line.split()
            op = parts[0].lower() if parts else ""

            stack_ref = ('%rbp)' in line or '[rbp' in line or '%rsp)' in line or '[rsp' in line)
            # push/pop access the stack implicitly even though their textual
            # operand is a register rather than an rbp/rsp memory expression.
            if op.startswith('push'):
                implicit_stack_writes += 1
            elif op.startswith('pop'):
                implicit_stack_reads += 1
            if stack_ref and not op.startswith('lea'):
                operands = line[len(parts[0]):].split(',') if parts else []
                destination_is_stack = bool(operands) and any(x in operands[-1] for x in ('%rbp)', '[rbp', '%rsp)', '[rsp'))
                if op.startswith('mov'):
                    stack_writing_ops += int(destination_is_stack)
                    stack_reading_ops += int(not destination_is_stack)
                elif op.startswith(('pop', 'push')):
                    pass  # Counted above because the stack access is implicit.
                else:
                    stack_reading_ops += 1
                    # x64 read/modify/write forms have a memory destination.
                    stack_writing_ops += int(destination_is_stack)

            if ('subq' in op or 'sub' in op) and ('rsp' in line or '%rsp' in line) and ('$' in line or ',' in line):
                m = re.search(r'\$(\d+)', line) or re.search(r', (\d+)', line)
                if m:
                    frame_size = int(m.group(1))

            # Non-x86 vector spellings: AArch64 NEON, RISC-V V, and WASM SIMD.
            non_x86_vector = bool(
                re.search(r'\bv(?:mm)?\d+\.(?:16b|8b|8h|4h|4s|2s|2d)\b', line) or
                re.search(r'\bv(?:setvli|le\d+|se\d+|add|sub|mul|div|and|or|xor)\.', op) or
                re.search(r'\b(?:i8x16|i16x8|i32x4|i64x2|f32x4|f64x2)\.', line)
            )
            if non_x86_vector:
                vector_instrs += 1
                max_vector_width = max(max_vector_width, 128)

            if not non_x86_vector and op.startswith(vector_op_prefixes) and op not in ('var', 'val'):
                vector_instrs += 1
                if 'zmm' in line:
                    max_vector_width = max(max_vector_width, 512)
                elif 'ymm' in line:
                    max_vector_width = max(max_vector_width, 256)
                else:
                    max_vector_width = max(max_vector_width, 128)

            if op in ('call', 'callq'):
                calls += 1
            elif op.startswith('j') or op in ('ret', 'retq'):
                branches += 1
            elif 'mov' in op or 'push' in op or 'pop' in op:
                if '(' in line or ')' in line or 'ptr' in line:
                    is_stack_ref = ('%rbp)' in line or '[rbp' in line or '%rsp)' in line or '[rsp' in line)
                    if 'push' in op or ('mov' in op and is_stack_ref):
                        if op.startswith('mov') and ('(%rbp)' in line.split(',')[-1] or '[rbp' in line.split(',')[-1] or '(%rsp)' in line.split(',')[-1] or '[rsp' in line.split(',')[-1]):
                            stores += 1
                            if is_stack_ref:
                                spills += 1
                        else:
                            loads += 1
                            if is_stack_ref:
                                reloads += 1
                    else:
                        loads += 1
                elif 'mov' in op:
                    moves += 1

    return {
        "total": total,
        "loads": loads,
        "stores": stores,
        "moves": moves,
        "branches": branches,
        "calls": calls,
        "frame_size": frame_size,
        "vector_instrs": vector_instrs,
        # These are syntactic assembly stack accesses, not allocator events.
        "assembly_stack_stores": spills,
        "assembly_stack_loads": reloads,
        "stack_reading_ops": stack_reading_ops,
        "stack_writing_ops": stack_writing_ops,
        "stack_reading_ops_including_implicit": stack_reading_ops + implicit_stack_reads,
        "stack_writing_ops_including_implicit": stack_writing_ops + implicit_stack_writes,
        "max_vector_width": max_vector_width
    }

def run_cmd(cmd, timeout=30.0):
    try:
        p = subprocess.run(cmd, shell=True, env=os.environ, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", f"timeout after {timeout}s"

def run_exec(exec_path, timeout=30.0):
    if not os.path.exists(exec_path) and os.path.exists(exec_path + ".exe"):
        exec_path = exec_path + ".exe"
    try:
        p = subprocess.run([exec_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", f"timeout after {timeout}s"

def summarize_runtimes(runtimes, output=""):
    """Return distribution data without hiding noisy measurements in a median."""
    if not runtimes:
        return {
            "median": 0.0, "min": 0.0, "max": 0.0, "mean": 0.0,
            "stddev": 0.0, "cv": 0.0, "samples": 0, "output": ""
        }

    mean = statistics.mean(runtimes)
    stdev = statistics.stdev(runtimes) if len(runtimes) > 1 else 0.0
    return {
        "median": statistics.median(runtimes),
        "min": min(runtimes),
        "max": max(runtimes),
        "mean": mean,
        "stddev": stdev,
        "cv": stdev / mean if mean > 0 else 0.0,
        "samples": len(runtimes),
        "output": output,
    }


def classify_runtime(candidate, reference):
    """Classify a same-run comparison while accounting for observed noise.

    A 3% difference is only called meaningful when it also exceeds two pooled
    coefficients of variation.  Large variability is reported explicitly
    rather than converted into a spurious compiler ranking.
    """
    if candidate["median"] <= 0 or reference["median"] <= 0:
        return "unavailable"
    ratio = candidate["median"] / reference["median"]
    noise_band = max(0.03, 2.0 * math.hypot(candidate["cv"], reference["cv"]))
    if abs(ratio - 1.0) <= noise_band:
        return "noisy/inconclusive" if noise_band > 0.05 else "unchanged"
    return "improved" if ratio < 1.0 else "regressed"


def classify_vector_profitability(vector, scalar):
    classification = classify_runtime(vector, scalar)
    return {
        "improved": "profitable",
        "regressed": "unprofitable",
        "unchanged": "neutral/inconclusive",
        "noisy/inconclusive": "neutral/inconclusive",
        "unavailable": "unavailable",
    }[classification]


def validate_benchmark_catalog(names):
    duplicates = len(names) != len(set(names))
    uncategorized = [name for name in names if name not in BENCHMARK_CATEGORIES]
    missing_metadata = [name for name in names if name not in BENCHMARK_METADATA]
    return duplicates, uncategorized, missing_metadata


def checksums_match(*outputs):
    return bool(outputs) and bool(outputs[0]) and all(value == outputs[0] for value in outputs)


def measure_execution(exec_path, samples=15, warmup=2, timeout=30.0):
    if not os.path.exists(exec_path) and os.path.exists(exec_path + ".exe"):
        exec_path = exec_path + ".exe"
    if not os.path.exists(exec_path):
        return summarize_runtimes([])

    for _ in range(warmup):
        run_exec(exec_path, timeout=timeout)

    runtimes = []
    output = ""
    for _ in range(samples):
        t0 = time.perf_counter()
        rc, out, err = run_exec(exec_path, timeout=timeout)
        t1 = time.perf_counter()
        if rc == 0:
            runtimes.append(t1 - t0)
            output = out.strip()

    if not runtimes:
        return summarize_runtimes([])

    return summarize_runtimes(runtimes, output)

def verify_static(exec_path):
    rc, out, err = run_cmd(f"readelf -d {exec_path}")
    return "There is no dynamic section in this file" in out or "no dynamic section" in out

def parse_args():
    parser = argparse.ArgumentParser(description="Fyra Backend — Multi-Category Benchmark Harness")
    parser.add_argument("--filter", type=str, default=os.environ.get("FYRA_BENCH_FILTER", ""), help="Comma-separated list of benchmarks to run")
    # Determine a sensible default target based on the host OS
    if sys.platform.startswith("win"):
        default_target = "x64-windows"
    else:
        default_target = "x64-linux"
    parser.add_argument("--targets", type=str, default=os.environ.get("FYRA_BENCH_TARGETS", default_target), help="Comma-separated list of target architectures (x64-windows, x64-linux, aarch64-linux, riscv64-linux, wasm32-wasi …)")
    parser.add_argument("--samples", type=int, default=int(os.environ.get("FYRA_BENCH_SAMPLES", "15")), help="Number of timing samples per benchmark")
    parser.add_argument("--warmup", type=int, default=int(os.environ.get("FYRA_BENCH_WARMUP", "2")), help="Number of warmup executions per benchmark")
    parser.add_argument("--quick", action="store_true", help="Use three samples and one warmup for a fast correctness-oriented run")
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("FYRA_BENCH_TIMEOUT", "30")), help="Execution timeout in seconds")
    parser.add_argument("--verbose", action="store_true", help="Print verbose assembly analysis details")
    parser.add_argument("--json", type=str, default="", help="Custom JSON output file path")
    parser.add_argument("--csv", type=str, default="", help="Custom CSV output file path")
    return parser.parse_args()

def main():
    args = parse_args()
    if args.quick:
        args.samples = 3
        args.warmup = 1

    print("==========================================================================")
    print(" Fyra Backend — Multi-Category Benchmark Harness (Granular Vector Metrics)")
    print("==========================================================================")

    if not os.path.exists(FYRA_BIN):
        print(f"Error: Fyra compiler not found at {FYRA_BIN}. Please build first.")
        sys.exit(1)

    bench_names = [f[:-2] for f in os.listdir(CORPUS_C_DIR) if f.endswith(".c")]
    bench_names.sort()

    duplicates, uncategorized, missing_metadata = validate_benchmark_catalog(bench_names)
    if duplicates or uncategorized or missing_metadata:
        details = uncategorized + missing_metadata
        print("Error: benchmarks require unique identifiers, categories, and metadata: " + ", ".join(details))
        return 1

    requested = {name.strip() for name in args.filter.split(",") if name.strip()}
    if requested:
        bench_names = [name for name in bench_names if name in requested]

    results = []

    for bname in bench_names:
        c_src = os.path.join(CORPUS_C_DIR, f"{bname}.c")
        fyra_src = os.path.join(CORPUS_FYRA_DIR, f"{bname}.fyra")

        if not os.path.exists(fyra_src):
            print(f"Skipping {bname}: missing .fyra source.")
            continue

        out_dir = os.path.join(BENCHMARKS_DIR, "output", bname)
        shutil.rmtree(out_dir, ignore_errors=True)
        os.makedirs(out_dir, exist_ok=True)
        print(f"Benchmarking {bname}...", flush=True)

        gcc_s = os.path.join(out_dir, "gcc.s")
        gcc_exec = os.path.join(out_dir, "gcc_exec")
        clang_s = os.path.join(out_dir, "clang.s")
        clang_exec = os.path.join(out_dir, "clang_exec")

        c_src_f = c_src.replace('\\', '/')
        gcc_s_f = gcc_s.replace('\\', '/')
        gcc_exec_f = gcc_exec.replace('\\', '/')
        clang_s_f = clang_s.replace('\\', '/')
        clang_exec_f = clang_exec.replace('\\', '/')
        fyra_src_f = fyra_src.replace('\\', '/')
        fyra_bin_f = FYRA_BIN.replace('\\', '/')

        static_flag = "-static" if sys.platform != "win32" else ""
        no_pie_flag = "-no-pie" if sys.platform != "win32" else ""

        commands = [
            f"gcc {static_flag} -O2 {c_src_f} -S -o {gcc_s_f}",
            f"gcc {static_flag} -O2 {c_src_f} -o {gcc_exec_f}",
            f"clang {static_flag} -O2 {c_src_f} -S -o {clang_s_f}",
            f"clang {static_flag} -O2 {c_src_f} -o {clang_exec_f}",
        ]

        target_list = [t.strip() for t in args.targets.split(",") if t.strip()]
        target_metrics = {}

        for target_triple in target_list:
            t_sanitized = target_triple.replace("-", "_")
            t_o2_s = os.path.join(out_dir, f"fyra_{t_sanitized}_o2.s")
            t_scalar_s = os.path.join(out_dir, f"fyra_{t_sanitized}_scalar.s")
            t_o2_s_f = t_o2_s.replace('\\', '/')
            t_scalar_s_f = t_scalar_s.replace('\\', '/')

            # Hold loop unrolling constant so this harness isolates SLP and
            # e-graph effects; unrolling has dedicated semantic tests.
            cmd_o2 = f"{fyra_bin_f} {fyra_src_f} --target {target_triple} -o {t_o2_s_f} -O2 --no-unroll"
            cmd_scalar = f"{fyra_bin_f} {fyra_src_f} --target {target_triple} -o {t_scalar_s_f} -O2 --no-unroll --disable-slp --disable-loop-vectorization"

            rc1, stdout1, stderr1 = run_cmd(cmd_o2, timeout=args.timeout)
            if rc1 != 0:
                print(f"[FAILED] {bname} ({target_triple}): command failed ({rc1}): {cmd_o2}\n{stderr1}")
                return 1

            rc2, stdout2, stderr2 = run_cmd(cmd_scalar, timeout=args.timeout)
            if rc2 != 0:
                print(f"[FAILED] {bname} ({target_triple}): command failed ({rc2}): {cmd_scalar}\n{stderr2}")
                return 1

            t_o2_s_real = t_o2_s + ".s" if os.path.exists(t_o2_s + ".s") else (t_o2_s + ".wat" if os.path.exists(t_o2_s + ".wat") else t_o2_s)
            asm_data = analyze_assembly(t_o2_s_real)
            target_metrics[target_triple] = asm_data
            print(f"  [{target_triple:<15}] Output verified | Instrs: {asm_data['total']:<4} | Loads: {asm_data['loads']:<3} | Stores: {asm_data['stores']:<3} | VecInstrs: {asm_data['vector_instrs']}")

        fyra_o1_s = os.path.join(out_dir, "fyra_o1.s")
        fyra_o2_s = os.path.join(out_dir, "fyra_o2.s")
        fyra_scalar_s = os.path.join(out_dir, "fyra_scalar.s")
        fyra_no_egraph_s = os.path.join(out_dir, "fyra_no_egraph.s")
        fyra_exec = os.path.join(out_dir, "fyra_exec")
        fyra_scalar_exec = os.path.join(out_dir, "fyra_scalar_exec")

        fyra_o1_s_f = fyra_o1_s.replace('\\', '/')
        fyra_o2_s_f = fyra_o2_s.replace('\\', '/')
        fyra_scalar_s_f = fyra_scalar_s.replace('\\', '/')
        fyra_no_egraph_s_f = fyra_no_egraph_s.replace('\\', '/')

        commands += [
            f"{fyra_bin_f} {fyra_src_f} --target x64-linux -o {fyra_o1_s_f} -O1 --no-unroll",
            f"{fyra_bin_f} {fyra_src_f} --target x64-linux -o {fyra_o2_s_f} -O2 --no-unroll",
            f"{fyra_bin_f} {fyra_src_f} --target x64-linux -o {fyra_scalar_s_f} -O2 --no-unroll --disable-slp --disable-loop-vectorization",
            f"{fyra_bin_f} {fyra_src_f} --target x64-linux -o {fyra_no_egraph_s_f} -O2 --no-unroll --disable-egraph",
        ]
        compile_seconds = {"gcc": 0.0, "clang": 0.0, "fyra": 0.0}
        for command in commands:
            t0 = time.perf_counter()
            rc, stdout, stderr = run_cmd(command, timeout=args.timeout)
            elapsed = time.perf_counter() - t0
            compiler = "gcc" if command.startswith("gcc ") else ("clang" if command.startswith("clang ") else "fyra")
            compile_seconds[compiler] += elapsed
            if args.verbose:
                print(f"  Command '{command}' took {elapsed:.3f}s", flush=True)
            if rc != 0:
                print(f"[FAILED] {bname}: command failed ({rc}): {command}\n{stderr}")
                return 1

        fyra_o1_s = fyra_o1_s + ".s" if os.path.exists(fyra_o1_s + ".s") else fyra_o1_s
        fyra_o2_s = fyra_o2_s + ".s" if os.path.exists(fyra_o2_s + ".s") else fyra_o2_s
        fyra_scalar_s = fyra_scalar_s + ".s" if os.path.exists(fyra_scalar_s + ".s") else fyra_scalar_s
        fyra_no_egraph_s = fyra_no_egraph_s + ".s" if os.path.exists(fyra_no_egraph_s + ".s") else fyra_no_egraph_s

        harness_c = os.path.join(BENCHMARKS_DIR, "harness.c")
        harness_c_f = harness_c.replace('\\', '/')
        fyra_o2_s_f = fyra_o2_s.replace('\\', '/')
        fyra_scalar_s_f = fyra_scalar_s.replace('\\', '/')
        fyra_exec_f = fyra_exec.replace('\\', '/')
        fyra_scalar_exec_f = fyra_scalar_exec.replace('\\', '/')

        if sys.platform == "win32":
            for s_path in [fyra_o2_s, fyra_scalar_s]:
                if os.path.exists(s_path):
                    with open(s_path, 'r') as sf:
                        filtered = [l for l in sf.readlines() if not re.match(r'^\s*\.(type|size|section\s+\.note\.GNU-stack)', l)]
                    with open(s_path, 'w') as sf:
                        sf.writelines(filtered)

        link_seconds = 0.0
        for command in [f"gcc {static_flag} {no_pie_flag} {fyra_o2_s_f} {harness_c_f} -o {fyra_exec_f}",
                        f"gcc {static_flag} {no_pie_flag} {fyra_scalar_s_f} {harness_c_f} -o {fyra_scalar_exec_f}"]:
            t0 = time.perf_counter()
            rc, stdout, stderr = run_cmd(command, timeout=args.timeout)
            link_seconds += time.perf_counter() - t0
            if rc != 0:
                print(f"[FAILED] {bname}: command failed ({rc}): {command}\n{stderr}")
                return 1

        # Assembly Analysis
        gcc_asm = analyze_assembly(gcc_s)
        clang_asm = analyze_assembly(clang_s)
        fyra_asm = analyze_assembly(fyra_o2_s)
        fyra_scalar_asm = analyze_assembly(fyra_scalar_s)
        fyra_no_egraph_asm = analyze_assembly(fyra_no_egraph_s)

        # Measure Execution Runtimes
        gcc_perf = measure_execution(gcc_exec, samples=args.samples, warmup=args.warmup, timeout=args.timeout)
        clang_perf = measure_execution(clang_exec, samples=args.samples, warmup=args.warmup, timeout=args.timeout)
        fyra_perf = measure_execution(fyra_exec, samples=args.samples, warmup=args.warmup, timeout=args.timeout)
        fyra_scalar_perf = measure_execution(fyra_scalar_exec, samples=args.samples, warmup=args.warmup, timeout=args.timeout)

        # Correctness Verification
        correct = checksums_match(gcc_perf["output"], clang_perf["output"],
                                  fyra_perf["output"], fyra_scalar_perf["output"])
        fyra_static = verify_static(fyra_exec)

        entry = {
            "name": bname,
            "category": BENCHMARK_CATEGORIES[bname],
            "categories": BENCHMARK_METADATA[bname]["categories"],
            "features": BENCHMARK_METADATA[bname]["features"],
            # Corpus programs currently time their kernel loop and process
            # startup together.  Naming the scope prevents these samples from
            # being mistaken for an in-process kernel timer.
            "timing_scope": "process_with_internal_kernel_loop",
            "correct": correct,
            "static_linked": fyra_static,
            "gcc_compile_time": compile_seconds["gcc"],
            "clang_compile_time": compile_seconds["clang"],
            "fyra_compile_time": compile_seconds["fyra"],
            "fyra_link_time": link_seconds,
            "fyra_binary_size": os.path.getsize(fyra_exec) if os.path.exists(fyra_exec) else 0,
            "gcc_time": gcc_perf["median"],
            "clang_time": clang_perf["median"],
            "fyra_time": fyra_perf["median"],
            "fyra_scalar_time": fyra_scalar_perf["median"],
            "gcc_time_min": gcc_perf["min"],
            "gcc_time_max": gcc_perf["max"],
            "gcc_time_mean": gcc_perf["mean"],
            "gcc_time_stddev": gcc_perf["stddev"],
            "gcc_time_cv": gcc_perf["cv"],
            "gcc_time_samples": gcc_perf["samples"],
            "clang_time_min": clang_perf["min"],
            "clang_time_max": clang_perf["max"],
            "clang_time_mean": clang_perf["mean"],
            "clang_time_stddev": clang_perf["stddev"],
            "clang_time_cv": clang_perf["cv"],
            "clang_time_samples": clang_perf["samples"],
            "fyra_time_min": fyra_perf["min"],
            "fyra_time_max": fyra_perf["max"],
            "fyra_time_mean": fyra_perf["mean"],
            "fyra_time_stddev": fyra_perf["stddev"],
            "fyra_time_cv": fyra_perf["cv"],
            "fyra_time_samples": fyra_perf["samples"],
            "fyra_scalar_time_min": fyra_scalar_perf["min"],
            "fyra_scalar_time_max": fyra_scalar_perf["max"],
            "fyra_scalar_time_mean": fyra_scalar_perf["mean"],
            "fyra_scalar_time_stddev": fyra_scalar_perf["stddev"],
            "fyra_scalar_time_cv": fyra_scalar_perf["cv"],
            "fyra_scalar_time_samples": fyra_scalar_perf["samples"],
            "fyra_over_gcc": (fyra_perf["median"] / gcc_perf["median"] if gcc_perf["median"] > 0 else 0.0),
            "fyra_over_clang": (fyra_perf["median"] / clang_perf["median"] if clang_perf["median"] > 0 else 0.0),
            "fyra_vs_gcc": classify_runtime(fyra_perf, gcc_perf),
            "fyra_vs_clang": classify_runtime(fyra_perf, clang_perf),
            "fyra_speedup": (fyra_scalar_perf["median"] / fyra_perf["median"] if fyra_perf["median"] > 0 else 0.0),
            "vector_profitability": classify_vector_profitability(fyra_perf, fyra_scalar_perf),
            "gcc_instrs": gcc_asm["total"],
            "clang_instrs": clang_asm["total"],
            "fyra_instrs": fyra_asm["total"],
            "fyra_scalar_instrs": fyra_scalar_asm["total"],
            "gcc_mem": gcc_asm["loads"] + gcc_asm["stores"],
            "clang_mem": clang_asm["loads"] + clang_asm["stores"],
            "fyra_mem": fyra_asm["loads"] + fyra_asm["stores"],
            "fyra_scalar_mem": fyra_scalar_asm["loads"] + fyra_scalar_asm["stores"],
            "fyra_frame_size": fyra_asm["frame_size"],
            "clang_frame_size": clang_asm["frame_size"],
            "fyra_vector_instrs": fyra_asm["vector_instrs"],
            "fyra_assembly_stack_stores": fyra_asm["assembly_stack_stores"],
            "fyra_assembly_stack_loads": fyra_asm["assembly_stack_loads"],
            "fyra_stack_reading_ops": fyra_asm["stack_reading_ops"],
            "fyra_stack_writing_ops": fyra_asm["stack_writing_ops"],
            "fyra_stack_reading_ops_including_implicit": fyra_asm["stack_reading_ops_including_implicit"],
            "fyra_stack_writing_ops_including_implicit": fyra_asm["stack_writing_ops_including_implicit"],
            "max_vector_width": fyra_asm["max_vector_width"]
        }
        entry["egraph_enabled"] = True
        entry["no_egraph_instrs"] = fyra_no_egraph_asm["total"]
        entry["egraph_instr_reduction"] = fyra_no_egraph_asm["total"] - fyra_asm["total"]
        for target_triple in target_list:
            prefix = target_triple.replace("-", "_")
            metrics = target_metrics[target_triple]
            entry[f"{prefix}_vector_instrs"] = metrics["vector_instrs"]
            entry[f"{prefix}_max_vector_width"] = metrics["max_vector_width"]
        results.append(entry)

        status = "PASSED" if correct else "FAILED"
        print(f"[{status}] {bname:<24} | Fyra: {fyra_perf['median']:.6f}s "
              f"[{fyra_perf['min']:.6f}, {fyra_perf['max']:.6f}] "
              f"n={fyra_perf['samples']} CV={fyra_perf['cv']:.1%} | "
              f"Fyra/GCC: {entry['fyra_over_gcc']:.3f} ({entry['fyra_vs_gcc']}) | "
              f"Fyra/Clang: {entry['fyra_over_clang']:.3f} ({entry['fyra_vs_clang']}) | "
              f"Vector speedup: {entry['fyra_speedup']:.3f}x")
        if fyra_perf["median"] < 0.100:
            print("  timing note: interval is below 100 ms; treat runtime ranking as provisional")

        if args.verbose:
            print(f"   Assembly detail: instrs={fyra_asm['total']}, loads={fyra_asm['loads']}, stores={fyra_asm['stores']}, assembly_stack_stores={fyra_asm['assembly_stack_stores']}, assembly_stack_loads={fyra_asm['assembly_stack_loads']}, frame_size={fyra_asm['frame_size']}")

    failed = [r["name"] for r in results if not r["correct"]]
    if failed:
        print(f"Refusing to update result files; failed benchmarks: {', '.join(failed)}")
        return 1

    names = [r["name"] for r in results]
    if len(names) != len(set(names)):
        print("Refusing to update result files; duplicate benchmark identifiers detected.")
        return 1

    if requested:
        print("Filtered run complete; canonical CSV/JSON results were not updated.")
        return 0

    json_path = args.json if args.json else os.path.join(BENCHMARKS_DIR, "benchmark_results.json")
    csv_path = args.csv if args.csv else os.path.join(BENCHMARKS_DIR, "benchmark_results.csv")

    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)

    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=results[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(results)

    print("\n--------------------------------------------------------------------------")
    print(" Benchmark Category Runtime Summary")
    print("--------------------------------------------------------------------------")

    categories = sorted({r["category"] for r in results})
    for category in categories:
        members = [r for r in results if r["category"] == category]
        gcc_ratio = geomean(r["fyra_over_gcc"] for r in members)
        clang_ratio = geomean(r["fyra_over_clang"] for r in members)
        print(f" {category:<22} Fyra/GCC: {gcc_ratio:.3f}x | "
              f"Fyra/Clang: {clang_ratio:.3f}x | workloads: {len(members)}")
    print(" Runtime ratios are reported by category; static counts remain diagnostics, not a compiler score.")
    no_egraph_total = sum(r["no_egraph_instrs"] for r in results)
    egraph_total = sum(r["fyra_instrs"] for r in results)
    egraph_reduction = no_egraph_total - egraph_total
    egraph_percent = (100.0 * egraph_reduction / no_egraph_total) if no_egraph_total else 0.0
    print(f" E-graph O2 Instruction Reduction                     : {egraph_reduction} ({egraph_percent:.1f}%)")
    for target_triple in [t.strip() for t in args.targets.split(",") if t.strip()]:
        prefix = target_triple.replace("-", "_")
        vector_total = sum(r[f"{prefix}_vector_instrs"] for r in results)
        max_width = max(r[f"{prefix}_max_vector_width"] for r in results)
        print(f" Vector Evidence {target_triple:<28}: {vector_total} instructions, {max_width}b max")
    print("==========================================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
