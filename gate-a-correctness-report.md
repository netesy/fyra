> **Historical report (superseded).** This records the previous blocked phase. The completed closure and authoritative final status are in [`gate-a-closure-report.md`](gate-a-closure-report.md).

# Gate A correctness repair report

## Repository baseline

The phase started on branch `work` at
`90c1bb131dcb158177a5cd2b8dcee643f70f1dfe` with a clean index. The local
object database does not contain `46102eb4ebfff058e34d5dd51891ef3becd97971`,
`fa737cb2b3ae6e742c63f59ddbbe4c83a3aa99d4`, or
`e4d9e933bd6e2ab38b1b91a85f09e2f042e685f9`; consequently no ancestry claim
between those three hashes can be proven from this checkout. Reflog evidence
shows that this workspace's `work` branch was created from `FETCH_HEAD`
(`90c1bb1`, the `dev` branch fetched from `https://github.com/netesy/fyra`) after
the old local `work` branch at `086f410` was renamed. This explains the actual
starting HEAD, but the unavailable objects do not provide evidence for why an
earlier workspace started at `fa737cb...` instead of `46102eb...`.

## Function-definition failure

The smallest failure was:

```fyra
function $helper(%x:i32):i32 { @entry %r = add %x,1:i32 ret %r:i32 }
export function $main():i32 { @entry %r = call $helper(41):i32 ret 0:i32 }
```

Whitespace was decisive: `%x : i32` worked while `%x:i32` did not. The lexer
classified an adjacent `:i32` as one named-type token. The function parser did
not see the expected colon, used the default parameter type, then abandoned the
function at the unexpected token. The module retained a `Function` object with
zero basic blocks, calls resolved to that object, and CodeGen correctly skipped
the bodyless object. This was a parser/location-identity defect, not inlining,
DCE, reachability, symbol spelling, object writing, or linking.

| Stage | Call present | Definition present | Symbol present |
|---|---:|---:|---:|
| initial IR | yes | **no body** | Function object named `helper` |
| optimized IR | yes | **no body** | Function object named `helper` |
| pre-CodeGen | yes | **no body** | Function object named `helper` |
| assembly | yes | no | call reference only |
| object | yes (relocation) | no | undefined `helper` |
| final artifact | link fails | no | unresolved `helper` |

The repair makes colon punctuation independent of following whitespace, adapts
named-type parsing, and validates that a module-local `Call` target has a body.
Compact mixed-width definitions, transitive calls, multiple callers, non-tail
calls, and optimized calls now execute. Fyra has no module dead-function pass:
CodeGen enumerates every nonempty module function, so removal of genuinely
unreachable helpers is not an optimization currently present in this semantic
model.

## x64 scalar floating point

The smallest failure was `fadd d_1.5,d_2.25:f64`. `ConstantFP` was formatted as
an integer immediate, producing `movsd $bits,%xmm15`; add/sub/mul/div and compare
then emitted the same illegal immediate class. Independent live FP values were
also corrupted because compare and integer-to-FP conversion used allocated
`xmm0` as an undeclared scratch register. FP parameter lookup used the integer
argument bank and calls/returns assumed `rax` even for FP results.

The repair interns exact f32/f64 IEEE payloads in read-only constant-pool memory,
keeps scalar FP copies in XMM/memory locations, breaks memory-to-memory copies
through the reserved XMM scratch, uses that same reserved register for
arithmetic/comparison/conversion temporaries, and uses the existing target FP
argument/return ABI. Ordered comparisons explicitly mask parity (unordered),
while `!=` includes unordered.

| Operation | IR types | Allocated locations | Old invalid/unsafe form | New legal form | Execution proven |
|---|---|---|---|---|---:|
| move | f32/f64 | XMM, stack, constant pool | integer `movl/movq`; immediate `movss/movsd` | `movss/movsd`, reserved-XMM bridge for mem/mem | yes |
| load | f32/f64 | memory to XMM | constants represented as immediates | RIP-relative pool to XMM | constant loads: yes; general data load: not newly proven |
| store | f32/f64 | XMM to memory | generic integer copy path possible | width-correct scalar FP move | not newly execution-proven |
| add | f32/f64 | XMM plus XMM/memory | immediate source | `addss/addsd` memory/register source | yes |
| sub | f32/f64 | XMM plus XMM/memory | immediate source | `subss/subsd` memory/register source | yes |
| mul | f32/f64 | XMM plus XMM/memory | immediate source | `mulss/mulsd` memory/register source | yes |
| compare | f32/f64 -> i32 | reserved XMM plus XMM/memory | immediate source; live `xmm0` clobber; unordered flags wrong | `ucomiss/ucomisd` plus parity-aware result | yes |

Execution regressions cover f32/f64 add/sub/mul, finite and NaN comparisons,
signed zero, positive/negative infinity, mixed integer/FP allocation, local
calls, and assembly/link/run behavior. Scalar FP stack locations use the same
typed move path. A dedicated forced scalar-FP spill test is not yet present, so
that item is not claimed complete.

## Gate A corpus

