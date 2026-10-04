# `aggregate_matrix` runtime-first optimization report

## Baseline and semantic equivalence

This phase started from clean branch `work` at `b4951c86c19295d2832b18e17e22bc02ed737363`. The authoritative baseline Release suite passed 79/79 and all 17 benchmark checksums agreed across GCC, Clang, Fyra O2, and vector-disabled Fyra. Hardware counters were unavailable (`perf` is not installed), so all dynamic counts below are derived from the generated loops and no counter values are fabricated.

The C and Fyra kernels do the same bounded work: 100,000 repetitions of 64 four-i32 records, followed by initialization of three 4x4 i64 matrices, 200,000 repetitions of a 4x4 multiply, and observation of three record fields plus `y[0][0]`, `y[1][2]`, and `y[3][3]`. `adjust(r,7,11)` in C is represented directly by the same field arithmetic in Fyra. All record values remain in i32 range, the checksum remains in i64 range, layouts use 16-byte records and 8-byte matrix elements, and every mode returns checksum `15007999386`.

Baseline canonical distribution (15 samples, 3 warmups) was: median 74.212 ms, min 68.944 ms, max 98.269 ms, mean 80.027 ms, standard deviation 10.363 ms, CV 12.95%. A quieter immediately preceding focused run measured 69.294 ms median, 67.382–79.968 ms, CV 5.5%; that focused distribution is paired with the focused after measurement for causality. Canonical baseline medians were Fyra 74.212 ms, scalar 74.917 ms, GCC 36.374 ms, and Clang 13.801 ms. Structural diagnostics were Fyra/GCC/Clang 287/159/71 static instructions; Fyra had 43 explicit memory operations, 13 stack-reading and 7 stack-writing operations (7 explicit assembly stack loads and 7 stores), an 80-byte frame, 64 vector instructions, and 128-bit maximum width.

## Component decomposition

Diagnostic variants changed only the unused repetition count and retained initialization/final observation. The aggregate-only variant executes one matrix multiply so `y` is initialized. Results use 15 samples and three warmups.

| Component | Fyra runtime | GCC runtime | Clang runtime | Fyra/Clang | Primary difference |
|---|---:|---:|---:|---:|---|
| aggregate operations | 31.867 ms | 25.546 ms | 16.225 ms | 1.96x | record storage remains materialized; Clang performs stronger scalar replacement/DSE |
| matrix operations | 52.137 ms | 23.233 ms | 7.356 ms | 7.09x | Fyra repeats invariant overwrite computation 200,000 times; Clang folds matrix values and leaves only an empty count loop |

Process startup prevents adding these values directly to the combined runtime, but the excess is decisive: matrix-only contributes about 44.8 ms of Fyra-over-Clang deficit versus about 15.6 ms for aggregate-only. The matrix repetition is therefore dominant.

## Pipeline and first missed opportunity

Production O2 order is: CFG/dominators/frontier/phi insertion/SSA rename/Mem2Reg; module inlining; then up to five fixed-point rounds of InstCombine, EGraph, division strength reduction, SCCP, copy elimination, GVN, CFG simplification, LICM, **IdempotentLoopCollapse**, ScalarEvolution, and DCE; finally LoopVectorizer, LSR, SLP, LoopUnroll, DCE, and instruction scheduling. Before this change, the bold pass was absent.

| Optimization stage | Redundant aggregate work | Redundant memory ops | Loop work | Address work | Notes |
|---|---|---|---|---|---|
| input | record and matrix arrays materialized | 25.6m matrix loads + 3.2m stores dynamically | 200k x 4 x 4 x 4 matrix body | about 128m matrix address operations | opportunity already semantically visible |
| Mem2Reg | unchanged | unchanged | unchanged | unchanged | explicit pointer-addressed fields are not scalar allocas |
| Inliner/InstCombine | C-style `adjust` is already direct in Fyra | unchanged | unchanged | minor canonicalization only | no whole-region reasoning |
| EGraph | arithmetic locally simplified | unchanged | unchanged | local expression choices only | no memory/loop effect reasoning |
| SCCP/GVN/CFG | constants propagated locally | repeated stores not forwarded across iterations | loop remains 200k | loop-dependent addresses remain | GVN cannot cross side-effecting nested loops |
| LICM | pure invariants may hoist | side-effecting region remains | loop remains 200k | some bases available, whole region not hoisted | correct conservative behavior |
| Idempotent collapse | unchanged aggregate portion | repeated matrix traffic reduced to one execution | **200,000 -> 1** | repeated address work reduced likewise | first pass able to prove and remove repetition |
| SCEV | recognizes ordinary induction, not region idempotence | unchanged after collapse | no closed-form for memory region | unchanged | not a scalar recurrence |
| LV | record/matrix initialization SIMD remains | matrix dot accesses rejected | dot loop stays scalar | multidimensional/strided address rejected | diagnostic: not `base + IV * element-size` |
| LSR/SLP/Unroll | no whole-region change | no whole-region change | small-loop transformations only | some local folding | cannot remove outer repetition |
| final | record materialization remains | one matrix execution | one 4x4 multiply | one multiply's address work | static body remains; dynamic executions collapse |

