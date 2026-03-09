# Fyra Compiler Backend

Fyra is a compiler backend written in C++17 that processes a QBE-like textual Intermediate Representation (IR) and compiles it into assembly code for multiple architectures. It is designed as a modular, extensible framework for building optimizing compiler backends.

## Features

### File Format Support
*   **Multiple Input Formats:** Supports `.fyra` files (Fyra format), and `.fy` files (Fyra format alternative extension)
*   **Automatic Format Detection:** Intelligently detects input format based on file extension
*   **QBE Compatibility:** Maintains compatibility with QBE Intermediate Language while extending capabilities

### IR Infrastructure
*   **Robust C++ IR Model:** Complete in-memory representation built with modern C++ features (smart pointers, RAII)
*   **User/Use Pattern:** Efficient operand tracking and def-use chain management
*   **Programmatic IR Building:** Fluent API via `IRBuilder` for generating IR from C++ code
*   **Type System:** Target-aware type system with support for custom integer bit widths and pointer types

### SSA Construction Pipeline
*   **Control Flow Graph (CFG) Construction:** Automatic CFG building with dominance analysis
*   **Dominator Tree Analysis:** Efficient dominator tree computation
*   **Dominance Frontier Analysis:** Precise dominance frontier calculation
*   **Phi Node Insertion:** Automatic SSA conversion with phi node placement
*   **SSA Renaming:** Complete SSA form construction

### Advanced Optimization Passes
*   **Sparse Conditional Constant Propagation (SCCP):** Advanced constant propagation with dead branch elimination
*   **Enhanced Dead Code Elimination:** Iterative dead instruction removal with dependency tracking
*   **Copy Elimination:** Redundant copy instruction removal
*   **Global Value Numbering (GVN):** Common subexpression elimination
*   **Mem2Reg:** Memory-to-register promotion for better optimization
*   **Loop Invariant Code Motion (LICM):** Move loop-invariant computations outside loops
*   **Control Flow Simplification:** Optimize control flow patterns

### Multi-Target Code Generation
*   **Enhanced CodeGen Framework:** Pattern-based instruction selection with validation
*   **Multiple Target Support:**
    *   Linux System V x64 ABI
    *   Windows x64 ABI
    *   AArch64 (ARM64)
    *   WebAssembly (Wasm32)
    *   RISC-V 64-bit (RiscV64)
*   **ASM Validation:** Built-in assembly validation with target-specific ABI compliance
*   **Object File Generation:** Direct object file generation for supported platforms

### Register Allocation
*   **Linear Scan Algorithm:** Efficient register allocation with spilling support
*   **Liveness Analysis:** Precise live interval computation
*   **Register Rewriting:** Automatic register assignment and spill code insertion

### Testing and Validation
*   **Comprehensive Test Suite:** Extensive unit tests using CTest framework
*   **Integration Testing:** End-to-end compilation pipeline testing
*   **ASM Validation Testing:** Automated assembly validation across all targets
*   **Regression Testing:** Continuous integration with test coverage

## Build Instructions

The project uses CMake as its build system and supports multiple platforms.

### Prerequisites

*   **C++17-compatible compiler:** GCC 7+, Clang 5+, or MSVC 2017+
*   **CMake 3.10 or higher**
*   **Standard build tools:** Make, Ninja, or Visual Studio

### Building on Linux/macOS

1.  **Create a build directory:**
    ```sh
    mkdir build
    cd build
    ```

2.  **Configure with CMake:**
    ```sh
    cmake ..
    ```

3.  **Build the project:**
    ```sh
    make
    ```

### Building on Windows

1.  **Create a build directory:**
    ```cmd
    mkdir build
    cd build
    ```

2.  **Configure with CMake:**
    ```cmd
    cmake .. -G "Visual Studio 16 2019"
    ```

3.  **Build the project:**
    ```cmd
    cmake --build . --config Release
    ```

### Using the Build Script

A convenience build script is provided for Unix-like systems:

```sh
./build.sh
```

This script automatically:
- Removes any existing build directory
- Creates a new build directory
- Configures the project with CMake
- Builds the project
- Runs the test suite

## Usage

The Fyra compiler driver supports multiple input formats and compilation targets.

### Basic Usage

```sh
./build/fyra_compiler <input.fyra|input.fy> -o <output.s> [options]
```

### Supported Input Formats

*   **`.fyra`** - Fyra Intermediate Language format
*   **`.fy`** - Fyra Intermediate Language format (alternative extension)

### Command Line Options

#### Target Platform Selection
```sh
--target <linux|windows|aarch64|wasm32|riscv64>  # Target platform (default: linux)
```

#### Code Generation Options
```sh
--enhanced                   # Use enhanced CodeGen with validation and optimization
--object                     # Generate object file in addition to assembly
--pipeline                   # Run comprehensive compilation pipeline for all targets
```

