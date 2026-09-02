# Forensic Investigation Report: Redundant `cltq` Sign Extension Analysis

## Objective & Executive Summary
This report presents a comprehensive forensic investigation into whether any real `cltq` (Convert Long to Quad, sign-extend `%eax` to `%rax`) emission sites in the current Fyra x86-64 backend are provably redundant using facts that already exist in the compiler. Per task instructions, compiler production code, register allocation, SSA, liveness, and LEA optimizations remained completely frozen.

**Central Question:**
> *Does Fyra already possess enough information to prove that any currently emitted `cltq` is redundant, without introducing new range analysis or speculative assumptions?*

**Forensic Conclusion:**
> **NO REAL REDUNDANT CLTQ FOUND — FREEZE**

Every single static `cltq` instruction generated across the real benchmark suite operates on a dynamic variable (function return value, function parameter, phi loop induction variable, or arithmetic result). In Fyra IR/LIR, dynamic 32-bit registers do not carry non-negativity range facts. Constant `extsw` operands are folded compile-time by `SCCP`/`IRBuilder` and never reach code generation. Without global range analysis, suppressing `cltq` on dynamic variables would convert sign extension to zero extension, corrupting negative 32-bit values ($val < 0$). Production code remains 100% untouched.

---

## 1. Every Real `cltq` Emission Site

In the Fyra x86-64 backend, `cltq` is emitted exclusively in `X64Architecture::emitCast` (`src/target/architecture/x64/X64Architecture.cpp` line 1258) when lowering `ir::Instruction::ExtSW` (sign-extend 32-bit `w` to 64-bit `l`) for register operands:

```assembly
movl %r32, %eax
cltq
movq %rax, %destOp
```

**Enumeration of All 10 Static `cltq` Emission Sites Across Corpus:**

| Benchmark | Assembly Line | Function / BB | Source IR | Source Type → Dest Type | SSA Provenance / Defining Instr |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `arithmetic` | Line 282 | `main` (`@body`) | `%v1_ext = extsw %v1 : l` | `i32` → `i64` | `Call` (`$test_mixed`) |
| `arithmetic` | Line 286 | `main` (`@body`) | `%v2_ext = extsw %v2 : l` | `i32` → `i64` | `Call` (`$test_chained`) |
| `arithmetic` | Line 290 | `main` (`@body`) | `%v3_ext = extsw %v3 : l` | `i32` → `i64` | `Call` (`$test_constant`) |
| `int_widths` | Line 41 | `test_widths` (`@start`) | `%e_ext = extsw %e : l` | `i32` → `i64` | `Parameter` (`%e`) |
| `int_widths` | Line 91 | `main` (`@body`) | `%i_l = extsw %i : l` | `i32` → `i64` | `PhiNode` (`%i` induction var) |
| `int_widths` | Line 111 | `main` (`@body`) | `%i_l = extsw %i : l` | `i32` → `i64` | `PhiNode` (`%i` induction var) |
| `loops` | Line 40 | `test_loop_sum` (`@body`) | `%term_l = extsw %term : l` | `i32` → `i64` | `Mul` (`%term = mul %i, 2 : w`) |
| `realistic_dot_product` | Line 37 | `dot_product` (`@body`) | `%a = extsw %a_w : l` | `i32` → `i64` | `Add` / Fused LEA (`%a_w`) |
| `realistic_dot_product` | Line 47 | `dot_product` (`@body`) | `%b = extsw %b_w : l` | `i32` → `i64` | `Add` (`%b_w = add %t7, 2 : w`) |
| `reg_pressure` | Line 150 | `main` (`@body`) | `%v_ext = extsw %v : l` | `i32` → `i64` | `Call` (`$test_reg_pressure_16`) |

---

## 2. Source IR/LIR for Each Site

* **Calls (4 sites):** `arithmetic` (3 calls) and `reg_pressure` (1 call) sign-extend function return values. Return values can be negative integer results.
* **Parameters (1 site):** `int_widths` sign-extends parameter `%e`. Function parameters can be negative signed integers.
* **Phi Nodes (2 sites):** `int_widths` sign-extends loop induction variable `%i`.
* **Arithmetic Results (3 sites):** `loops` sign-extends `%term = mul %i, 2` and `realistic_dot_product` sign-extends `%a_w` (fused LEA result) and `%b_w` (`add %t7, 2`).

---

## 3. SSA Provenance Analysis

Tracing through `User`/`Use` and `getOriginalValue()` provenance:
* **Function Calls:** Defined by `ir::Instruction::Call`. No range constraints attached to return values in LIR.
* **Parameters:** Defined by `ir::Parameter`. Calling convention permits full 32-bit signed integer range $[-2^{31}, 2^{31}-1]$.
* **Phi Nodes:** Defined by `ir::PhiNode`. Values range dynamically across loop execution.
* **Arithmetic Instructions:** Defined by `ir::Instruction::Mul` / `ir::Instruction::Add`. In standard 32-bit two's complement arithmetic, results wrap on overflow and can produce negative values if bit 31 becomes set.

---

## 4. Redundancy Evaluation (Theoretical vs Actual)

* **Constant Operands (`extsw w 100`):** *Theoretically redundant.* However, `SCCP` (Sparse Conditional Constant Propagation) and `IRBuilder` fold constant `extsw` expressions at compile-time. Consequently, zero constant `extsw` instructions reach `X64Architecture::emitCast` in optimized builds.
* **Zero-Extension Chains (`ExtUW -> ExtSW`):** *Theoretically redundant.* However, audit of the entire benchmark suite confirms that 0 zero-extension to sign-extension chains exist in real code.
* **Dynamic Variables (All 10 sites):** *Actually NOT redundant.* Every single reaching `extsw` operates on a dynamic virtual register. Because bit 31 may be set during execution, `cltq` is semantically required.

