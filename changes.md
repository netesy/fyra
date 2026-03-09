# Changelog

## Version 0.0.1

### Major Features
- **Complete Enhanced Instruction Support:** Implemented comprehensive instruction support for AArch64 and WASM32 architectures according to design specifications.
- **Enhanced ABI Compliance:**
  - **AArch64:** Full AAPCS64 calling convention support with enhanced argument passing and retrieval
  - **WASM32:** WebAssembly specification compliance with type-aware stack-based operations
  - **Windows x64:** Full Windows x64 calling convention support with stack alignment
- **Advanced Memory Operations:** Type-aware load/store operations with proper alignment and safety checks
- **Comprehensive Type System:** Complete type conversion matrix supporting int/float/double conversions with sign extension and truncation
- **Enhanced Control Flow:** Advanced comparison operations and conditional branching for all supported data types
- **Multi-Target Support:** RiscV64, Wasm32, AArch64, Windows x64, and Linux x64 architectures
- **Project Renamed:** The project has been renamed to "Fyra"

### Core Infrastructure
- **IR Foundation:** Complete in-memory Intermediate Representation with `Module`, `Function`, `Instruction` data structures
- **IR Builder:** Fluent-style `IRBuilder` for programmatic IR construction
- **Parser:** Comprehensive parser for QBE text format with automatic format detection
- **User/Use Model:** Major refactoring to `User`/`Use` model for robust transformation support
- **Type System:** Target-aware type system with support for custom integer bit widths and pointer types

### Analysis & Optimization Pipeline
- **SSA Construction:** Complete SSA analysis pipeline with CFG building, dominator analysis, and phi insertion
  - `CFGBuilder`: Explicit Control Flow Graph construction
  - `DominatorTree`: Efficient dominator tree computation
  - `DominanceFrontier`: Precise dominance frontier calculation
  - `PhiInsertion`: Automatic phi node placement at join points
- **Liveness Analysis:** Robust live range computation for virtual registers
- **Optimization Passes:**
  - Sparse Conditional Constant Propagation (SCCP) with dead branch elimination
  - Iterative Dead Instruction Elimination with dependency tracking
  - Copy Elimination for redundant instructions
  - Global Value Numbering (GVN) for common subexpression elimination
  - Mem2Reg for memory-to-register promotion
  - Loop Invariant Code Motion (LICM)
  - Control Flow Simplification

### Code Generation
- **Enhanced CodeGen Framework:** Pattern-based instruction selection with validation
- **Multi-Target Code Generation:** Support for Linux x64, Windows x64, AArch64, WASM32, and RiscV64
- **Register Allocation:** Linear scan algorithm with spilling support
- **ASM Validation:** Built-in assembly validation with target-specific ABI compliance
- **Object File Generation:** Direct object file generation for supported platforms
- **Instruction Set:** Full QBE instruction set implementation including bitwise operations (`and`, `or`, `xor`, `shl`, `shr`, `sar`)

### Testing & Validation
- **Comprehensive Test Suite:** Extensive unit tests using CTest framework covering:
  - Parser and lexer validation
  - IR construction and type system
  - CFG and dominator analysis
  - SSA construction and phi insertion
  - All optimization passes
  - Multi-target code generation
  - Register allocation and spilling
  - End-to-end integration tests
- **ASM Validation Testing:** Automated assembly validation across all targets
- **Regression Testing:** Continuous integration with test coverage

### Build System
- **CMake Integration:** Modern CMake build configuration
- **Multi-Platform Support:** Builds on Linux, macOS, and Windows
- **Compiler Support:** GCC 7+, Clang 5+, MSVC 2017+
- **Enhanced Testing:** Comprehensive test targets for all components

### Documentation
- **Complete API Reference:** Full API documentation for all public interfaces
- **Architecture Guide:** System design and component overview
- **IRBuilder Guide:** Programmatic IR construction examples
- **Fyra IL Specification:** Complete language reference
- **Getting Started Guide:** Fast track for new users
- **Developer Guide:** Instructions for extending the co
