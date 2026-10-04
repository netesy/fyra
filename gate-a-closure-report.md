# Gate A correctness-closure report

## Baseline and scope

The phase started on branch `work` at `16640e26fabde9c4ef17cb33d374e0a2c432d741` with a clean worktree. The checkout, rather than hashes from another environment, was treated as authoritative. The pre-change Release suite passed 79/79, while the standalone reproducers produced alias checksum `1346105651290635210` versus reference `5565956349991118218`, and mixed-FP checksum `1783293664` versus reference `1000000`. No performance optimization was attempted.

## Alias forensics

The minimized loop was `dst[i] = src[i] + 1` over i64 elements. Shared `AffineAnalysis` describes its load as `(src, i, offset 0, width 8, read)` and store as `(dst, i, offset 0, width 8, write)`. For start `s` and bound `n`, each range is `[base+s*8, base+n*8)`. The existing unsigned distance guard correctly selects vector execution only for disjoint ranges; unrepresentable ranges and nonzero offsets remain conservative. Exact alias is safe for this operation, but conservatively falls back because no operation-specific proof exists.

The last correct representation was the scalar pre-LV IR. The first incorrect representation was post-LV: the transformer committed the versioned CFG, then failed late to vectorize the store expression because scalar constant `1` was not broadcast. The vector path therefore lacked the scalar store's semantics. The repair preflights store-expression support before mutation and broadcasts constants only within store data expressions (never load-address expressions). This fixes transformation completeness; it does not weaken alias legality or globally disable pointer-loop vectorization.

| Alias case | Scalar correct | Old vector correct | New vector correct | Vector path | Fallback | Exact root cause |
|---|---|---|---|---|---|---|
| no alias | yes | no | yes | yes | no | late unsupported scalar constant in store value |
| exact alias | yes | yes | yes | no | yes | conservatively versioned |
| forward +1 | yes | no | yes | no | yes | old malformed vector path; overlap dependence |
| backward +1 | yes | no | yes | no | yes | old malformed vector path; overlap dependence |
| forward multi | yes | no | yes | no | yes | old malformed vector path; overlap dependence |
| backward multi | yes | no | yes | no | yes | old malformed vector path; overlap dependence |
| runtime unknown/no overlap | yes | no | yes | yes | no | runtime guard selects disjoint vector path |
| runtime unknown/overlap | yes | no | yes | no | yes | runtime guard selects scalar path |

The execution regression covers 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, and 65 elements; exact, ±1, and ±3 overlaps; full-memory comparison; guard sentinels; and explicit path counters. Unsafe overlap executes the scalar fallback, disjoint inputs execute vector main, and all sentinels remain intact.

## Mixed integer/FP forensics

The failing loop carried an induction, an FP sum, and an integer count whose update observed the FP prefix sum. The old LV chose a single i32 reduction and ignored the second recurrence/prefix observation. This is not a legal associative reduction, regardless of final-value coincidence. The general repair rejects multiple non-induction recurrences and reduction updates observed within the loop, before CFG mutation. Independent supported f32/f64 array arithmetic remains vectorized and execution-tested.

| Stage | Correct? | Representation | Evidence |
|---|---|---|---|
| scalar source/IR | yes | f32/f64 prefix sum plus i32 count | scalar reference agrees |
| pre-LV | yes | two non-induction PHIs | printed IR snapshot |
| old post-LV | **no** | one vector accumulator; other recurrence ignored | first incorrect transition |
| new post-LV | yes | unchanged scalar IR | transform returns false; byte-identical print |
| post-LSR | yes | scalar | four-way execution |
| post-SLP | yes | scalar | four-way execution |
| post-unroll | yes | scalar | four-way execution |
| pre-RegAlloc | yes | scalar typed f32/f64 and i32 | legal assembly inputs |
| post-RegAlloc | yes | GPR/XMM classes separated | assertions and execution |
| assembly | yes | scalar `addss/addsd`, compares and conversions | assembles |
| execution | yes | checksum 2000000 | GCC/Clang/Fyra/scalar agree |

The first-error transition was pre-LV to old post-LV. It was recurrence/reduction legality, not lane typing, conversion, arithmetic, comparison-mask lowering, epilogue state, or backend lowering. Strict exceptional semantics are retained by scalar fallback for this loop; dedicated scalar tests cover NaN, signed zero, and infinities.

## Scalar FP spilling

