# Loop Unroll Regression Forensics & Forensic Report

## Executive Summary

This forensic investigation audited the performance and code-generation behavior of `arithmetic.fyra` following the implementation and integration of `LoopUnrollPass`. While all 34/34 CTests pass and 100% numerical correctness/checksum parity (`5000008149999648`) is preserved across all benchmark targets, `arithmetic.fyra` runtime remained at ~0.865s (vs Clang ~0.706s), failing to achieve the expected ~0.736s target.

Detailed disassembly of `build/arithmetic_fyra.s` revealed that **`LoopUnrollPass` did not unroll `arithmetic.fyra` at all**. The benchmark binary remained in 1x scalar loop form. A pipeline trace proved that the unrolling eligibility check `isLegalToUnroll` rejected `arithmetic.fyra` because `liveVregCount > 12`. `liveVregCount` was computed by simply counting the total number of typed IR instructions in the basic block (which reached 28 following `DivisionStrengthReduction` expansion), rather than analyzing actual concurrent live ranges.

---

## Contract Verification Table

| Contract Criteria | Status | Forensic Evidence / Details |
| :--- | :---: | :--- |
| **LoopUnrollPass Implementation & -O2 Integration** | **PASS** | `LoopUnrollPass` implemented in `include/transforms/LoopUnroll.h` & `src/transforms/LoopUnroll.cpp` and registered in `src/main.cpp`. |
| **100% Output Parity & Correctness** | **PASS** | All benchmark targets pass checksum checks (`5000008149999648` exact match for `arithmetic`). |
| **No Regression on Non-Target Benchmarks** | **PASS** | `loops`, `reg_pressure`, `tail_recursion`, `realistic_dot_product`, `int_widths` maintained baseline performance. |
| **34/34 CTests Passing** | **PASS** | `cd build && ctest --output-on-failure` passes 34/34 tests in 0.23s. |
| **`arithmetic` Gap Closed to Clang Level (~0.71s)** | **FAIL** | `arithmetic.fyra` ran at ~0.865s instead of ~0.736s because `LoopUnrollPass` rejected unrolling `arithmetic.fyra`. |

---

## Pipeline Stage & First Point of Divergence

- **Pipeline Sequence**:
  `DivisionStrengthReduction` $\to$ `SCCP` $\to$ `CopyElimination` $\to$ `GVN` $\to$ `CFGSimplifier` $\to$ `LICM` $\to$ **`LoopUnrollPass`** $\to$ `SCEV` $\to$ `DCE` $\to$ `X64Lowering`
- **First Point of Divergence**:
  `LoopUnrollPass::isLegalToUnroll`.
- **Divergence Details**:
  During `DivisionStrengthReduction`, each modulo operation in `arithmetic.fyra` was replaced with reciprocal multiplication (`Smulh`), shift, and subtraction operations. This increased the IR instruction count in `@body` to 28 instructions. When `LoopUnrollPass::isLegalToUnroll` evaluated the loop, it performed:
  ```cpp
  for (ir::BasicBlock* bb : loop.blocks) {
      for (auto& instPtr : bb->getInstructions()) {
          if (inst->getType() && !inst->getType()->isVoidTy()) {
              liveVregCount++;
          }
      }
  }
  if (liveVregCount > 12) return false; // REJECTED: 28 > 12
  ```
  Consequently, `LoopUnrollPass` bailed out, producing 1x scalar code instead of unrolled code.

---

## Root Cause Classification

**`IMPLEMENTATION FAILURE`**

### Rationale:
The compiler pass (`LoopUnrollPass`) contains a flawed heuristic (`liveVregCount > 12`) that treats static instruction count as virtual register live pressure. As a result, valid candidate loops expanded by preceding optimization passes (such as `DivisionStrengthReduction`) are incorrectly rejected, preventing the compiler from unrolling `arithmetic.fyra`. Controlled assembly modification (`/tmp/var_e.s`) previously proved that applying 2x loop unrolling to Fyra's generated assembly successfully reduces execution time from 0.865s down to 0.736s.

---

## Assembly & Performance Gap Explanation

### Fyra Generated Assembly (`build/arithmetic_fyra.s`):
```assembly
.LBB0_body:
	movslq	%ebx, %rax
	imulq	$1374389535, %rax, %rax
	sarq	$32, %rax
	sarq	$5, %eax
	movl	%ebx, %ecx
	sarl	$31, %ecx
	subl	%ecx, %eax
	imull	$100, %eax, %eax
	movl	%ebx, %ecx
	subl	%eax, %ecx
	addl	%ecx, %edx
	... [1x loop body repeated for 7 modulos] ...
	addl	$1, %ebx
	cmpl	$100000000, %ebx
	jl	.LBB0_body
```
- Total static instructions in hot loop: 77
- Hardware `idiv` instructions: 0
- Loop iteration count: $100,000,000$ (1x)
- Latency per iteration: ~21 cycles (serialized dependency chain)
- Measured execution time: **0.865s**

### Clang Generated Assembly (`/tmp/arithmetic_clang.s`):
```assembly
.LBB0_2:
	# Iteration 1
	movslq	%ebx, %rax
	imulq	$1374389535, %rax, %rcx
	...
	# Interleaved Iteration 2
	leal	1(%rbx), %eax
	movslq	%eax, %rsi
	imulq	$1374389535, %rsi, %rdi
	...
	addl	$2, %ebx
	cmpl	$100000000, %ebx
	jne	.LBB0_2
```
- Total static instructions in hot loop: 154
- Hardware `idiv` instructions: 0
- Loop iteration count: $50,000,000$ (2x unrolled)
- Instruction-Level Parallelism (ILP): 2x (interleaved independent execution pipelines hide 3-cycle `imulq` latency)
- Measured execution time: **0.706s**

### Fyra Manual 2x Unrolled Reference (`/tmp/var_e.s`):
- Unrolls hot loop 2x, interleaving instructions between iteration $i$ and $i+1$.
- Measured execution time: **0.736s** (closing 84% of the remaining gap to Clang).

---

## Recommended Next Step

**Single Recommendation**:
Refactor `LoopUnrollPass::isLegalToUnroll` in `src/transforms/LoopUnroll.cpp` to replace the naive static instruction count check (`liveVregCount > 12`) with an accurate instruction/register heuristic or relax the instruction threshold (e.g., `bodyInstCount <= 150` and max live SSA values across basic block boundaries $\le 16$). This will allow `LoopUnrollPass` to successfully unroll `arithmetic.fyra`, achieving the target ~0.736s runtime while preserving all safety invariants and test passes.
