# Fyra Compiler API Reference

This document provides comprehensive API documentation for the Fyra compiler library, covering all major classes and interfaces for programmatic IR construction and manipulation.

## Table of Contents

1. [Core IR Classes](#core-ir-classes)
2. [IRBuilder API](#irbuilder-api)
3. [Type System](#type-system)
4. [Transform Passes](#transform-passes)
5. [Code Generation](#code-generation)
6. [Usage Examples](#usage-examples)

---

## Core IR Classes

### ir::Value

Base class for all values in Fyra IR.

```cpp
class Value {
public:
    Type* getType() const;
    const std::string& getName() const;
    void setName(const std::string& name);
    
    // Use management
    const std::vector<Use*>& getUses() const;
    bool hasOneUse() const;
    void replaceAllUsesWith(Value* newValue);
    
    virtual void print(std::ostream& os) const = 0;
};
```

### ir::User

Represents values that use other values.

```cpp
class User : public Value {
public:
    unsigned getNumOperands() const;
    Value* getOperand(unsigned idx) const;
    void setOperand(unsigned idx, Value* val);
    
    const std::vector<Use>& getOperands() const;
};
```

### ir::Instruction

Base class for all instructions.

```cpp
class Instruction : public User {
public:
    enum Opcode {
        Add, Sub, Mul, Div, Udiv, Rem, Urem,
        FAdd, FSub, FMul, FDiv,
        And, Or, Xor, Shl, Shr, Sar, Neg,
        Load, Store, Alloc, Blit,
        Ceq, Cne, Cslt, Csle, Csgt, Csge,
        Cult, Cule, Cugt, Cuge,
        Ret, Jmp, Jnz, Hlt, Call, Phi, Copy,
        // ... conversion instructions
    };
    
    Opcode getOpcode() const;
    BasicBlock* getParent() const;
    void setParent(BasicBlock* bb);
    
    bool isTerminator() const;
    bool isBinaryOp() const;
    bool isComparison() const;
};
```

### ir::BasicBlock

Container for instructions.

```cpp
class BasicBlock : public Value {
public:
    typedef std::vector<std::unique_ptr<Instruction>>::iterator iterator;
    
    Function* getParent() const;
    
    // Instruction management
    void addInstruction(std::unique_ptr<Instruction> instr);
    iterator begin();
    iterator end();
    
    // Control flow
    std::vector<BasicBlock*> getPredecessors() const;
    std::vector<BasicBlock*> getSuccessors() const;
};
```

### ir::Function

Represents a function.

```cpp
class Function : public Value {
public:
    Function(const std::string& name, Type* returnType);
    
    Type* getReturnType() const;
    
    // Parameters
    void addParameter(std::unique_ptr<Parameter> param);
    const std::vector<std::unique_ptr<Parameter>>& getParameters() const;
    
    // Basic blocks
    void addBasicBlock(std::unique_ptr<BasicBlock> bb);
    const std::vector<std::unique_ptr<BasicBlock>>& getBasicBlocks() const;
    
    // Utility
    BasicBlock* getEntryBlock() const;
    bool isDeclaration() const;
};
```

### ir::Module

Top-level container.

```cpp
class Module {
public:
    Module(const std::string& name);
    
    // Functions
    void addFunction(std::unique_ptr<Function> func);
    Function* getFunction(const std::string& name) const;
    const std::vector<std::unique_ptr<Function>>& getFunctions() const;
    
    // Global variables
    void addGlobalVariable(std::unique_ptr<GlobalVariable> global);
    
    void print(std::ostream& os) const;
};
```

---

## IRBuilder API

### Core Builder Interface

```cpp
class IRBuilder {
public:
    // Setup
    void setModule(Module* module);
    void setInsertPoint(BasicBlock* bb);
    void setInsertPoint(BasicBlock* bb, BasicBlock::iterator it);
    
    // Function/Block creation
    Function* createFunction(const std::string& name, Type* returnType);
    BasicBlock* createBasicBlock(const std::string& name, Function* parent);
    
    // Terminator instructions
    Instruction* createRet(Value* val = nullptr);
    Instruction* createJmp(Value* target);
    Instruction* createJnz(Value* cond, Value* targetTrue, Value* targetFalse);
    Instruction* createHlt();
    
    // Arithmetic instructions
    Instruction* createAdd(Value* lhs, Value* rhs);
    Instruction* createSub(Value* lhs, Value* rhs);
    Instruction* createMul(Value* lhs, Value* rhs);
    Instruction* createDiv(Value* lhs, Value* rhs);
    Instruction* createUdiv(Value* lhs, Value* rhs);
    Instruction* createNeg(Value* op);
    
    // Floating-point arithmetic
    Instruction* createFAdd(Value* lhs, Value* rhs);
    Instruction* createFSub(Value* lhs, Value* rhs);
    Instruction* createFMul(Value* lhs, Value* rhs);
    Instruction* createFDiv(Value* lhs, Value* rhs);
    
    // Memory operations
    Instruction* createAlloc(Type* type);
    Instruction* createLoad(Value* ptr);
    Instruction* createStore(Value* value, Value* ptr);
    
    // Comparisons
    Instruction* createCeq(Value* lhs, Value* rhs);
    Instruction* createCslt(Value* lhs, Value* rhs);
    // ... other comparison methods
    
    // Function calls
    Instruction* createCall(Value* callee, const std::vector<Value*>& args, Type* retType = nullptr);
    
    // SSA
    PhiNode* createPhi(Type* type, unsigned numOperands, Instruction* alloc);
};
```

---

## Type System

### ir::Type

Base type class.

```cpp
class Type {
public:
    virtual size_t getSizeInBits(const TargetInfo& target) const = 0;
    virtual size_t getAlignment(const TargetInfo& target) const = 0;
    
    bool isIntegerTy() const;
    bool isFloatTy() const;
    bool isDoubleTy() const;
    bool isPointerTy() const;
    bool isVoidTy() const;
};
```

### Concrete Types

```cpp
class IntegerType : public Type {
public:
    static IntegerType* get(unsigned bitWidth);
    unsigned getBitWidth() const;
};

class FloatType : public Type {
public:
    static FloatType* get();
};

class DoubleType : public Type {
public:
    static DoubleType* get();
};

class PointerType : public Type {
public:
    static PointerType* get(Type* elementType);
    Type* getElementType() const;
};

class VoidType : public Type {
public:
    static VoidType* get();
};
```

---

## Transform Passes

### Base Transform Pass

```cpp
class TransformPass {
protected:
    std::shared_ptr<ErrorReporter> errorReporter;
    
public:
    virtual bool run(Function& func) = 0;
    virtual std::string getName() const = 0;
    
    bool hasErrors() const;
    bool hasWarnings() const;
};
```

### Key Transform Passes

```cpp
// SSA Construction
class CFGBuilder {
public:
    static void run(Function& func);
};

class DominatorTree : public TransformPass {
public:
    bool run(Function& func) override;
    // ... dominator tree queries
};

// Optimizations
class SCCP : public TransformPass {
public:
    SCCP(std::shared_ptr<ErrorReporter> reporter);
    bool run(Function& func) override;
};

class DeadInstructionElimination : public TransformPass {
public:
    bool run(Function& func) override;
};

// Register Allocation
class LinearScanAllocator : public TransformPass {
public:
    bool run(Function& func) override;
};
```

---

## Code Generation

### Enhanced CodeGen

```cpp
class EnhancedCodeGen {
public:
    struct CompilationResult {
        bool success;
        std::string assemblyPath;
        std::string objectPath;
        ValidationResult validation;
        double totalTimeMs;
    };
    
    static std::unique_ptr<EnhancedCodeGen> create(Module& module, const std::string& target);
    
    CompilationResult compileToObject(const std::string& outputPrefix, 
                                     bool enableValidation = true,
                                     bool generateObject = false);
    
    void enableVerboseOutput(bool enable);
};

// Factory
class EnhancedCodeGenFactory {
public:
    static std::unique_ptr<EnhancedCodeGen> create(Module& module, const std::string& target);
};
```

### Target Information

```cpp
class TargetInfo {
public:
    virtual void emitAdd(CodeGen& cg, std::ostream& os, Instruction& instr) const = 0;
    virtual void emitCall(CodeGen& cg, std::ostream& os, Instruction& instr) const = 0;
    // ... other emit methods
    
    virtual CallingConvention getCallingConvention() const = 0;
    virtual size_t getPointerSize() const = 0;
};
```

---

## Usage Examples

### Basic IR Construction

```cpp
#include "ir/Module.h"
#include "ir/IRBuilder.h"

void basicExample() {
    // Create module and builder
    ir::Module module("example");
    ir::IRBuilder builder;
    builder.setModule(&module);
    
    // Create function
    ir::IntegerType* i32 = ir::IntegerType::get(32);
    ir::Function* func = builder.createFunction("add", i32);
    
    // Add parameters
    auto param1 = std::make_unique<ir::Parameter>(i32, "a");
    auto param2 = std::make_unique<ir::Parameter>(i32, "b");
    func->addParameter(std::move(param1));
    func->addParameter(std::move(param2));
    
    // Create basic block
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);
    
    // Create instructions
    ir::Value* sum = builder.createAdd(func->getParameters()[0].get(),
                                      func->getParameters()[1].get());
    builder.createRet(sum);
}
```

### Optimization Pipeline

```cpp
void optimizeFunction(ir::Function& func) {
    auto errorReporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    
    // SSA Construction
    transforms::CFGBuilder::run(func);
    transforms::DominatorTree domTree;
    domTree.run(func);
    
    // Optimizations
    transforms::SCCP sccp(errorReporter);
    sccp.run(func);
    
    transforms::DeadInstructionElimination dce(errorReporter);
    dce.run(func);
    
    // Register allocation
    transforms::LinearScanAllocator regAlloc;
    regAlloc.run(func);
}
```

### Code Generation

```cpp
void generateCode(ir::Module& module) {
    // Enhanced code generation
    auto codeGen = codegen::EnhancedCodeGenFactory::create(module, "linux");
    codeGen->enableVerboseOutput(true);
    
    auto result = codeGen->compileToObject("output", true, true);
    
    if (result.success) {
        std::cout << "Assembly: " << result.assemblyPath << std::endl;
        std::cout << "Object: " << result.objectPath << std::endl;
    }
}
```

---

*For complete examples and advanced usage, see the [IRBuilder Guide](ir_builder.md) and [Architecture Documentation](architecture.md).*