The x64 target owns 16 architectural XMM registers and reserves XMM15 (physical id 115), leaving 15 allocatable. The allocator now filters eviction candidates by register class, asserts that no FP/vector value receives a GPR or XMM15, and uses a fresh slot when retroactively spilling an already-defined interval. Reusing a slot freed at the current scan position was incorrect because that slot can overlap the earlier part of the retroactively spilled interval.

| Type | Allocatable FP regs | Peak live FP virtuals | Spill decisions | Stack slots | Stores | Reloads | Correct |
|---|---:|---:|---:|---:|---:|---:|---|
| f32 | 15 | 20 | 6 | 6 | 6 `movss` | 6 `movss` | yes |
| f64 | 15 | 20 | 6 | 6 | 6 `movsd` | 6 `movsd` | yes |

Each test keeps 20 converted values live, spills live values, reloads them for a subsequent sum, and validates the observable result. Simultaneous integer arguments/temporaries exercise mixed GPR/XMM pressure. XMM15 is excluded from allocator candidates and is used only by width-correct backend spill bridges.

## Canonical benchmark results

Timing scope is `process_with_internal_kernel_loop`; static counts are structural diagnostics, not runtime proxies. Linux `perf` was absent, so `hardware_counters_available=false` and no counters or derived values were fabricated.

| Benchmark | Fyra | Scalar | GCC | Clang | Fyra/GCC | Fyra/Clang | Vector speedup | CV | Fyra static | Clang static | Correct |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| aggregate_matrix | .061490 | .061782 | .033182 | .010132 | 1.853 | 6.069 | 1.005 | 1.5% | 287 | 71 | yes |
| aliasing | .003034 | .002899 | .002729 | .002796 | 1.112 | 1.085 | .955 | 7.4% | 335 | 447 | yes |
| arithmetic | .059588 | .060037 | .034546 | .033961 | 1.725 | 1.755 | 1.008 | 1.4% | 279 | 189 | yes |
| bitwise_hash | .089119 | .090082 | .051711 | .058089 | 1.723 | 1.534 | 1.011 | 10.4% | 61 | 57 | yes |
| branch_state | .092438 | .092381 | .041443 | .041140 | 2.230 | 2.247 | .999 | 13.3% | 76 | 41 | yes |
| fp_semantics | .002720 | .002739 | .002775 | .002747 | .980 | .990 | 1.007 | 8.0% | 82 | 36 | yes |
| int_widths | .046448 | .108583 | .072799 | .064519 | .638 | .720 | 2.338 | 2.2% | 170 | 91 | yes |
| loops | .002803 | .002707 | .002692 | .002697 | 1.041 | 1.039 | .966 | 16.5% | 42 | 21 | yes |
| memory_bandwidth | .046273 | .045347 | .027739 | .024225 | 1.668 | 1.910 | .980 | 3.5% | 194 | 192 | yes |
| mixed_fp | .022415 | .022549 | .005763 | .005640 | 3.889 | 3.975 | 1.006 | 3.2% | 121 | 69 | yes |
| nested_loops | .126213 | .127796 | .076914 | .048870 | 1.641 | 2.583 | 1.013 | 1.6% | 49 | 57 | yes |
| non_tail_calls | .002732 | .002849 | .002651 | .002601 | 1.030 | 1.050 | 1.043 | 9.5% | 89 | 44 | yes |
| realistic_dot_product | .002679 | .002694 | .003143 | .002753 | .852 | .973 | 1.006 | 8.9% | 48 | 30 | yes |
| reg_pressure | .072771 | .126925 | .074032 | .072416 | .983 | 1.005 | 1.744 | 10.0% | 173 | 95 | yes |
| reg_pressure_b | .034230 | .034126 | .021500 | .025735 | 1.592 | 1.330 | .997 | 1.1% | 71 | 64 | yes |
| simd_loop_liveness | .002694 | .002728 | .002658 | .002613 | 1.013 | 1.031 | 1.013 | 6.5% | 49 | 29 | yes |
| tail_recursion | .002818 | .002838 | .027404 | .002740 | .103 | 1.028 | 1.007 | 10.1% | 31 | 43 | yes |

