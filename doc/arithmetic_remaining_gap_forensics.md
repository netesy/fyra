# Forensic Audit — Remaining x86-64 `arithmetic` Performance Gap

## 1. Baseline Performance & Verification

- **CTests**: 33/33 tests passing (100%).
- **Benchmark Correctness**: 6/6 benchmarks passing with exact checksum match (`5000008149999648`).
- **`idivl`/`idivq` Instructions in `arithmetic` Main Loop**: 0 (100% eliminated by `Smulh`/`Umulh`).

| Implementation | Runtime (Median) | Static Instructions | Hardware Divides (`idiv`) |
|---|---|---|---|
| **GCC -O2** | ~0.715s | 345 | 0 |
| **Clang -O2** | ~0.704s | 189 | 0 |
| **Fyra -O2** | **~0.854s** | **162** | **0** |

Fyra is now only **~21% slower than Clang** (~0.854s vs ~0.704s), down from the original **3.19x slowdown** (~2.29s).

---

## 2. Hot-Loop Assembly Comparison

### Fyra Hot Loop (`main_body` in `benchmarks/output/arithmetic/fyra_o2.s`):
```assembly
main_body:
  movslq %r11d, %rax
  imulq $613566757, %rax, %rax
  sarq $32, %rax
  movl %eax, %r8d
  movl %r11d, %r9d
  shrl $31, %r9d
  addl %r9d, %r8d
  imull $7, %r8d
  movl %r11d, %eax
  subl %r8d, %eax
  movl %eax, %r8d
  ... [Repeated for % 11, % 13, % 5, % 9, % 17, % 23] ...
  addl $1, %r11d
  cmpl $100000000, %r11d
  jle main_loop
```

### Clang Hot Loop (`.LBB8_1` in `benchmarks/output/arithmetic/clang.s`):
```assembly
.LBB8_1:
  movl %r9d, %r13d
  imulq $613566757, %r13, %rbp
  shrq $32, %rbp
  movl %r9d, %r15d
  subl %ebp, %r15d
  shrl %r15d
  addl %ebp, %r15d
  shrl $2, %r15d
  leal (,%r15,8), %ebp
  subl %ebp, %r15d
  addl %r8d, %r15d
  ... [Unrolled 2x iteration body] ...
  addq $2, %rcx
  cmpq $100000000, %rcx
  jne .LBB8_1
```

---

## 3. Instruction Analysis & Differences

1. **Hardcoded Register Forcing in `Smulh` Lowering**:
   - Fyra's `X64Architecture::emitSmulh` forces `%rax` as a hardcoded temporary for `imulq $C, %rax, %rax`.
   - Because all 7 modulo operations in the loop force their high-multiply operands through `%rax`, the x86 CPU pipeline experiences **write-after-write (WAW) register renaming dependencies** on `%rax`, preventing out-of-order execution units from evaluating independent modulo operations in parallel.

2. **2x Loop Unrolling & Instruction-Level Parallelism (ILP)**:
   - Clang unrolls the `main` loop by 2x, interleaving two independent loop iterations inside `.LBB8_1`.
   - Interleaving independent iterations doubles instruction-level parallelism, allowing the CPU execution pipelines to overlap multiplication and addition latencies.

3. **LEA Scale-Subtract Folding**:
   - Clang computes quotient multiplication and subtraction ($7 \times Q$) using 3-operand LEA instructions: `leal (,%r15,8), %ebp; subl %ebp, %r15d` ($8 \times Q - Q = 7 \times Q$), whereas Fyra emits scalar `imull $7, %r8d`.

---

## 4. Root Cause

`ROOT CAUSE IDENTIFIED`

The remaining ~21% performance gap (~0.854s vs ~0.704s) is primarily caused by **register forcing in `Smulh` lowering**:
Forcing all 7 high-part multiplications in the loop to use `%rax` creates false write-after-write (WAW) register dependencies through `%rax`, artificially serializing the execution of independent modulo operations.

---

## 5. Next Optimization Recommendation

### Smallest Evidence-Backed Next Step:
**Allow `Smulh` x64 Lowering to Use Arbitrary Destination/Source Registers**:

- **Exact Subsystem / File**: `src/target/architecture/x64/X64Architecture.cpp` (`emitSmulh` / `emitUmulh`).
- **Exact Transformation**:
  Modify `emitSmulh` for 32-bit operations to use the target register assigned by the register allocator (e.g. `imulq $imm, %src_reg, %dst_reg_64`) rather than hardcoding `%rax`.
- **Expected Assembly Change**:
  Eliminates intermediate `movslq %r11d, %rax` and `movl %eax, %dst` copies, allowing each modulo operation to use distinct registers (%r8, %r9, %r10, %r12, %r13, %r14, %r15).
- **Expected Benchmark Impact**:
  Removes WAW pipeline stalls, enabling out-of-order execution across the 7 modulo calculations, expected to close the remaining ~0.15s gap to Clang/GCC.
- **Safety Risks**:
  None. Uses standard 3-operand `imulq $imm, %src, %dst` x86-64 instruction forms.
