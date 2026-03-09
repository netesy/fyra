# Target Differences in Fyra IR

This document outlines the differences in how Fyra IR should be used when targeting different operating systems and architectures. While the IR is designed to be mostly portable, low-level operations like syscalls and certain ABI expectations require target-specific knowledge.

## Calling Conventions

### x86_64
*   **Linux (System V ABI):** Uses `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9` for the first six integer/pointer arguments.
*   **Windows (Microsoft x64 ABI):** Uses `%rcx`, `%rdx`, `%r8`, `%r9` for the first four integer/pointer arguments.
    *   **Shadow Store:** Windows requires 32 bytes of "shadow space" to be allocated on the stack by the caller immediately above the return address, even if the callee has fewer than 4 parameters. The Fyra compiler handles this automatically in the function prologue unless the "Simple Leaf" optimization is applied.

### AArch64
*   **Linux/Windows (AAPCS64):** Both use `x0`-`x7` for the first eight integer/pointer arguments and `v0`-`v7` for floating-point arguments.
*   **Windows ARM64 Specifics:** Register `x18` is reserved for the Platform Abstraction Layer (TEB) and should not be used by the IR. The Fyra compiler marks this register as reserved.

### RISC-V 64
*   **Linux (LP64D ABI):** Uses `a0`-`a7` for integer arguments and `fa0`-`fa7` for floating-point arguments.

## Syscalls

Syscall numbers and register mappings differ significantly between targets.

| Target | Syscall Number Register | Argument Registers |
| :--- | :--- | :--- |
| **Linux x64** | `rax` | `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` |
| **Linux AArch64** | `x8` | `x0`, `x1`, `x2`, `x3`, `x4`, `x5` |
| **Linux RISC-V 64** | `a7` | `a0`, `a1`, `a2`, `a3`, `a4`, `a5` |
| **Windows x64** | `rax` | `rcx`, `rdx`, `r8`, `r9` (highly unstable) |
| **Windows ARM64**| `x16` | `x0`-`x7` (highly unstable) |

**Note:** On Windows, direct syscalls are discouraged. It is recommended to call Win32 API functions instead.

## Stack Alignment
All supported targets (except Wasm) require the stack pointer to be 16-byte aligned before a `call` instruction. The Fyra compiler ensures this alignment in the function prologue by rounding up the allocated stack space.

## Optimization: Simple Leaf Functions
For functions that meet the following criteria:
1. No `call` instructions.
2. No local variables (after optimization).
3. No parameters.

The Fyra compiler will omit the frame pointer setup (`push rbp` / `mov rbp, rsp`) and stack pointer adjustments to produce minimal, high-performance machine code. This is particularly effective for small mathematical helpers and entry points.