The missed opportunity is visible in input IR and could legally be removed as soon as SSA/CFG and local-allocation provenance are available. It remained through every old scalar pass because none reasoned that a side-effecting loop can nevertheless be idempotent when it only overwrites local storage from disjoint, unchanged local inputs.

## Aggregate and matrix forensics

Fyra represents fields and matrices as offsets from local `alloc` storage. Mem2Reg does not promote these pointer-addressed fields, and there is no first-class aggregate ABI. No whole-record data copy survives in Fyra's hot loop; the apparent `%a/%m/%x/%y = copy %*_slot` operations are pointer identities. The local storage addresses never escape to calls, returns, globals, or stored pointers. Field-sensitive escape information was not previously available as a reusable analysis.

The matrix nest is `rep[200000] -> row[4] -> col[4] -> dot[4]`. The three inner bounds are constant and their induction variables are recognized, but SCEV only eliminates supported scalar closed-form recurrences and LoopUnroll only performs conservative 2x cloning of small natural loops. LICM cannot move a region containing stores. Shared AffineAnalysis represents the simple affine pieces but conservatively rejects the complete narrow-index multidimensional expressions; x64 does fold some simple scale/displacement expressions, while the final scalar dot body still materializes row/column indices and byte offsets. The dot reduction is one serialized accumulator and is not vectorized because its two-dimensional accesses are not accepted as unit-stride `base + IV*element-size` accesses. Multiple accumulators would be legal for integer wrapping addition but would address only a secondary throughput issue.

Before optimization, the matrix nest executes 12.8 million dot iterations, 12.8 million multiplies/adds, 25.6 million loads, 3.2 million stores, roughly 128 million address calculations, and about 21.2 million loop-condition branches. The 24-instruction final dot body alone contributes about 307 million dynamic instructions; control and surrounding loops raise the hot estimate above 330 million. Clang constant-folds the matrix result, performs zero matrix arithmetic or memory traffic in the repetition, and retains an approximately 400,000-instruction decrement/branch delay loop. GCC instead vectorizes two columns/reduction work with SSE and retains the repetition, explaining its intermediate runtime.

## Ranked causes

| Rank | Root cause | Runtime evidence | Static evidence | Dynamic evidence | Selected? |
|---:|---|---|---|---|---|
| 1 | missing idempotent overwrite-loop collapse | matrix-only 52.137 vs 7.356 ms | complete nested body survives | >330m estimated instructions and 28.8m memory ops | **yes** |
| 2 | matrix reduction/address vectorization miss | GCC matrix-only 23.233 ms | scalar dot body with materialized indices | 12.8m serialized dot iterations | no |
| 3 | missing field-sensitive scalar replacement/DSE | aggregate-only 31.867 vs 16.225 ms | record array stores survive | 25.6m record stores | no |

Exactly one general optimization was implemented: collapse a positive constant-count repetition loop to one iteration when its induction has control-only uses, all memory is rooted in non-escaping local allocations, stores do not alias any loads, and no calls, allocations, returns, or trapping integer division occur in the region. It is independent of matrices, dimensions, names, x64, and benchmark structure. Shared AffineAnalysis is consulted; a separate narrow pointer-provenance walk identifies local allocation ownership without duplicating affine arithmetic.

Positive testing proves an invariant overwrite of a local allocation collapses from ten iterations to one. Negative tests retain repetition when the stored value uses the induction, when a load and store share storage (read-modify-write), or when storage is a possibly-aliasing/escaping pointer parameter. Canonical native execution supplies the end-to-end positive semantic proof.

## Causal result

