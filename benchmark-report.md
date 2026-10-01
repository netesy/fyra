# Fyra Backend Benchmark Report

This report records the latest successful run of the canonical `python3 benchmarks/run_suite.py` harness. The machine-readable source of truth is `benchmarks/benchmark_results.json` (also exported as CSV). The harness refuses to update either file unless every native checksum matches the GCC and Clang oracles.

## Method

- GCC, Clang, and Fyra are compiled at `-O2` and linked statically.
- Fyra loop unrolling is held disabled in this harness so the SLP-on, SLP-off, and e-graph comparison arms measure identical loop bodies; loop unrolling has dedicated semantic regression tests.
- The canonical run uses two warmups followed by fifteen measured samples.
- The e-graph comparison compiles an additional `-O2 --disable-egraph` artifact and reports its instruction-count difference from normal `-O2`.
- Cross-target vector evidence is derived from emitted assembly. Native runtime timing and checksum validation are performed for x86-64 only.

## Current Results

| Benchmark | Correct | Fyra median (s) | SLP speedup | Fyra instructions | E-graph instruction reduction |
|---|---:|---:|---:|---:|---:|
| arithmetic | true | 0.075034 | 1.046x | 289 | 18 |
| int_widths | true | 0.104929 | 1.007x | 77 | 0 |
| loops | true | 0.128688 | 0.999x | 74 | 128 |
| realistic_dot_product | true | 0.002902 | 1.018x | 45 | 0 |
| reg_pressure | true | 0.093504 | 1.009x | 301 | 0 |
| simd_loop_liveness | true | 0.002772 | 1.245x | 48 | 2 |
| tail_recursion | true | 0.002850 | 0.991x | 29 | 0 |

Across the corpus, normal O2 emits 863 analyzed instructions versus 1,011 with the e-graph disabled: a reduction of 148 instructions (14.6%). A zero means the e-graph found no profitable rewrite for that workload, not that the pass was disconnected.

## Cross-target vector evidence

| Target | Vector instructions observed | Maximum observed width |
|---|---:|---:|
| x86-64 | 174 | 128 bits |
| AArch64 NEON | 19 | 128 bits |
| RISC-V 64 V | 46 | 128 bits |
| WASM32 SIMD | 0 | 0 bits |

The `reg_pressure` SLP workload provides non-x86 evidence in this run: 19 AArch64 NEON-form instructions and 46 RISC-V V instructions. No WASM SIMD instructions were observed, so this report does **not** claim WASM auto-vectorization evidence.

## Aggregate metrics

- Geometric mean relative performance versus Clang O2: **50.3%**.
- Geometric mean instruction ratio versus Clang O2: **1.58x**.
- Geometric mean memory-operation ratio versus Clang O2: **1.14x**.
- Correctness: **7/7 benchmarks passed**.