Performance-shape classification: A—`aggregate_matrix`, `arithmetic`, `branch_state`, `mixed_fp`, `reg_pressure_b`; B—`nested_loops`, `memory_bandwidth`; C—`int_widths`, `reg_pressure`, `realistic_dot_product`; D (short/noisy)—`aliasing`, `bitwise_hash`, `fp_semantics`, `loops`, `non_tail_calls`, `simd_loop_liveness`, `tail_recursion`. This is descriptive only. The largest measured deficit is `aggregate_matrix` at 6.069× Clang; no causal optimization diagnosis was made.

## Coverage and protected milestones

There are 17 canonical workloads. Aliasing and mixed integer/FP are canonical and four-way correct. Forced f32/f64 spills are an explicit correctness-gate regression rather than a timing workload. Aggregate memory/data layout is PASS; first-class aggregate parameter/return ABI is N/A because the current IR exposes scalar parameters/returns and pointer-addressed aggregate storage, not first-class aggregate ABI values. `simd_loop_liveness` demonstrates 128-bit SIMD; `int_widths` demonstrates 256-bit SIMD. `loops` remains O(1) and 42 instructions; `arithmetic` remains 279 instructions with its 48-byte frame; `int_widths` remains 2.338× faster than scalar; `reg_pressure` remains 1.744× faster than scalar; TCO, helper emission, FP exceptional semantics, shared AffineAnalysis, and x64 memory folding remain covered.

Against the immediately preceding same-machine run, no protected stable workload regressed over 3%; observed changes were below that threshold or noisy. Consequently none regressed over 5%.

Cross-target structural tests pass for x64, AArch64, RISC-V and Wasm at their supported compile/validation levels. The new checks live in shared legality and conservatively retain scalar IR; no private affine/dependence parser was added.

## Mandatory direct answers

1. `16640e26fabde9c4ef17cb33d374e0a2c432d741`.
2. Yes.
3. `benchmarks/reproducers/aliasing.fyra`, minimized to `dst[i]=src[i]+1`.
4. The old vector path failed disjoint/no-alias and partial-overlap executions; scalar/exact-fallback behavior was correct.
5. Scalar pre-LV IR.
6. The committed post-LV versioned CFG.
7. The store value `load + 1` was not vectorized after CFG mutation because `1` was not broadcast.
8. Yes; it conservatively required runtime versioning.
9. Yes; the guard correctly separates disjoint and overlap cases.
10. Yes: complete half-open byte intervals are used; unsupported offsets remain scalar.
11. Yes; representability and unsigned-distance checks reject unsafe ranges.
12. Yes for this element-local operation, though conservatively sent to fallback.
13. Yes, by scalar fallback.
14. Yes, by scalar fallback.
15. Yes.
16. Yes.
17. Yes.
18. `benchmarks/reproducers/mixed_fp.fyra`, carrying FP sum plus integer count with a prefix comparison.
19. Pre-LV.
20. Old post-LV.
21. The single-reduction transform discarded/invalidly modeled the second recurrence and prefix-observed update.
22. No.
23. No.
24. No.
25. No.
26. Yes: legality of multiple/prefix-observed reductions.
27. No.
28. No.
29. Yes, via correct scalar fallback for this unsupported recurrence and vector execution for supported loops.
30. Yes, same qualification.
31. Yes.
32. Yes.
33. Yes.
34. Yes, across the recorded boundary matrix.
35. 15.
36. XMM15, physical id 115.
37. No; candidate construction excludes it and assertions enforce the invariant.
38. 20 simultaneously live source FP virtuals.
39. Six for f32 and six for f64.
40. Six for f32 and six for f64.
41. Yes.
42. Yes.
43. Yes.
44. 17.
45. Yes.
46. Yes.
47. Yes, as a correctness regression.
48. Aggregate memory/data layout PASS; first-class aggregate ABI N/A.
49. Yes.
50. Yes, all 17.
51. Yes, 2.338× versus scalar.
52. Yes, 1.744× versus scalar.
53. Yes, 42-instruction O(1) form.
54. Yes, 279 instructions and 48-byte frame.
55. Yes.
56. Yes.
57. No stable protected regression over 3%.
58. No.
59. No; `perf` was unavailable.
60. Not applicable; no values were fabricated.
61. Yes, 79/79.
62. Yes, the fresh targeted Debug suite passes.
63. Yes, structurally/compile validated at supported target levels; this is not claimed as execution on non-host targets.
64. No; shared `AffineAnalysis` remains authoritative.
65. Yes after the final commit.
66. Yes: **GATE A: PASS**.

No performance investigation was started. The next phase may select a target from runtime and dynamic evidence.