#### Validation and Debugging
```sh
--validate                   # Enable ASM validation (default: enabled)
--no-validate               # Disable ASM validation
--verbose                    # Enable verbose output for debugging
```

### Usage Examples

#### Basic Compilation
```sh
# Compile Fyra IL to Linux x64 assembly
./fyra_compiler program.fyra -o program.s

# Compile for Windows x64 target
./fyra_compiler program.fy -o program.s --target windows
```

#### Enhanced Compilation with Validation
```sh
# Use enhanced CodeGen with validation
./fyra_compiler program.fyra -o program.s --enhanced --verbose

# Generate both assembly and object file
./fyra_compiler program.fyra -o program.s --enhanced --object
```

#### Multi-Target Pipeline
```sh
# Run comprehensive pipeline for all targets
./fyra_compiler program.fyra -o program --pipeline

# Pipeline for specific target with verbose output
./fyra_compiler program.fyra -o program --pipeline --target aarch64 --verbose
```

### Assembly and Linking

The generated assembly file can be assembled and linked using standard tools:

```sh
# Linux/macOS with GCC
gcc program.s -o my_program

# Windows with MSVC
ml64 program.s /link /out:my_program.exe

# Cross-compilation for AArch64
aarch64-linux-gnu-gcc program.s -o my_program
```

## Testing

Fyra includes a comprehensive test suite covering all major components.

### Running Tests

To run the complete test suite from the `build` directory:

```sh
make test
# or for more detailed output:
ctest --output-on-failure
```

### Test Categories

#### Core Infrastructure Tests
*   **Parser Tests:** Lexical analysis and parsing validation
*   **IR Construction Tests:** Module, function, and instruction building
*   **Type System Tests:** Type validation and target-aware sizing

#### Analysis and Transform Tests
*   **CFG Builder Tests:** Control flow graph construction
*   **Dominator Analysis Tests:** Dominator tree and frontier computation
*   **SSA Construction Tests:** Phi insertion and SSA renaming
*   **Optimization Tests:** SCCP, DCE, GVN, copy elimination, LICM

#### Code Generation Tests
*   **Multi-Target Tests:** Assembly generation for all supported targets
*   **Enhanced CodeGen Tests:** Pattern matching and validation
*   **Register Allocation Tests:** Linear scan algorithm and spilling
*   **ASM Validation Tests:** Target-specific ABI compliance

#### Integration Tests
*   **End-to-End Tests:** Complete compilation pipeline
*   **Object Generation Tests:** Object file creation and validation
*   **Multi-Format Tests:** QBE, Fyra, and .fy format support

### Test Organization

Tests are organized in the `tests/` directory:

```
tests/
├── test_parser.cpp           # Parser and lexer tests
├── test_cfg.cpp              # Control flow graph tests
├── test_dom.cpp              # Dominator analysis tests
├── test_phi.cpp              # Phi insertion tests
├── test_sccp.cpp             # SCCP optimization tests
├── test_enhanced_codegen.cpp # Enhanced code generation tests
├── test_regalloc.cpp         # Register allocation tests
├── test_integration.cpp      # End-to-end integration tests
└── ...                       # Additional specialized tests
```

### Writing New Tests

To add new tests, create a `.cpp` file in the `tests/` directory and add the corresponding CMake configuration in `CMakeLists.txt`. All tests should follow the existing patterns and include appropriate assertions.

## Programmatic IR Building

In addition to parsing text files, the IR can be constructed programmatically using the `IRBuilder` class. This provides a fluent API for generating IR from C++ applications.

### Basic Example

Here's how to build a simple `add` function programmatically:

```cpp
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "codegen/EnhancedCodeGen.h"
#include <iostream>

void build_ir_programmatically() {
    // 1. Set up the Module and Builder
    ir::Module myModule("my_programmatic_module");
    ir::IRBuilder builder;
    builder.setModule(&myModule);

    // 2. Get necessary types
    ir::IntegerType* type_w = ir::IntegerType::get(32);

    // 3. Create a function: function w $my_add()
    ir::Function* my_add_func = builder.createFunction("my_add", type_w);

    // 4. Create a basic block and set the insertion point
    ir::BasicBlock* start_block = builder.createBasicBlock("start", my_add_func);
    builder.setInsertPoint(start_block);

    // 5. Create constants and instructions
    ir::Value* arg1 = ir::ConstantInt::get(type_w, 10);
    ir::Value* arg2 = ir::ConstantInt::get(type_w, 32);

    ir::Value* c = builder.createAdd(arg1, arg2);
    c->setName("c");

    builder.createRet(c);

    // 6. Generate code using Enhanced CodeGen
    auto codeGenerator = codegen::EnhancedCodeGenFactory::create(myModule, "linux");
    if (codeGenerator) {
        auto result = codeGenerator->compileToObject("output", true, false);
        if (result.success) {
            std::cout << "Compilation successful!" << std::endl;
        }
    }
}
```

