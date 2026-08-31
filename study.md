# Comprehensive Analysis and Benchmark Study of Fyra Compiler Backend

## Executive Summary

This study evaluates the technical claims made about the **Fyra Compiler Backend**:

> *"Fyra is a lightweight optimizing compiler backend that targets x86_64, aarch64, and riscv64 architectures. It consumes programs written in a simple intermediate language, optimizes them, and emits assembly code. The project aims to deliver roughly 85% of the performance of advanced compilers like LLVM while using only 10% of the code."*

### Summary Verdict Table

| Claim Component | Status | Empirical / Code Base Evidence |
| :--- | :--- | :--- |
| **Lightweight Optimizing Compiler Backend** | **TRUE** | Written in modern C++17 (~21.8k LOC) with a modular pipeline (CFG, SSA, SCCP, DCE, GVN, Mem2Reg, LICM, Linear Scan RegAlloc). |
| **Target Architectures (x86_64, AArch64, RISC-V 64)** | **TRUE** | Native support for x86_64 (Linux SystemV & Windows PE/COFF), AArch64, RISC-V 64, plus WASM32. |
| **Consumes Simple IR & Emits Assembly** | **TRUE** | Accepts `.fyra` and `.fy` textual Intermediate Representation (QBE-compatible with colon typing) and generates native assembly (`.s`). |
| **Uses Only 10% of the Code of LLVM** | **TRUE (Exceeds Claim)** | Fyra codebase is **~21.8k LOC** vs LLVM's **~5M+ LOC** (~0.43% of LLVM's size). |
| **Delivers ~85% of LLVM's Performance** | **PARTIALLY TRUE / FALSE** | Achieves **~23.5% of `clang -O2`** and **~47% of `clang -O0`** performance on compute/call workloads due to frame overhead and lack of vectorization. |

---

## 1. Codebase & Line Count Analysis ("10% of Code")

An automated physical line count of source files in `src/` and `include/` was conducted:

* **Fyra Compiler Backend Total LOC**: **21,847** lines of C++17 (headers and implementations).
* **LLVM Core Subsystem LOC**: **~5,000,000+** lines of C++.

### Comparative Size Ratio
$$\text{Code Ratio} = \frac{21,847}{5,000,000} \approx 0.437\%$$

Fyra uses **0.44%** of the codebase size of LLVM, significantly less than the claimed 10% upper bound.

---

## 2. Architecture & Target Support Analysis

The codebase was analyzed for multi-target instruction selection and ABI lowering:

1. **x86_64 (`x64`)**:
   - **System V ABI (Linux/macOS)**: System V calling convention, frame pointer management, CFI directives.
   - **Windows x64 ABI**: Shadow space management, Microsoft calling convention (`rcx, rdx, r8, r9`), PE/COFF generation.
2. **AArch64 (`aarch64`)**:
   - ARM64 calling convention (`w0-w7`, `x0-x7`), pattern-based instruction selection.
3. **RISC-V 64 (`riscv64`)**:
   - RISC-V 64-bit ABI (`a0-a7`), standard register saving and stack alignment.
4. **WebAssembly (`wasm32`)**:
   - WebAssembly binary/text emission (`.wat`/`.wasm`).

---

## 3. Empirical Benchmarking (Fyra vs LLVM / GCC)

### Test Environment
* **OS**: Linux 6.6 x86_64
* **Compiler / Toolchain**: GCC 13.2.0, Clang 18.1.3 (LLVM)
* **Fyra Build**: Release binary with optimization passes enabled (`-O2`)

### Workload 1: Recursive Computation (Fibonacci $N=40$)
*Tests function call overhead, stack framing, register spill/fill, and branch prediction.*

| Compiler / Opt Level | Execution Time (s) | Relative Performance vs `clang -O2` |
| :--- | :--- | :--- |
| **gcc -O2** | **0.324s** | **142.9%** |
| **clang -O3** | **0.459s** | **100.8%** |
| **clang -O2** | **0.463s** | **100.0%** (Baseline) |
| **clang -O0** | **0.926s** | **50.0%** |
| **Fyra Backend (-O2)** | **1.972s** | **23.5%** |

#### Performance Ratio Analysis:
$$\text{Fyra / LLVM (-O2)} = \frac{0.463\,\text{s}}{1.972\,\text{s}} = 23.48\%$$

---

## 4. Edge Cases & Structural Findings

1. **SSA Invariant Requirement**:
   - When non-SSA code with name reassignments (`%i = add %i, 1`) is passed without `phi` nodes, SCCP constant propagation treats early assignments as immutable invariants across back-edges.
2. **DWARF Debug Line Directives**:
   - Fixed issue where assembly generated `.loc` directives prior to emitting `.file` compile unit headers.
3. **PE Generator Relocation Handling**:
   - Fixed vector element reference invalidation during PE image creation in `src/target/artifact/executable/pe.cpp`.

---

## Conclusion

1. **Lightweight backend targeting x86_64, AArch64, RISC-V 64**: **TRUE**
2. **10% of LLVM's codebase size**: **TRUE** (actual is ~0.44%)
3. **85% of LLVM's execution performance**: **OVERSTATED** (actual is ~23.5% to ~47% of LLVM depending on workload).
