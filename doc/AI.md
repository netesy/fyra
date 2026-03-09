# AI Agent Guide for Fyra Backend

This document provides technical specifications and implementation patterns for AI agents co-building with the Fyra compiler.

## IR Structure

- **Module**: Contains `Functions`, `GlobalVariables`, and `Types`.
- **Function**: Contains `BasicBlocks` and `Parameters`.
- **BasicBlock**: Contains a list of `Instructions` and maintains predecessor/successor links.
- **Instruction**: Derived from `User`, which manages `Use` objects pointing to `Values`. This forms a complete def-use chain.

## Backend Architecture

The backend follows a simple "Direct Dispatch" pattern. `CodeGen::emit` iterates through the IR and calls virtual methods on a `TargetInfo` object.

### Key Classes

- `CodeGen`: The main engine. Manages `Assembler`, `Relocations`, and `Symbols`.
- `TargetInfo`: Abstract base class for target-specific logic.
- `Assembler`: Buffer-based machine code generator.

## Implementation Patterns

### 1. Stack Management

Fyra uses a simplified stack model where every value-producing instruction is assigned an 8-byte slot relative to the frame pointer (`rbp`/`x29`/`s0`).

**Pattern for Prologue:**
```cpp
void MyTarget::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    cg.currentStackOffset = 0;
    // 1. Assign slots to parameters
    // 2. Assign slots to value-producing instructions
    // 3. Align stack and call emitPrologue
}
```

### 2. Binary Emission

Binary emission must match the behavior of textual assembly exactly. Use the `Assembler` to emit raw bytes.

**Pattern for Binary Op:**
```cpp
void MyTarget::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Textual
    } else {
        auto& assembler = cg.getAssembler();
        // 1. Load operands from stack slots into registers
        // 2. Emit R-type/I-type instruction opcode and operands
        // 3. Store register result back to the instruction's stack slot
    }
}
```

### 3. Relocations

For branches and global accesses, use `CodeGen::addRelocation`.
- `R_X86_64_PC32`: x86_64 relative 32-bit offset.
- `R_AARCH64_CALL26`: AArch64 function call offset.
- `R_RISCV_CALL`: RISC-V `auipc` + `jalr` pair.

## Best Practices

1.  **Always implement both paths**: Never implement textual emission without corresponding binary emission (and vice versa) for core instructions.
2.  **Use `getStackOffset`**: Always use `cg.getTargetInfo()->getStackOffset(cg, val)` instead of accessing the map directly to handle constants safely.
3.  **Validate Alignment**: Targets like AArch64 and System V x64 strictly require 16-byte stack alignment before a `call`.
4.  **Check Operand Sizes**: Use the instruction's type to determine whether to emit 32-bit (`w`) or 64-bit (`x`/`q`) variants of instructions.

## Syscall Mapping

**Note:** Syscall numbers are highly dependent on both the OS and the Architecture. The following table describes the *register mapping* for the `syscall` instruction.

| Target | Syscall Num Register | Argument Registers (in order) |
|--------|----------------------|--------------------------------|
| Linux x64 | `rax` | `rdi, rsi, rdx, r10, r8, r9` |
| Linux ARM64 | `x8` | `x0, x1, x2, x3, x4, x5` |
| RISC-V 64 | `a7` | `a0, a1, a2, a3, a4, a5` |
| Windows x64 | `rax` | `r10, rdx, r8, r9` |
| Windows ARM64 | `x16` | `x0, x1, x2, x3, x4, x5, x6, x7` |

### Example (Linux x64 `write`):
```fyra
# write(fd=1, buf=$msg, len=13) -> syscall 1
%res = syscall(l 1, l 1, l $msg, l 13) : l
```