### Advanced IR Construction

The `IRBuilder` supports all Fyra IL instructions and provides type-safe construction:

```cpp
// Memory operations
ir::Value* ptr = builder.createAlloc(ir::IntegerType::get(32));
builder.createStore(value, ptr);
ir::Value* loaded = builder.createLoad(ptr);

// Control flow
ir::BasicBlock* thenBlock = builder.createBasicBlock("then", func);
ir::BasicBlock* elseBlock = builder.createBasicBlock("else", func);
builder.createJnz(condition, thenBlock, elseBlock);

// Phi nodes for SSA
builder.setInsertPoint(mergeBlock);
ir::Value* phi = builder.createPhi(type_w);
builder.addPhiIncoming(phi, value1, thenBlock);
builder.addPhiIncoming(phi, value2, elseBlock);

// Function calls
std::vector<ir::Value*> args = {arg1, arg2};
ir::Value* result = builder.createCall(function, args, type_w);
```

### Integration with Optimization Pipeline

Programmatically built IR can be optimized using the same pipeline:

```cpp
// Run optimization passes
for (auto& func : myModule.getFunctions()) {
    // SSA construction
    transforms::CFGBuilder::run(*func);
    // ... other transforms
    
    // Enhanced optimizations
    transforms::SCCP sccp(error_reporter);
    sccp.run(*func);
    
    transforms::DeadInstructionElimination dce(error_reporter);
    dce.run(*func);
}
```

For complete API documentation, see [`doc/ir_builder.md`](doc/ir_builder.md) and [`doc/api_reference.md`](doc/api_reference.md).

## Architecture Overview

Fyra follows a modular architecture with clear separation of concerns:

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Frontend      │    │   Analysis &    │    │    Backend      │
│                 │    │   Transforms    │    │                 │
│ • Lexer         │────▶│ • CFG Builder   │────▶│ • Enhanced      │
│ • Parser        │    │ • SSA Builder   │    │   CodeGen       │
│ • IRBuilder     │    │ • Optimizations │    │ • Multi-Target  │
└─────────────────┘    │ • Reg Alloc     │    │ • ASM Validation│
                       └─────────────────┘    │ • Object Gen    │
                                              └─────────────────┘
```

### Core Components

*   **IR Core** (`ir/`): Value hierarchy, Module structure, type system
*   **Parser** (`parser/`): Lexical analysis and syntax parsing
*   **Transforms** (`transforms/`): Analysis passes and optimizations
*   **CodeGen** (`codegen/`): Target-specific code generation
*   **Validation** (`codegen/validation/`): ASM validation and ABI compliance

### Key Design Patterns

*   **User/Use Pattern**: Efficient operand tracking and def-use chains
*   **Builder Pattern**: Fluent API for IR construction
*   **Strategy Pattern**: Target-specific code generation backends
*   **Pass Infrastructure**: Modular analysis and transformation framework

## Documentation

Comprehensive documentation is available in the `doc/` directory:

*   **[Getting Started](doc/getting_started.md)** - Fast track for new users
*   **[Fyra IL Specification](doc/fyra_il.md)** - Complete language reference
*   **[IRBuilder Guide](doc/ir_builder.md)** - Programmatic IR construction
*   **[Architecture Guide](doc/architecture.md)** - System design and components
*   **[API Reference](doc/api_reference.md)** - Complete API documentation
*   **[AI Agent Guide](doc/AI.md)** - Cobuilding with AI agents
*   **[QBE Comparison](doc/reference.md)** - QBE to Fyra migration guide
*   **[Extending the Compiler](doc/extending_compiler.md)** - Developer guide

## Contributing

Fyra is designed to be extensible and welcomes contributions:

1. **Adding New Targets**: Implement the `TargetInfo` interface
2. **New Optimization Passes**: Extend the `TransformPass` framework
3. **IR Extensions**: Add new instruction types and operations
4. **Testing**: Expand test coverage for new features

### Development Setup

1. Fork the repository
2. Set up the development environment (see Build Instructions)
3. Make changes and add tests
4. Run the full test suite: `make test`
5. Submit a pull request

### Code Style

*   Follow modern C++17 practices
*   Use RAII and smart pointers
*   Maintain const-correctness
*   Add comprehensive tests for new features
*   Document public APIs thoroughly

## License

This project is open source. See the LICENSE file for details.

## Acknowledgments

Fyra is inspired by the [QBE Compiler Backend](https://c9x.me/compile/) and follows similar design principles while extending functionality for modern compiler needs.