Three passing canonical workloads were added: `non_tail_calls`, `fp_semantics`,
and `reg_pressure_b`. Existing production output establishes the 256-bit path
with `int_widths` and the 128-bit path with `simd_loop_liveness`. First-class
aggregate parameters/returns are not represented by the current IR ABI;
aggregate memory/data-layout and matrix behavior remain covered by
`aggregate_matrix`.

| Category | Benchmark | Exercised features | Status |
|---|---|---|---|
| integer arithmetic | arithmetic | mixed scalar operations | Yes |
| widths/extensions | int_widths | signed/unsigned widening | Yes |
| hashing/bit manipulation | bitwise_hash | shifts/xor/multiply | Yes |
| branch/state machine | branch_state | data-dependent state | Yes |
| closed-form loops | loops | SCEV O(1) result | Yes |
| nested loops | nested_loops | runtime nested induction | Yes |
| aliasing | reproducer only | five guarded alias shapes | **Blocked: vector result mismatch** |
| memory bandwidth | memory_bandwidth | resident/streaming copy/add/triad | Yes |
| aggregates/data layout | aggregate_matrix | records and array layout | Yes |
| matrix | aggregate_matrix | repeated runtime-dependent 4x4 | Yes |
| non-tail calls | non_tail_calls | mixed widths, two sites, nested helper | Yes |
| GPR pressure A | reg_pressure | long live ranges | Yes |
| GPR pressure B | reg_pressure_b | loop-carried accumulators and bursts | Yes |
| SIMD pressure | simd_loop_liveness | register-native liveness | Yes |
| mixed integer/FP | reproducer only | integer induction/conversion plus FP reduction/compare | **Blocked: vector result mismatch** |
| FP reduction | fp_semantics | chained scalar FP operations | Yes |
| FP exceptional semantics | fp_semantics | NaN, signed zero, infinities | Yes |
| 128-bit vector path | simd_loop_liveness | XMM production instructions | Yes |
| 256-bit vector path | int_widths | YMM production instructions | Yes |

The guarded alias and mixed integer/FP workloads are retained under
`benchmarks/reproducers/`. Their vector-enabled checksums differ from GCC,
Clang, and Fyra vector-disabled output. Repairing loop-versioning overlap semantics and the mixed-FP vector path are new compiler
correctness tasks and was not safe to fold into these two requested repairs.
Therefore **GATE A: BLOCKED** and Gate B / nested-loop optimization did not
start.

## Final canonical runtime table (15 samples, 3 warmups)

Times are seconds. CV is Fyra's sample coefficient of variation.

| Benchmark | Fyra | Scalar | GCC | Clang | Fyra/GCC | Fyra/Clang | Vector speedup | CV | Correct |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| aggregate_matrix | 0.054442 | 0.053854 | 0.026244 | 0.012088 | 2.074 | 4.504 | 0.989 | 2.7% | Yes |
| arithmetic | 0.062923 | 0.063482 | 0.041026 | 0.038181 | 1.534 | 1.648 | 1.009 | 1.9% | Yes |
| bitwise_hash | 0.097112 | 0.096476 | 0.063072 | 0.069040 | 1.540 | 1.407 | 0.993 | 6.2% | Yes |
| branch_state | 0.091003 | 0.092082 | 0.051431 | 0.050677 | 1.769 | 1.796 | 1.012 | 1.3% | Yes |
| fp_semantics | 0.002677 | 0.002871 | 0.002791 | 0.002686 | 0.959 | 0.997 | 1.073 | 18.0% | Yes |
| int_widths | 0.054707 | 0.103378 | 0.070349 | 0.080897 | 0.778 | 0.676 | 1.890 | 12.8% | Yes |
| loops | 0.002780 | 0.002690 | 0.002782 | 0.002680 | 0.999 | 1.037 | 0.968 | 17.8% | Yes |
| memory_bandwidth | 0.044097 | 0.044383 | 0.028565 | 0.025422 | 1.544 | 1.735 | 1.006 | 8.3% | Yes |
| nested_loops | 0.148166 | 0.146584 | 0.079819 | 0.059769 | 1.856 | 2.479 | 0.989 | 6.1% | Yes |
| non_tail_calls | 0.003055 | 0.002974 | 0.002862 | 0.002953 | 1.068 | 1.035 | 0.973 | 30.1% | Yes |
| realistic_dot_product | 0.003911 | 0.003419 | 0.003530 | 0.003315 | 1.108 | 1.180 | 0.874 | 16.7% | Yes |
| reg_pressure | 0.080411 | 0.132600 | 0.087642 | 0.087542 | 0.917 | 0.919 | 1.649 | 2.0% | Yes |
| reg_pressure_b | 0.033169 | 0.033085 | 0.023330 | 0.029891 | 1.422 | 1.110 | 0.997 | 3.1% | Yes |
| simd_loop_liveness | 0.003069 | 0.003067 | 0.002715 | 0.002724 | 1.130 | 1.127 | 0.999 | 17.7% | Yes |
| tail_recursion | 0.002708 | 0.002627 | 0.032567 | 0.002620 | 0.083 | 1.034 | 0.970 | 6.5% | Yes |

