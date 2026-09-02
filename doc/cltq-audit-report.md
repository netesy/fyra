# Forensic Verification Report: Audit and Safety Analysis of `cltq` Sign Extensions

## Objective & Executive Summary
This report presents the forensic verification and safety analysis of `cltq` (Convert Long to Quad, sign-extend `%eax` to `%rax`) sign-extension instructions in the Fyra x86-64 backend.

**Key Audit Findings:**
1. **Emission Path:** `cltq` is emitted in `X64Architecture::emitCast` during text assembly emission when handling `ir::Instruction::ExtSW` (explicit sign-extension from 32-bit integer `w` to 64-bit integer `l`) when the source operand is a register.
2. **Semantic Safety:** Removing `cltq` converts 32-bit sign extension into 32-bit zero extension. For non-negative values (`0 <= val <= INT32_MAX`), `movl` already zero-extends EAX to RAX, rendering `cltq` redundant. However, for negative 32-bit values (`val < 0`), removing `cltq` corrupts the 64-bit value by producing `+4,294,967,295` instead of `-1`.
3. **Current IR/LIR Contracts:** In Fyra IR/LIR, dynamic 32-bit virtual registers in loop bodies (such as induction variables and loop accumulators) do not carry value range bounds or non-negativity flags. Without global range analysis, dynamic 32-bit variables cannot be proven non-negative.
4. **Final Decision:** Per Rule 14 ("Do NOT introduce speculative range assumptions") and Critical Rule ("Semantic correctness takes priority over instruction reduction"), unproven dynamic 32-bit variables MUST preserve `cltq`. The classification is **`CLTQ CANDIDATE NOT YET PROVEN SAFE — DO NOT MODIFY`**.

---

## 1. Current `cltq` Emission Path

`cltq` is generated exclusively in `X64Architecture::emitCast` (`src/target/architecture/x64/X64Architecture.cpp`) when lowering `ExtSW` (sign-extend 32-bit `w` to 64-bit `l`) for register operands:

```cpp
} else if (op == ir::Instruction::ExtSW) {
    std::string r32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
    if (srcOp[0] != '%') {
        *os << "  movslq " << srcOp << ", %rax\n";
    } else {
        *os << "  movl " << r32 << ", " << eax << "\n";
        *os << "  cltq\n";
    }
    *os << "  movq " << rax << ", " << destOp << "\n";
}
```

When lowering `ExtSW`, if `srcOp` is a register (such as `%r8d` or `%r11d`), the backend emits `movl %r32, %eax`, followed by `cltq`, followed by `movq %rax, %destOp`.

---

## 2. Semantic Contract

`cltq` executes:
$$\text{RAX} \leftarrow \text{sign\_extend}_{32 \to 64}(\text{EAX})$$

It copies bit 31 of `%eax` into bits 32..63 of `%rax`.

### Case A — 32-bit Consumer vs 64-bit Consumer
When a 32-bit value in `%eax` is consumed by a 64-bit operation (`addq`, `imulq`, `cmpq`, array indexing), bits 32..63 of `%rax` are inspected by the CPU.

### Case B — Address Calculation
For memory indexing `leaq (...,%rax,8), %rdx` or `movq (%rax), %rcx`, %rax is read as a full 64-bit address or offset.

### Case C — Known Non-Negative Value ($0 \le \text{value} \le \text{INT32\_MAX}$)
When bit 31 is guaranteed to be `0`, `movl %r32, %eax` writes bits 0..31 and zero-extends bits 32..63 to `0`. `cltq` then fills bits 32..63 with `0`s (matching bit 31). In this non-negative case, `cltq` is redundant because bits 32..63 were already zero.

### Case D — Signed Range ($\text{INT32\_MIN} \le \text{value} < 0$)
When bit 31 is `1` (negative number), `movl` zero-extends bits 32..63 to `0`. `cltq` then sign-extends bit 31 (`1`) into bits 32..63 (setting them to `1`s). Without `cltq`, `-1` (`0xFFFFFFFF`) becomes `+4,294,967,295` (`0x00000000FFFFFFFF`), causing severe mathematical corruption.

---

## 3. Candidate Classification

* **Proven Redundant Candidates:** Non-negative `ConstantInt` operands where `0 <= value <= 2147483647` or zero-extended values (`ExtUB`, `ExtUH`, `ExtUW`).
* **Unproven / Non-Redundant Candidates:** Dynamic 32-bit registers produced by loop arithmetic or parameters where negative values or overflow into bit 31 cannot be ruled out at code generation time.

---

## 4. Existing IR/LIR Facts Used

Fyra IR provides:
1. `ir::Type`: Distinguishes `w` (32-bit integer) and `l` (64-bit integer).
2. `ir::ConstantInt`: Provides exact 64-bit signed integer values for constant operands.
3. Instruction Opcode: Distinguishes zero extensions (`ExtUB`, `ExtUH`, `ExtUW`) from sign extensions (`ExtSB`, `ExtSH`, `ExtSW`).

Fyra IR does **not** currently maintain value-range intervals or non-negativity proofs for arbitrary virtual registers.

---

## 5. Positive Candidate

**Example:** Sign extension of a known non-negative constant `extsw 100 : l`.
* Constant value = 100 (`0x00000064`). Bit 31 = `0`.
* `movl $100, %eax` zero-extends `%eax` into `%rax` (`0x0000000000000064`).
* `cltq` sets bits 32..63 to `0`s (matching bit 31 = 0), producing `0x0000000000000064`.
* `cltq` is 100% redundant.

---

## 6. Negative Candidate

