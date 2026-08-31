#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import math
import statistics

BENCHMARKS_DIR = os.path.dirname(os.path.abspath(__file__))
CORPUS_C_DIR = os.path.join(BENCHMARKS_DIR, "corpus", "c")
CORPUS_FYRA_DIR = os.path.join(BENCHMARKS_DIR, "corpus", "fyra")
BUILD_DIR = os.path.join(BENCHMARKS_DIR, "..", "build")
FYRA_BIN = os.path.join(BUILD_DIR, "fyra_compiler")

def geomean(iterable):
    vals = [x for x in iterable if x > 0]
    if not vals:
        return 0.0
    return math.exp(sum(math.log(x) for x in vals) / len(vals))

def count_assembly_metrics(asm_file):
    if not os.path.exists(asm_file):
        return {"total": 0, "loads_stores": 0, "branches": 0, "moves": 0}

    total = 0
    loads_stores = 0
    branches = 0
    moves = 0

    with open(asm_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('.') or line.startswith('#') or line.endswith(':'):
                continue

            total += 1
            op = line.split()[0].lower() if line.split() else ""

            if 'mov' in op or 'push' in op or 'pop' in op or 'lea' in op:
                if '(' in line or ')' in line or 'ptr' in line:
                    loads_stores += 1
                elif 'mov' in op:
                    moves += 1
            elif op.startswith('j') or op == 'call' or op == 'ret':
                branches += 1

    return {
        "total": total,
        "loads_stores": loads_stores,
        "branches": branches,
        "moves": moves
    }

def run_cmd(cmd):
    p = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p.returncode, p.stdout, p.stderr

def measure_execution(exec_path, samples=5, warmup=1):
    if not os.path.exists(exec_path):
        return {"median": 0.0, "min": 0.0, "output": ""}

    for _ in range(warmup):
        run_cmd(exec_path)

    runtimes = []
    output = ""
    for _ in range(samples):
        t0 = time.perf_counter()
        rc, out, err = run_cmd(exec_path)
        t1 = time.perf_counter()
        if rc == 0:
            runtimes.append(t1 - t0)
            output = out.strip()

    if not runtimes:
        return {"median": 0.0, "min": 0.0, "output": ""}

    return {
        "median": statistics.median(runtimes),
        "min": min(runtimes),
        "output": output
    }

def main():
    print("==========================================================================")
    print(" Fyra Backend — Multi-Category Benchmark Harness")
    print("==========================================================================")

    if not os.path.exists(FYRA_BIN):
        print(f"Error: Fyra compiler not found at {FYRA_BIN}. Please build first.")
        sys.exit(1)

    bench_names = [f[:-2] for f in os.listdir(CORPUS_C_DIR) if f.endswith(".c")]
    bench_names.sort()

    results = []

    for bname in bench_names:
        c_src = os.path.join(CORPUS_C_DIR, f"{bname}.c")
        fyra_src = os.path.join(CORPUS_FYRA_DIR, f"{bname}.fyra")

        if not os.path.exists(fyra_src):
            print(f"Skipping {bname}: missing .fyra source.")
            continue

        out_dir = os.path.join(BENCHMARKS_DIR, "output", bname)
        os.makedirs(out_dir, exist_ok=True)

        # 1. Compile GCC -O2
        gcc_s = os.path.join(out_dir, "gcc.s")
        gcc_exec = os.path.join(out_dir, "gcc_exec")
        run_cmd(f"gcc -O2 {c_src} -S -o {gcc_s}")
        run_cmd(f"gcc -O2 {c_src} -o {gcc_exec}")

        # 2. Compile Clang -O2
        clang_s = os.path.join(out_dir, "clang.s")
        clang_exec = os.path.join(out_dir, "clang_exec")
        run_cmd(f"clang -O2 {c_src} -S -o {clang_s}")
        run_cmd(f"clang -O2 {c_src} -o {clang_exec}")

        # 3. Compile Fyra -O1 & -O2
        fyra_o1_s = os.path.join(out_dir, "fyra_o1.s")
        fyra_o2_s = os.path.join(out_dir, "fyra_o2.s")
        fyra_exec = os.path.join(out_dir, "fyra_exec")

        run_cmd(f"{FYRA_BIN} {fyra_src} -o {fyra_o1_s} -O1")
        run_cmd(f"{FYRA_BIN} {fyra_src} -o {fyra_o2_s} -O2")
        harness_c = os.path.join(BENCHMARKS_DIR, "harness.c")
        run_cmd(f"gcc -no-pie {fyra_o2_s} {harness_c} -o {fyra_exec}")

        # Measure Assembly Metrics
        gcc_metrics = count_assembly_metrics(gcc_s)
        clang_metrics = count_assembly_metrics(clang_s)
        fyra_o2_metrics = count_assembly_metrics(fyra_o2_s)

        # Measure Runtimes
        gcc_perf = measure_execution(gcc_exec)
        clang_perf = measure_execution(clang_exec)
        fyra_perf = measure_execution(fyra_exec)

        # Correctness Check
        correct = (fyra_perf["output"] == clang_perf["output"] and len(fyra_perf["output"]) > 0)

        entry = {
            "name": bname,
            "correct": correct,
            "gcc_time": gcc_perf["median"],
            "clang_time": clang_perf["median"],
            "fyra_time": fyra_perf["median"],
            "gcc_instrs": gcc_metrics["total"],
            "clang_instrs": clang_metrics["total"],
            "fyra_instrs": fyra_o2_metrics["total"],
            "gcc_mem": gcc_metrics["loads_stores"],
            "clang_mem": clang_metrics["loads_stores"],
            "fyra_mem": fyra_o2_metrics["loads_stores"],
        }
        results.append(entry)

        status = "PASSED" if correct else "FAILED"
        print(f"[{status}] {bname:<24} | Fyra: {fyra_perf['median']:.3f}s | Clang: {clang_perf['median']:.3f}s | GCC: {gcc_perf['median']:.3f}s")

    print("\n--------------------------------------------------------------------------")
    print(" Benchmark Category Summary & Normalized Geometric Means")
    print("--------------------------------------------------------------------------")

    time_ratios = [r["clang_time"] / r["fyra_time"] for r in results if r["fyra_time"] > 0]
    instr_ratios = [r["fyra_instrs"] / r["clang_instrs"] for r in results if r["clang_instrs"] > 0]
    mem_ratios = [r["fyra_mem"] / r["clang_mem"] for r in results if r["clang_mem"] > 0]

    geo_perf = geomean(time_ratios) * 100.0
    geo_instr = geomean(instr_ratios)
    geo_mem = geomean(mem_ratios)

    print(f" Geometric Mean Relative Performance (Fyra / Clang -O2) : {geo_perf:.1f}%")
    print(f" Geometric Mean Instruction Ratio    (Fyra / Clang -O2) : {geo_instr:.2f}x")
    print(f" Geometric Mean Memory Operations     (Fyra / Clang -O2) : {geo_mem:.2f}x")
    print("==========================================================================")

if __name__ == "__main__":
    main()