---

## 5. Existing Compiler Fact Proving Redundancy

**Finding:** Fyra currently possesses **zero existing compiler facts** proving that bit 31 is zero for any of the 10 reaching `extsw` emission sites.

* Fyra IR/LIR does not maintain value intervals or non-negativity flags on virtual registers.
* To prove bit 31 is zero for dynamic variables, the compiler would require a new global range-analysis framework, which is explicitly forbidden by Rule 14.

---

## 6. Negative Cases (Semantic Protection Proof)

Removing `cltq` converts 32-bit sign extension ($\text{SEXT}_{32 \to 64}$) into 32-bit zero extension ($\text{ZEXT}_{32 \to 64}$) because x86-64 `movl` zero-extends bits 32..63 of GPRs.

Consider the following semantic test values:

| Value | 32-Bit Representation | With `cltq` ($\text{SEXT}$) | Without `cltq` ($\text{ZEXT}$) | Semantic Result |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `0x00000000` | `0x0000000000000000` (`0`) | `0x0000000000000000` (`0`) | Safe |
| `1` | `0x00000001` | `0x0000000000000001` (`1`) | `0x0000000000000001` (`1`) | Safe |
| `INT32_MAX` | `0x7FFFFFFF` | `0x000000007FFFFFFF` (`2147483647`) | `0x000000007FFFFFFF` (`2147483647`) | Safe |
| `-1` | `0xFFFFFFFF` | `0xFFFFFFFFFFFFFFFF` (`-1`) | `0x00000000FFFFFFFF` (`+4294967295`) | **CORRUPTED** |
| `-500` | `0xFFFFFE0C` | `0xFFFFFFFFFFFFFE0C` (`-500`) | `0x00000000FFFFFE0C` (`+4294966796`) | **CORRUPTED** |
| `INT32_MIN` | `0x80000000` | `0xFFFFFFFF80000000` (`-2147483648`) | `0x0000000080000000` (`+2147483648`) | **CORRUPTED** |

**Conclusion:** For all negative signed 32-bit values ($val < 0$), removing `cltq` causes catastrophic mathematical corruption by converting negative values into large positive numbers. Preserving `cltq` is mandatory.

---

## 7. Assembly Before/After (Theoretical Candidate)

For a hypothetical non-negative constant `extsw w 100 : l`:

**BEFORE:**
```assembly
  movl $100, %eax
  cltq
  movq %rax, %r8
```

**AFTER (If eliminated):**
```assembly
  movl $100, %eax
  movq %rax, %r8
```

*Architectural Proof:* `movl $100, %eax` zero-extends bits 32..63 of `%rax` to 0. Since bit 31 of 100 is 0, sign extension produces `0x0000000000000064`, identical to zero extension. However, as proven in Section 4, SCCP folds all such constant cases before code generation, so this transformation never triggers in production.

---

## 8. Dynamic Execution-Count Verification

Forensic breakdown of dynamic `cltq` executions:

| Benchmark | Static `cltq` Count | Containing Loop / Function | Iterations / Call Count | Dynamic `cltq` Executions |
| :--- | :--- | :--- | :--- | :--- |
| `arithmetic` | 3 | `main_loop` in `main` | 100,000,000 | **300,000,000** |
| `int_widths` | 3 | `test_widths` & `main_loop` | 50,000,000 | **150,000,000** |
| `loops` | 1 | `test_loop_sum_body` | 50 * 2,000,000 | **100,000,000** |
| `realistic_dot_product` | 2 | `dot_product_body` | 20 * 5,000,000 | **200,000,000** |
| `reg_pressure` | 1 | `main_loop` in `main` | 50,000,000 | **50,000,000** |
| `tail_recursion` | 0 | N/A | 0 | **0** |
| **CORPUS TOTAL** | **10** | | | **800,000,000** |

All 10 static `cltq` instructions execute unconditionally on every loop iteration, confirming exactly 800 million dynamic executions across the corpus.

---

## 9. Correctness & Test Results

* **CTest Suite:** **26/26 PASSED** (100%)
* **Benchmark Correctness Checksums:** **6/6 PASSED**
* **`realistic_dot_product` Exact Checksum:** **`-5962125950464483584` (PASS)**
* **Static Linking Verification:** **PASS**
* **Compiler Production Code:** **100% Frozen and Untouched**

---

## 10. Git Diff / Scope Audit

### Production Compiler Changes
* **None.** Production compiler code was kept 100% frozen.

### Infrastructure & Audit Documentation
* `benchmarks/run_suite.py`: Preserved memory classification fix from LEA task.
* `benchmarks/benchmark_results.json`: Updated benchmark results.
* `benchmarks/benchmark_results.csv`: Updated benchmark results CSV.
* `doc/lea-audit-report.md`: Frozen LEA audit report.
* `doc/lea-forensic-audit.md`: Forensic LEA audit report.
* `doc/cltq-audit-report.md`: `cltq` safety audit report.
* `doc/cltq-investigation-report.md`: Forensic `cltq` investigation report.

No temporary build artifacts or pycache files tracked.

---

## 11. Exact Final Classification

```text
NO REAL REDUNDANT CLTQ FOUND — FREEZE
```
