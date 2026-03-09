# Fyra Compiler Architecture

This document provides a comprehensive overview of the Fyra compiler architecture, including system design, component interactions, and implementation details.

## Table of Contents

1. [System Overview](#system-overview)
2. [Architecture Principles](#architecture-principles)
3. [Component Architecture](#component-architecture)
4. [Data Flow and Processing Pipeline](#data-flow-and-processing-pipeline)
5. [Core Components](#core-components)
6. [Design Patterns](#design-patterns)
7. [Target System Integration](#target-system-integration)
8. [Extension Points](#extension-points)

---

## System Overview

Fyra is a modular compiler backend designed for flexibility, performance, and extensibility. It transforms high-level intermediate representations into optimized machine code for multiple target architectures.

### High-Level Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Frontend      │    │   Analysis &    │    │    Backend      │
│   (Input)       │────▶│   Transforms    │────▶│   (Output)      │
│                 │    │                 │    │                 │
│ • Lexer         │    │ • CFG Builder   │    │ • Enhanced      │
│ • Parser        │    │ • SSA Builder   │    │   CodeGen       │
│ • IRBuilder     │    │ • Optimizations │    │ • Multi-Target  │
│                 │    │ • Reg Alloc     │    │ • ASM Validation│
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Key Characteristics

- **Modular Design**: Clear separation between frontend, analysis, and backend
- **SSA-Based**: Static Single Assignment form for optimization
- **Multi-Target**: Support for x86-64, AArch64, WebAssembly, RISC-V
- **Extensible**: Plugin architecture for new targets and passes
- **Performance-Oriented**: Efficient algorithms and data structures

---

## Architecture Principles

### 1. Modularity and Separation of Concerns

Each component has a well-defined responsibility:
- **Frontend**: Input parsing and IR construction
- **Analysis**: Program analysis and transformation
- **Backend**: Code generation and optimization

### 2. Type Safety

- Strong typing throughout the IR
- Compile-time type checking
- Type-aware optimization passes

### 3. Immutability and Functional Design

- Immutable IR objects where possible
- Functional transformation passes
- Clear data flow dependencies

### 4. Performance by Design

- Efficient data structures (User/Use pattern)
- Linear-time algorithms where possible
- Memory-conscious implementation

### 5. Extensibility

- Plugin architecture for new targets
- Configurable optimization pipelines
- Clear extension interfaces

---

## Component Architecture

### Frontend Components

#### Lexer (`parser/Lexer.h`, `parser/Lexer.cpp`)

**Responsibility**: Tokenize input text into meaningful symbols

**Key Features**:
- Multi-format support (.fyra, .fy)
- Efficient lookahead and backtracking
- Error recovery and reporting

**Interface**:
```cpp
class Lexer {
public:
    Token nextToken();
    void setFormat(FileFormat format);
    bool hasErrors() const;
};
```

#### Parser (`parser/Parser.h`, `parser/Parser.cpp`)

**Responsibility**: Convert tokens into IR structures

**Key Features**:
- Recursive descent parsing
- Error recovery and diagnostics
- Support for all Fyra IL constructs

**Interface**:
```cpp
class Parser {
public:
    std::unique_ptr<ir::Module> parseModule();
    bool parseFunction(ir::Function* func);
    ir::Instruction* parseInstruction();
};
```

#### IRBuilder (`ir/IRBuilder.h`, `ir/IRBuilder.cpp`)

**Responsibility**: Programmatic IR construction

**Key Features**:
- Fluent API design
- Type safety validation
- Automatic operand management

### IR Core Components

#### Value Hierarchy (`ir/Value.h`, `ir/Value.cpp`)

**Responsibility**: Base class for all IR values

**Key Components**:
- `Value` - Base class for all values
- `User` - Values that use other values
- `Use` - Represents a use of a value
- `Constant` - Compile-time constant values
- `Instruction` - Executable operations

```cpp
class Value {
    Type* type;
    std::string name;
    std::vector<Use*> uses;
public:
    virtual void print(std::ostream& os) const = 0;
};
```

#### Type System (`ir/Type.h`, `ir/Type.cpp`)

**Responsibility**: Type representation and operations

**Key Features**:
- Target-aware sizing
- Type compatibility checking
- Efficient type comparison

**Type Hierarchy**:
```cpp
class Type {
public:
    virtual size_t getSizeInBits(const TargetInfo& target) const = 0;
    virtual size_t getAlignment(const TargetInfo& target) const = 0;
};

class IntegerType : public Type { /* ... */ };
class FloatType : public Type { /* ... */ };
class PointerType : public Type { /* ... */ };
```

#### Module Structure (`ir/Module.h`, `ir/Module.cpp`)

**Responsibility**: Top-level container for IR

**Key Components**:
- Function collection
- Global variable management
- Symbol table maintenance

### Analysis and Transform Components

#### Control Flow Analysis

**CFGBuilder** (`transforms/CFGBuilder.h`, `transforms/CFGBuilder.cpp`)
- Constructs control flow graphs
- Identifies basic blocks and edges
- Handles complex control flow patterns

**DominatorTree** (`transforms/DominatorTree.h`, `transforms/DominatorTree.cpp`)
- Computes dominance relationships
- Efficient dominator tree construction
- Used by many optimization passes

**DominanceFrontier** (`transforms/DominanceFrontier.h`, `transforms/DominanceFrontier.cpp`)
- Calculates dominance frontiers
- Essential for SSA construction
- Phi placement algorithm

#### SSA Construction Pipeline

```
Input IR → CFG Builder → Dominator Tree → Dominance Frontier → Phi Insertion → SSA Renamer → SSA IR
```

**PhiInsertion** (`transforms/PhiInsertion.h`, `transforms/PhiInsertion.cpp`)
- Places phi nodes at join points
- Uses dominance frontiers
- Minimal SSA form construction

**SSARenamer** (`transforms/SSARenamer.h`, `transforms/SSARenamer.cpp`)
- Renames variables for SSA form
- Maintains def-use chains
- Handles complex control flow

#### Optimization Passes

**SCCP** (Sparse Conditional Constant Propagation)
- Advanced constant propagation
- Dead branch elimination
- Lattice-based analysis

**DeadInstructionElimination**
- Removes unused instructions
- Iterative elimination
- Dependency tracking
- Conservative local memory analysis for dead store elimination

**GVN** (Global Value Numbering)
- Common subexpression elimination
- Value numbering algorithm
- Redundancy detection

**Mem2Reg**
- Memory to register promotion
- Stack slot elimination
- Improved optimization opportunities

#### Register Allocation

**LinearScanAllocator** (`transforms/LinearScanAllocator.h`)
- Efficient register allocation
- Spill code generation
- Target-aware register assignment

**LivenessAnalysis** (`transforms/LivenessAnalysis.h`)
- Computes variable lifetimes
- Live-in/live-out analysis
- Interference graph construction

### Backend Components

#### Enhanced CodeGen Framework

**EnhancedCodeGen** (`codegen/EnhancedCodeGen.h`, `codegen/EnhancedCodeGen.cpp`)
- Pattern-based instruction selection
- Multi-target support
- Integrated validation

**Features**:
- Instruction pattern matching
- Target-specific optimizations
- Assembly validation
- Object file generation

#### Target Backends

**SystemV_x64** (`codegen/target/SystemV_x64.h`)
- Linux x86-64 ABI support
- System V calling convention
- Optimized instruction selection
- Unified `emitFunctionPrologue` for consistent stack mapping

**Windows_x64** (`codegen/target/Windows_x64.h`)
- Windows x86-64 (AMD64) ABI support
- Microsoft calling convention
- Shadow store (32 bytes) and stack alignment handling

**Windows_AArch64** (`codegen/target/Windows_AArch64.h`)
- Windows ARM64 ABI support
- AAPCS64-based calling convention with Windows-specific modifications
- Support for PE/COFF metadata

**AArch64** (`codegen/target/AArch64.h`)
- ARM64 architecture support (AAPCS64 ABI)
- Support for up to 64-bit constant loading via `movz`/`movk` sequence
- Optimized NEON instruction support and in-memory relocation

**Wasm32** (`codegen/target/Wasm32.h`)
- WebAssembly target
- Stack-based execution model using a Stackifier algorithm for structured control flow
- Mapping of IR instructions to Wasm locals in `emitFunctionPrologue`

**RiscV64** (`codegen/target/RiscV64.h`)
- RISC-V 64-bit support (LP64D ABI)
- Efficient RISC instruction mapping
- Support for in-memory ELF generation with `R_RISCV_CALL_PLT` and `R_RISCV_GOT_HI20` relocations
- Implementation of callee-saved register preservation (`ra`, `s0`)

#### Validation System

**ASMValidator** (`codegen/validation/ASMValidator.h`)
- Assembly syntax validation
- Target-specific checks
- ABI compliance verification

#### Binary and Executable Generation

**ElfGenerator** (`codegen/execgen/elf.h`)
- Multi-pass ELF executable generation
- Segment alignment to page boundaries (0x1000) for OS protection
- Relocation support for x86_64, AArch64, and RISC-V 64
- Entry point lookup (main, $main, _main)

**TargetABIRegistry** (`codegen/validation/TargetABIRegistry.h`)
- ABI rule database
- Calling convention validation
- Cross-platform compatibility

---

## Data Flow and Processing Pipeline

### Standard Compilation Flow

```
1. Input File (.fyra/.fy)
   ↓
2. Lexer → Tokens
   ↓
3. Parser → Raw IR
   ↓
4. CFG Builder → Control Flow Graph
   ↓
5. SSA Construction → SSA IR
   ↓
6. Optimization Passes → Optimized IR
   ↓
7. Register Allocation → Register-Allocated IR
   ↓
8. Code Generation → Assembly
   ↓
9. ASM Validation → Validated Assembly
   ↓
10. Object Generation → Object File (optional)
```

### Enhanced Pipeline Flow

```
Input → Parser → SSA Builder → Enhanced Optimizations → Register Allocation → Enhanced CodeGen → Validation → Output
```

**Enhanced Optimizations**:
1. SCCP (Constant Propagation)
2. Control Flow Simplification
3. Loop Invariant Code Motion
4. Dead Code Elimination
5. Copy Elimination
6. Global Value Numbering

### Data Structures and Algorithms

#### User/Use Pattern

```cpp
class User : public Value {
    std::vector<Use> operands;
public:
    void addOperand(Value* val);
    void setOperand(unsigned idx, Value* val);
};

class Use {
    Value* val;
    User* user;
public:
    void set(Value* newVal);
};
```

**Benefits**:
- Efficient def-use tracking
- Automatic reference updates
- Memory-safe value replacement

#### SSA Construction Algorithm

```cpp
// Simplified SSA construction
void buildSSA(Function& func) {
    CFGBuilder::run(func);
    DominatorTree domTree;
    domTree.run(func);
    
    DominanceFrontier domFrontier;
    domFrontier.run(func, domTree);
    
    PhiInsertion phiInsert;
    phiInsert.run(func, domFrontier);
    
    SSARenamer renamer;
    renamer.run(func, domTree);
}
```

---

## Core Components

### IR Core Classes

#### Value Hierarchy

```
Value (abstract)
├── User (abstract)
│   ├── Instruction
│   │   ├── BinaryOperator
│   │   ├── UnaryOperator
│   │   ├── TerminatorInst
│   │   └── PhiNode
│   ├── Function
│   └── BasicBlock
├── Constant
│   ├── ConstantInt
│   ├── ConstantFP
│   └── GlobalValue
└── Parameter
```

#### Module Organization

```cpp
class Module {
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<GlobalVariable>> globals;
    std::string name;
    
public:
    Function* getFunction(const std::string& name);
    void addFunction(std::unique_ptr<Function> func);
    void print(std::ostream& os) const;
};
```

### Transform Pass Infrastructure

#### Base Transform Pass

```cpp
class TransformPass {
protected:
    std::shared_ptr<ErrorReporter> errorReporter;
    
public:
    virtual bool run(Function& func) = 0;
    virtual bool runOnModule(Module& module);
    virtual std::string getName() const = 0;
};
```

#### Pass Manager

```cpp
class PassManager {
    std::vector<std::unique_ptr<TransformPass>> passes;
    
public:
    void addPass(std::unique_ptr<TransformPass> pass);
    bool runOnFunction(Function& func);
    void runOptimizationPipeline(Module& module);
};
```

---

## Design Patterns

### 1. Visitor Pattern (Implicit)

Used throughout the codebase for IR traversal:

```cpp
void visitInstruction(Instruction* instr) {
    switch (instr->getOpcode()) {
        case Instruction::Add:
            handleAdd(instr);
            break;
        case Instruction::Load:
            handleLoad(instr);
            break;
        // ... other cases
    }
}
```

### 2. Builder Pattern

IRBuilder provides fluent interface:

```cpp
builder.createFunction("test", IntegerType::get(32))
       .createBasicBlock("entry")
       .setInsertPoint()
       .createAdd(a, b)
       .createRet();
```

### 3. Strategy Pattern

Target-specific code generation:

```cpp
class TargetInfo {
public:
    virtual void emitAdd(CodeGen& cg, std::ostream& os, Instruction& instr) = 0;
    virtual void emitCall(CodeGen& cg, std::ostream& os, Instruction& instr) = 0;
};

class SystemV_x64 : public TargetInfo {
    void emitAdd(CodeGen& cg, std::ostream& os, Instruction& instr) override;
    // ... other methods
};
```

### 4. Factory Pattern

Type system uses factory methods:

```cpp
IntegerType* IntegerType::get(unsigned bitWidth) {
    static std::unordered_map<unsigned, std::unique_ptr<IntegerType>> cache;
    if (cache.find(bitWidth) == cache.end()) {
        cache[bitWidth] = std::make_unique<IntegerType>(bitWidth);
    }
    return cache[bitWidth].get();
}
```

### 5. Observer Pattern (Use/User)

Automatic notification of value changes:

```cpp
void Use::set(Value* newVal) {
    if (val) val->removeUse(this);
    val = newVal;
    if (val) val->addUse(this);
}
```

---

## Target System Integration

### Target Abstraction Layer

```cpp
class TargetInfo {
public:
    // Code generation interface
    virtual void emitPrologue(CodeGen& cg, int stack_size) = 0;
    virtual void emitEpilogue(CodeGen& cg) = 0;
    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) = 0;
    
    // ABI information
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const = 0;
    
    // Target-specific properties
    virtual size_t getPointerSize() const = 0;
    virtual size_t getStackAlignment() const = 0;

    // Helper for stack mapping
    int32_t getStackOffset(const CodeGen& cg, ir::Value* val) const;
};
```

### Multi-Target Compilation

```cpp
class CodeGenPipeline {
public:
    struct PipelineConfig {
        std::vector<std::string> targetPlatforms;
        bool enableValidation;
        bool enableObjectGeneration;
        std::string outputPrefix;
    };
    
    PipelineResult execute(Module& module, const PipelineConfig& config);
};
```

### Cross-Platform Support

- **Calling Conventions**: Automatic ABI handling
- **Register Allocation**: Target-aware register assignment
- **Instruction Selection**: Platform-optimized instruction mapping
- **Object File Generation**: Native object file creation

---

## Extension Points

### Adding New Targets

1. **Implement TargetInfo interface**:
```cpp
class MyTarget : public TargetInfo {
    void emitAdd(CodeGen& cg, std::ostream& os, Instruction& instr) override;
    // ... implement all required methods
};
```

2. **Register target in factory**:
```cpp
TargetRegistry::registerTarget("mytarget", 
    []() { return std::make_unique<MyTarget>(); });
```

3. **Add target-specific optimizations**:
```cpp
class MyTargetOptimizations : public TransformPass {
    bool run(Function& func) override;
};
```

### Adding New Optimization Passes

1. **Inherit from TransformPass**:
```cpp
class MyOptimization : public TransformPass {
public:
    bool run(Function& func) override;
    std::string getName() const override { return "MyOptimization"; }
};
```

2. **Implement analysis logic**:
```cpp
bool MyOptimization::run(Function& func) {
    bool changed = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (canOptimize(instr)) {
                optimize(instr);
                changed = true;
            }
        }
    }
    return changed;
}
```

3. **Register in pass pipeline**:
```cpp
passManager.addPass(std::make_unique<MyOptimization>());
```

### Adding New IR Instructions

1. **Add to Instruction::Opcode enum**:
```cpp
enum Opcode {
    // ... existing opcodes
    MyNewInstruction,
};
```

2. **Implement in IRBuilder**:
```cpp
Instruction* IRBuilder::createMyNew(Value* operand) {
    return createInstruction(Instruction::MyNewInstruction, {operand});
}
```

3. **Add target support**:
```cpp
void TargetInfo::emitMyNew(CodeGen& cg, std::ostream& os, Instruction& instr) {
    // Target-specific implementation
}
```

---

*This architecture enables Fyra to be both powerful and extensible, supporting current needs while allowing for future growth and adaptation to new targets and optimization techniques.*