| Stage | Runtime | Static instr | Memory ops | Frame | Correct |
|---|---:|---:|---:|---:|---|
| focused baseline | 69.294 ms (CV 5.5%) | 287 | 43 | 80 B | yes |
| selected transform | 26.245 ms (CV 7.6%) | 287 | 43 | 80 B | yes |
| final canonical run | 28.393 ms (CV 24.4%) | 287 | 43 | 80 B | yes |

The quiet paired result improves by 43.049 ms, or **62.13%**. Static instructions and static memory operations are unchanged, proving that static shortening was not the mechanism. Matrix-only improves from 52.137 ms to 7.337 ms (**85.93%**); aggregate-only is statistically unchanged at 31.867 vs 32.555 ms. Dynamic matrix work falls from 200,000 matrix executions to one: dot iterations 12.8m -> 64, loads 25.6m -> 128, stores 3.2m -> 16, and address calculations about 128m -> 640. Register allocation and static spill shape are unchanged.

## Full 17-workload regression

Absolute timings drifted upward during the second full run; unchanged GCC/Clang timings moved similarly for the longer apparent regressions (for example arithmetic Fyra +10.5%, GCC +10.4%, Clang +11.5%). Static output is identical for every non-target workload and the pass diagnostic confirms that no other canonical loop matched. Short/high-CV changes are therefore noise, not compiler regressions. `nested_loops` moved +7.6% with 5.5% CV, while its same-run Fyra/GCC ratio improved from 2.231 to 2.197; this is host drift rather than a generated-code change.

| Benchmark | Before | After | Delta | After CV | Correct | Status |
|---|---:|---:|---:|---:|---|---|
| aggregate_matrix | .074212 | .028393 | -61.7% | 24.4% | yes | material causal improvement |
| aliasing | .004693 | .004619 | -1.6% | 14.2% | yes | noisy |
| arithmetic | .077186 | .085318 | +10.5% | 7.8% | yes | host drift; GCC/Clang +10–11% |
| bitwise_hash | .104568 | .105179 | +0.6% | 6.7% | yes | unchanged |
| branch_state | .108975 | .115837 | +6.3% | 5.2% | yes | host drift; GCC +6.4% |
| fp_semantics | .003795 | .004082 | +7.6% | 9.2% | yes | short/noisy |
| int_widths | 2.220346 | 2.292693 | +3.3% | 2.8% | yes | baseline host-specific AVX/SSE issue; output unchanged |
| loops | .004052 | .003988 | -1.6% | 20.3% | yes | O(1), noisy |
| memory_bandwidth | .055317 | .056157 | +1.5% | 4.3% | yes | unchanged |
| mixed_fp | .023238 | .026164 | +12.6% | 9.8% | yes | short/noisy; Clang +16.9% |
| nested_loops | .188038 | .202328 | +7.6% | 5.5% | yes | host drift; ratio improved vs GCC |
| non_tail_calls | .003900 | .004092 | +4.9% | 7.1% | yes | short/noisy |
| realistic_dot_product | .004001 | .003677 | -8.1% | 13.7% | yes | short/noisy |
| reg_pressure | .087357 | .091761 | +5.0% | 5.9% | yes | ratio improved; vector speedup 1.824x |
| reg_pressure_b | .040531 | .040863 | +0.8% | 6.1% | yes | unchanged |
| simd_loop_liveness | .003811 | .004027 | +5.7% | 11.2% | yes | short/noisy; 128-bit path intact |
| tail_recursion | .004194 | .004052 | -3.4% | 12.0% | yes | noisy; TCO intact |

A repository-first caveat is mandatory: on this Broadwell host, `int_widths` was already unprofitable at the untouched baseline (2.220 s vector versus about 0.124 s scalar). Its assembly mixes legacy SSE and AVX in the hot loop, which is a host-sensitive pre-existing sentinel regression; this phase neither caused nor changed that output and did not implement a second unrelated optimization.

## Mandatory direct answers

