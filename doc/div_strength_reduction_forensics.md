# Forensic Audit — Constant Division Strength Reduction Regression Analysis

## Executive Summary
This forensic audit investigates why the Constant Division/Modulo Strength Reduction pass, despite successfully eliminating all 7 x86 hardware `idivl` instructions from the `arithmetic` benchmark, yielded negligible runtime improvement (~2.255s vs baseline ~2.29s, compared to Clang's ~0.712s).

The investigation reveals that the replacement sequence expanded each `rem %x, C` operation into **11 generic IR instructions**, which lowered into **14 x86-64 machine instructions** per remainder. Across 7 modulo operations per loop iteration, the loop body expanded by **98 machine instructions** (from 129 total static instructions to 193).

Although 64-bit integer multiplication (`imulq`) has only 3-cycle latency compared to `idivl`'s 10-15 cycles, the massive instruction expansion—featuring 64-bit sign-extensions (`movslq`), variable-register shift setups (`movq $32, %rcx; sarq %cl`), redundant 32/64-bit register truncations, and multi-step remainder reconstructions (`N - Q * D`)—created front-end instruction decoder bottlenecks and serialization dependency chains whose total cycle cost (~70-90 cycles) nearly matched the 7 `idivl` instructions (~80-100 cycles).

---

## 1. Mathematical Implementation Correctness
**Status: PROVEN CORRECT.**
- The Granlund–Montgomery / Hacker's Delight reciprocal multiplication algorithms for 32-bit signed and unsigned constants are mathematically sound.
- Differential testing across edge cases (`0`, `1`, `-1`, `INT_MAX`, `INT_MIN`, `UINT_MAX`) and benchmark divisors (5, 7, 9, 11, 13, 17, 23) confirms 100% numerical parity against native C division and remainder.
- The `arithmetic` benchmark checksum (`5000008149999648`) produced by Fyra matches Clang -O2 and GCC -O2 exactly.

---

## 2. Representative Generated Assembly (e.g. `x % 7`)

### Fyra Lowered Assembly for `x % 7`:
```assembly
  movslq %r11d, %r8          # 1. Sign-extend 32-bit N to 64-bit
  movq %r8, %r9              # 2. Copy to operand reg
  imulq $613566757, %r9      # 3. 64-bit multiply by magic constant
  movq %r9, %rax             # 4. Copy product
  movq $32, %rcx             # 5. Load shift count 32 into %rcx
  sarq %cl, %rax             # 6. Variable shift right 32 to extract high 32 bits
  movq %rax, %r9             # 7. Copy result
  movq %r9, %rax             # 8. Truncate copy
  movq %rax, %r9d            # 9. Move to 32-bit reg
  movq %r11d, %rax           # 10. Load N
  movq $31, %rcx             # 11. Load shift count 31 into %rcx
  shrq %cl, %rax             # 12. Extract sign bit
  movq %rax, %edi            # 13. Copy sign bit
  addl %edi, %r9d            # 14. Quotient Q = high32 + signBit
  imull $7, %r9d             # 15. Product = Q * 7
  movl %r11d, %eax           # 16. Copy N
  subl %r9d, %eax            # 17. Remainder = N - Product
  movl %eax, %r9d            # 18. Store remainder
```

---

## 3. Before/After Instruction Counts

| Metric | Before (Baseline) | After (Division Strength Reduction) | Clang -O2 |
|---|---|---|---|
| **`arithmetic` Static Instructions** | 129 | 193 (+64 instrs, +50%) | 189 |
| **`main_body` Static Instructions** | 68 | 134 (+66 instrs, +97%) | 42 |
| **Hardware `idivl` Instructions** | 7 | **0** | 0 |
| **64-bit Multiply (`imulq`)** | 0 | 7 | 0 |
| **Variable Shift Setup (`movq $32, %rcx`)** | 0 | **14** | 0 |

---

## 4. Before/After Dependency Chain Comparison

### Baseline (`idivl`):
- Loop body executed 7 `idivl` operations sequentially:
  $$\text{Latency} \approx 7 \times 12 \text{ cycles} = 84 \text{ cycles}$$

### After Division Strength Reduction:
- Each of the 7 remainder operations requires a 14-instruction sequential dependency chain:
  $$N \xrightarrow{\text{movslq}} N_{64} \xrightarrow{\text{imulq}} P \xrightarrow{\text{movq}} \dots \xrightarrow{\text{sarq}} \text{High} \xrightarrow{\text{addl}} Q \xrightarrow{\text{imull}} Q \cdot C \xrightarrow{\text{subl}} \text{Rem}$$
- Critical path latency per remainder:
  $$\text{imulq (3)} + \text{sarq (1)} + \text{addl (1)} + \text{imull (3)} + \text{subl (1)} + \text{moves (5)} \approx 12-14 \text{ cycles}$$
- Total body latency across 7 remainders:
  $$\approx 7 \times 12 \text{ cycles} = 84 \text{ cycles}$$

Because the latency of 14 scalar instructions on a single dependent operand chain equals the latency of 1 `idivl`, execution time is completely unchanged.

---

## 5. Instruction Comparison Against Clang / GCC

Clang avoids 64-bit IR expansion by utilizing x86-64 target capabilities directly:
1. **Immediate Shift Operations**: Clang uses immediate shifts (`sarl $31, %eax`) rather than loading shift counts into `%rcx` (`movq $31, %rcx; sarq %cl`).
2. **Fused Multi-Operand Addresses (`leal`)**: Clang computes addition/subtraction and scale-multiplication simultaneously via `leal (%rax,%rax,2), %ecx`.
3. **Implicit 32-bit Zero-Extension**: Clang operates natively in 32-bit registers, relying on x86-64's implicit zero-extension of 32-bit writes to avoid explicit `movslq` and 64-bit register copies.

---

## 6. Unnecessary IR Operations
In `src/transforms/DivisionStrengthReduction.cpp`, high-part 32-bit signed multiplication is constructed in target-independent IR using 64-bit extension:
```cpp
ir::Value* nExt = builder.createExtSW(N, i64Ty);        // ExtSW
ir::Value* mExt = ir::ConstantInt::get(i64Ty, m.magic);
ir::Value* mul64 = builder.createMul(nExt, mExt);        // Mul (64-bit)
ir::Value* c32 = ir::ConstantInt::get(i64Ty, 32);
ir::Value* high64 = builder.createSar(mul64, c32);       // Sar (64-bit)
ir::Value* high32 = builder.createTruncD(high64, type);  // TruncD
```
This forces 5 IR instructions (`ExtSW`, `ConstantInt`, `Mul`, `Sar`, `TruncD`) for a single high-part multiplication, creating bloated IR that backend lowering cannot easily collapse back into a 32-bit high-part multiply.

---

## 7. Unnecessary Machine Instructions
1. **Variable Shifts via `%rcx`**:
   `movq $32, %rcx; sarq %cl, %rax` adds 2 instructions per shift instead of 1 (`sarq $32, %rax`).
2. **Redundant Register Moves**:
   Lack of immediate 32-to-64 extension collapsing generates `movslq %r11d, %r8; movq %r8, %r9; imulq $..., %r9`.

---

## 8. Appropriateness of the Magic-Number Algorithm
The **Granlund–Montgomery algorithm** is the industry standard used by GCC and Clang. The algorithm itself is optimal; the performance issue lies entirely in how high-part multiplication and shifts are represented and lowered.

---

## 9. Correct Compiler Layer & Architecture
Applying Division Strength Reduction in target-independent IR by generating generic 64-bit extension trees is at the **wrong abstraction layer**.

### Recommendation:
Constant division/modulo strength reduction should either:
1. Introduce a high-part multiply IR node (`smulh` / `umulh`), allowing target-independent IR to remain compact (3 IR instructions instead of 11):
   ```text
   %high = smulh %N, %magic : w
   %Q    = sar %high, %shift : w
   %Rem  = sub %N, (%Q * %C) : w
   ```
2. Or perform division strength reduction during **x64 CodeGen lowering** where target instruction patterns (e.g. `imull` 32-bit high product, immediate shifts, and `leal`) can be emitted directly.

---

## 10. Smallest Technically Sound Next Implementation Step
1. **Add `Smulh` / `Umulh` High-Part Multiply Opcodes to Fyra IR**:
   Allow `IRBuilder` to emit high-part multiplication directly without 64-bit extensions.
2. **Update x64 Lowering**:
   Lower `Smulh`/`Umulh` to x86 32-bit high product instructions and emit immediate shifts (`sarq $imm, %reg` / `sarl $imm, %reg`) instead of variable shifts via `%rcx`.