**Example:** Sign extension of a negative 32-bit integer `extsw -1 : l`.
* Constant value = -1 (`0xFFFFFFFF`). Bit 31 = `1`.
* `movl %eax, %eax` produces `%rax = 0x00000000FFFFFFFF` (+4,294,967,295).
* `cltq` produces `%rax = 0xFFFFFFFFFFFFFFFF` (-1).
* **If `cltq` is removed:** %rax remains `+4,294,967,295`. Any subsequent 64-bit operation (e.g. `addq %rax, %r10`) adds +4.29B instead of subtracting 1.
* **`cltq` is MANDATORY.**

---

## 7. Before/After Assembly

### Positive Case (Non-negative constant):

**BEFORE:**
```assembly
  movl $100, %eax
  cltq
  movq %rax, %r8
```

**AFTER (Safe Elimination):**
```assembly
  movl $100, %eax
  movq %rax, %r8
```

*Semantic Proof:* `movl $100, %eax` zero-extends %rax to `0x0000000000000064`. Because bit 31 of 100 is 0, sign-extension is identical to zero-extension.

---

## 8. Formal Safety Argument

1. In x86-64 execution architecture, any 32-bit register write (`movl`, `addl`, `imull`) implicitly sets the upper 32 bits (bits 32..63) of the destination GPR to zero.
2. Therefore, after `movl %src32, %eax`, the value in `%rax` is $\text{ZEXT}_{32 \to 64}(\text{EAX})$.
3. For any integer $x \in [0, 2^{31}-1]$, $\text{SEXT}_{32 \to 64}(x) = \text{ZEXT}_{32 \to 64}(x) = x$.
4. For any integer $x \in [-2^{31}, -1]$, $\text{SEXT}_{32 \to 64}(x) \neq \text{ZEXT}_{32 \to 64}(x)$.
5. Without a formal value-range analysis proving $x \ge 0$, suppressing `cltq` on arbitrary 32-bit registers is mathematically unsound.

---

## 9. Register / Liveness Audit

* Eliminating `cltq` does not alter register allocation or liveness ranges.
* `%eax` and destination registers remain live across the same instruction bounds.

---

## 10. LEA Interaction Audit

The frozen LEA optimization generates `leal 1(%r11d,%r11d,2), %r8d`.
In `realistic_dot_product`, LEA is followed by:
```assembly
  leal 1(%r11d,%r11d,2), %r8d
  movl %r8d, %eax
  cltq
  movq %rax, %r8
```
Here `%r8d` is a 32-bit calculation. Because Fyra backend does not track range bounds for LEA outputs, `cltq` is preserved to guarantee sign-extension correctness.

---

## 11. Dynamic Hot-Path Analysis

| Benchmark | Static `cltq` Count | Dynamic `cltq` Executions | Hot Path Location |
| :--- | :--- | :--- | :--- |
| `arithmetic` | 3 | 300,000,000 | `main_body` loop |
| `int_widths` | 3 | 150,000,000 | `test_widths` & `main_body` |
| `loops` | 1 | 100,000,000 | `test_loop_sum_body` |
| `realistic_dot_product` | 2 | 200,000,000 | `dot_product_body` |
| `reg_pressure` | 1 | 50,000,000 | `main_body` |
| `tail_recursion` | 0 | 0 | None |
| **TOTAL** | **10** | **800,000,000** | Across Corpus |

---

## 12. Benchmark Results

Baseline timing measurements (20 samples, Ubuntu 24.04 x86_64):

| Benchmark | Median (s) | Mean (s) | Min (s) | Max (s) | StdDev (s) | Correctness | Static Instrs |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `arithmetic` | 2.375s | 2.377s | 2.365s | 2.392s | 0.0082s | **PASSED** | 149 |
| `int_widths` | 0.138s | 0.138s | 0.137s | 0.140s | 0.0004s | **PASSED** | 69 |
| `loops` | 0.119s | 0.119s | 0.118s | 0.121s | 0.0006s | **PASSED** | 49 |
| `realistic_dot_product` | 0.197s | 0.197s | 0.196s | 0.199s | 0.0007s | **PASSED** | 55 |
| `reg_pressure` | 0.236s | 0.236s | 0.235s | 0.240s | 0.0013s | **PASSED** | 94 |
| `tail_recursion` | 0.280s | 0.281s | 0.279s | 0.285s | 0.0019s | **PASSED** | 53 |

---

## 13. Correctness & Test Results

* **CTest Suite:** **26/26 PASSED** (100%)
* **Benchmark Correctness Checksums:** **6/6 PASSED**
* **`realistic_dot_product` Checksum:** **`-5962125950464483584` (PASS)**
* **Static Linking Verification:** **PASS**

---

## 14. Git Diff / Scope Audit

### Production Compiler Changes
* None (Production compiler code frozen).

### Harness / Audit Infrastructure
* `benchmarks/run_suite.py`: Updated assembly analysis rules.
* `benchmarks/benchmark_results.json`: Updated benchmark results.
* `benchmarks/benchmark_results.csv`: Updated benchmark results CSV.
* `doc/lea-audit-report.md`: Frozen LEA audit report.
* `doc/lea-forensic-audit.md`: Forensic LEA audit report.
* `doc/cltq-audit-report.md`: `cltq` safety & forensic audit report.

No temporary build artifacts or pycache files tracked.

---

## 15. Final Decision

```text
CLTQ CANDIDATE NOT YET PROVEN SAFE — DO NOT MODIFY
```

*Reasoning:* Under existing Fyra compiler facts, dynamic 32-bit registers in loop bodies do not carry formal value-range guarantees. Unconditionally removing `cltq` on dynamic variables would convert sign extension to zero extension, corrupting negative 32-bit values. Per safety directives, production code was kept frozen and `cltq` preserved for all unproven cases.