1. `b4951c86c19295d2832b18e17e22bc02ed737363`.
2. Yes.
3. Yes, all 17 four-way checksums agreed.
4. The 200,000 repeated 4x4 multiply dominates; record initialization is secondary.
5. Yes; dimensions, counts, widths, layout, observations, and checksum match.
6. Aggregate-only Fyra median: 31.867 ms.
7. Matrix-only Fyra median: 52.137 ms.
8. Matrix multiplication/repetition.
9. SSA preparation; module inlining; five-round scalar fixed point listed above; LV, LSR, SLP, unroll, DCE, scheduler.
10. Input IR already exposes identical repeated local overwrites.
11. After CFG/SSA and local allocation provenance are established.
12. No, not pointer-addressed aggregate fields.
13. Yes, notably record storage; it is secondary.
14. Zero whole-aggregate data copies; pointer identity copies are not aggregate copies.
15. Zero whole-aggregate copies are semantically required.
16. The selected transform avoids 25,599,872 of 25.6m matrix loads.
17. It avoids 3,199,984 of 3.2m matrix stores.
18. Approximately 127,999,360 matrix address calculations.
19. No; the relevant allocations remain local, though `y` is observed after the loop.
20. No reusable field-sensitive escape analysis existed; the new pass uses conservative local-allocation provenance only.
21. Yes: all matrix bounds are constants 200000/4/4/4.
22. It understands ordinary induction but not idempotent memory regions.
23. Only through conservative 2x unrolling; it does not collapse the nest.
24. Pure values only; it correctly cannot hoist the store-containing region.
25. It represents simple pieces; complete narrow multidimensional expressions are conservatively non-affine.
26. Some simple forms are folded, but the scalar dot body still materializes indices and offsets.
27. Yes, one scalar accumulator.
28. Yes for wrapping integer arithmetic, but that is secondary and was not implemented.
29. No.
30. LV reports that matrix addresses are not accepted as `base + induction * element-size`.
31. Not applicable.
32. More than 330m derived dynamic hot instructions before; roughly 2k after.
33. Roughly 400k decrement/branch instructions in Clang's retained empty repetition loop.
34. 25.6m loads and 3.2m stores before; 128 and 16 after.
35. Zero matrix memory operations inside Clang's repeated hot loop.
36. GCC scalar-replaces initialization and vectorizes paired matrix reduction work, but retains repetition.
37. Clang constant-folds/scalar-replaces the matrix result and removes matrix memory work, leaving an empty count loop.
38. Fyra has local folding, SCCP, GVN, LICM, SCEV, LV, LSR, SLP, and unrolling, but lacked region idempotence.
39. Existing passes treat stores as side effects and have no proof that repeating the whole nested region is observationally redundant.
40. Idempotent repetition; reduction/address vectorization; aggregate scalar replacement/DSE.
41. Idempotent local-overwrite loop collapse.
42. Legality uses loop structure, local-allocation provenance, alias separation, and side effects—not names or dimensions.
43. A ten-iteration invariant overwrite of local storage must collapse to one.
44. Induction-dependent stores, same-storage read/write, and pointer-parameter stores must not collapse.
45. Focused 69.294 ms; full baseline 74.212 ms.
46. Focused 26.245 ms; final full run 28.393 ms.
47. 62.13% on the quiet paired focused runs.
48. Yes, matrix-only improved 85.93%; aggregate-only did not materially change.
49. Unchanged at 287.
50. Matrix repetitions fell 200,000 -> 1 and estimated hot instructions >330m -> about 2k.
51. Matrix loads/stores fell 25.6m/3.2m -> 128/16.
52. Static register/spill shape and 80-byte frame are unchanged.
53. No generated-code stable regression; apparent absolute changes track host/compiler timing drift or high CV.
54. No generated-code stable regression over 5%.
55. No on this host at baseline or after; this pre-existing AVX/SSE transition sentinel regression is explicitly recorded.
56. Yes, 1.824x vector/scalar speedup.
57. Yes, 42 static instructions and O(1).
58. Yes.
59. Yes.
60. Yes.
61. Yes, all 17 four-way checksums.
62. Yes, 80/80 Release tests.
63. Yes, the fresh targeted Debug suite including the new test passes.
64. Yes at supported validation/structural levels; only x64 was executed.
65. No; shared AffineAnalysis remains authoritative and provenance is not affine re-parsing.
66. Yes after the final commit.

## Outcome

The selected general optimization materially improves the dominant component and aggregate runtime while preserving correctness. No second optimization was attempted. Because the repository-first baseline reproduced a pre-existing `int_widths` profitability failure on this host, that sentinel is reported rather than hidden; it was not caused by this patch.

**Formal phase status:** `OPTIMIZATION PHASE: BLOCKED BY PRE-EXISTING int_widths LEGACY-SSE/AVX TRANSITION DEFICIT ON THE AUTHORITATIVE HOST.` The aggregate objective itself is causally achieved, but the user's success criterion also requires `int_widths` to remain profitable. It was already 0.056x vector/scalar in the untouched baseline and 0.058x after, so this report does not falsely declare the entire phase successful. Repairing that separate backend issue would be a second optimization and is intentionally deferred.