The largest measured canonical deficit is `aggregate_matrix` at 4.504x Clang.
No causal optimization diagnosis was attempted; it remains a sentinel. The old
milestones remain structurally intact: `loops` is 42 instructions/O(1),
`arithmetic` is 279 instructions with its 48-byte frame, `int_widths` retains a
1.890x vector speedup, and `reg_pressure` retains a 1.649x vector speedup.

## Repaired function-emission stage table

| Stage | Call present | Definition present | Symbol present |
|---|---:|---:|---:|
| initial IR | yes | yes | local `Function` identity |
| optimized IR | yes for deliberately non-inline workload | yes | same identity |
| pre-CodeGen | yes | yes | same identity |
| assembly | yes | yes, once | global definition and call target agree |
| object | relocation | yes, once | defined `T helper` |
| final artifact | resolved call | yes | executable runs checksum 5711 |

## Direct answers

1. Start HEAD: `90c1bb131dcb158177a5cd2b8dcee643f70f1dfe`.
2. The earlier `fa737cb...` versus `46102eb...` difference cannot be proven: neither object exists locally. The current reflog only proves replacement from fetched `90c1bb1`.
3. Smallest helper reproducer: one unspaced `%x:i32` parameter, one add/return, and one caller.
4. The body was lost during parsing; a bodyless Function object remained from initial IR onward.
5. Root owner: lexer/parser, not inlining, DCE, reachability, CodeGen enumeration, symbols, or linker.
6. Violated invariant: a module-local call target existed without exactly one body/definition.
7. Repair: whitespace-independent colon lexing, compatible named-type parsing, and module validation of local call definitions.
8. Unreachable-helper removal is N/A: no module dead-function elimination exists; nonempty module functions are emitted.
9. Transitive helper calls execute (explicit O0 call-chain regression).
10. Multiple callers/sites to one helper execute.
11. Mixed-width helper arguments execute.
12. Smallest FP reproducer: `fadd d_1.5,d_2.25:f64`.
13. Immediate FP move/add/sub/mul/div/compare forms failed; generic FP copy and hidden-scratch forms were also unsafe.
14. SSE scalar instructions do not accept integer immediate FP payloads; generic integer moves used the wrong register class; allocated `xmm0` was clobbered as scratch.
15. Root owners: location representation of constants plus x64 CodeGen scratch/copy/ABI lowering; scalar regalloc already selected XMM IDs.
16. f32/f64 allocated values use XMM class; tests prove both widths.
17. Constant-pool loads are correct; a new general data stack load/store execution test is still missing.
18. Scalar FP memory-to-memory copies are bridged through the reserved XMM register.
19. FP constants are exact-width read-only pool entries and legal RIP-relative operands.
20. f32/f64 add/sub/mul are execution-proven.
21. Finite comparisons are execution-proven.
22. NaN comparisons are parity-aware and execution-proven (`!=` true; ordered relations/equality false).
23. +0/-0 comparison is execution-proven; bit-preserving move/select coverage is incomplete.
24. +/-infinity arithmetic/comparison is execution-proven.
25. Forced FP spills/reloads are not yet execution-proven.
26. Mixed live GPR/XMM scalar work executes in the correctness regression; the canonical mixed-FP vector workload still mismatches.
27. Existing SysV FP parameters/returns use XMM argument/return registers; a retained O0 mixed integer/FP helper call is execution-proven.
28. There are 15 canonical benchmarks.
29. Aliasing is not fully covered by a passing canonical workload; the complete guarded reproducer exposes a mismatch.
30. Memory bandwidth covers resident/streaming copy/add/triad and remains checksum-correct.
31. Aggregate memory/data-layout is covered.
32. First-class aggregate ABI is N/A under the current IR/ABI model.
33. Mixed integer/FP has a complete reproducer but is blocked by a vector-enabled checksum mismatch.
34. FP exceptional semantics are covered by a passing canonical sentinel.
35. Non-tail mixed-width calls are covered and remain in final assembly.
36. A materially different second GPR-pressure workload is covered.
37. 128-bit (`simd_loop_liveness`) and 256-bit (`int_widths`) production paths are independently demonstrated.
38. All 15 canonical workloads have matching checksums.
39. `int_widths` remains profitable at 1.890x, though its 12.8% CV makes the precise ratio provisional.
40. `loops` remains 42 instructions and O(1).
41. `reg_pressure` remains profitable at 1.649x with 2.0% CV.
42. `arithmetic` remains 279 instructions with a 48-byte frame.
43. The full Release suite passes.
44. The requested fresh Debug test set passes.
45. Aliasing and mixed integer/FP vector correctness still block Gate A; forced FP-spill and full FP-call coverage also remain incomplete.
46. No: **GATE A: BLOCKED**.
47. Largest measured passing-corpus deficit: `aggregate_matrix`, 4.504x Clang.
48. No causal optimization diagnosis was made, and no performance audit began.
