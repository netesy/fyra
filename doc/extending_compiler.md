# Extending the Fyra Compiler

This guide provides comprehensive instructions for extending the Fyra compiler with new targets, optimization passes, and IR features.

## Table of Contents

1. [Adding New Target Architectures](#adding-new-target-architectures)
2. [Creating Custom Optimization Passes](#creating-custom-optimization-passes)
3. [Extending the IR Instruction Set](#extending-the-ir-instruction-set)
4. [Adding Analysis Passes](#adding-analysis-passes)
5. [Contributing Guidelines](#contributing-guidelines)

---

## Adding New Target Architectures

### Step 1: Implement TargetInfo Interface

Create a new target class inheriting from `TargetInfo`:

```cpp
// include/codegen/target/MyTarget.h
#pragma once
#include "TargetInfo.h"

namespace codegen {
namespace target {

class MyTarget : public TargetInfo {
public:
    // Basic target information
    std::string getName() const override { return "mytarget"; }
    size_t getPointerSize() const override { return 8; } // 64-bit
    size_t getStackAlignment() const override { return 16; }
    
    // Instruction emission
    void emitAdd(CodeGen& cg, std::ostream& os, ir::Instruction& instr) const override;
    void emitSub(CodeGen& cg, std::ostream& os, ir::Instruction& instr) const override;
    void emitMul(CodeGen& cg, std::ostream& os, ir::Instruction& instr) const override;
    // ... implement all required methods
    
    // ABI and calling convention
    CallingConvention getCallingConvention() const override;
    std::vector<Register> getAvailableRegisters() const override;
    
    void emitPrologue(CodeGen& cg, std::ostream& os, ir::Function& func) const override;
    void emitEpilogue(CodeGen& cg, std::ostream& os, ir::Function& func) const override;
};

} // namespace target
} // namespace codegen
```

### Step 2: Implement Target-Specific Code Generation

```cpp
// src/codegen/target/MyTarget.cpp
#include "codegen/target/MyTarget.h"
#include "codegen/CodeGen.h"

namespace codegen {
namespace target {

void MyTarget::emitAdd(CodeGen& cg, std::ostream& os, ir::Instruction& instr) const {
    // Get operands
    ir::Value* lhs = instr.getOperand(0);
    ir::Value* rhs = instr.getOperand(1);
    
    // Generate target-specific assembly
    std::string dest = cg.getRegisterForValue(&instr);
    std::string src1 = cg.getValueAsOperand(lhs);
    std::string src2 = cg.getValueAsOperand(rhs);
    
    os << "    add " << dest << ", " << src1 << ", " << src2 << "\n";
}

void MyTarget::emitPrologue(CodeGen& cg, std::ostream& os, ir::Function& func) const {
    os << func.getName() << ":\n";
    os << "    ; Function prologue\n";
    os << "    push bp\n";
    os << "    mov bp, sp\n";
    
    // Allocate stack space for locals
    size_t stackSize = cg.getStackFrameSize();
    if (stackSize > 0) {
        os << "    sub sp, " << stackSize << "\n";
    }
}

CallingConvention MyTarget::getCallingConvention() const {
    return CallingConvention::MyTargetCC;
}

} // namespace target
} // namespace codegen
```

### Step 3: Register the Target

```cpp
// In target registration file or factory
void registerMyTarget() {
    TargetRegistry::registerTarget("mytarget", []() {
        return std::make_unique<MyTarget>();
    });
}
```

### Step 4: Add Target-Specific Optimizations

```cpp
class MyTargetOptimizations : public transforms::TransformPass {
public:
    bool run(ir::Function& func) override {
        bool changed = false;
        
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (canOptimizeForMyTarget(*instr)) {
                    optimizeInstruction(*instr);
                    changed = true;
                }
            }
        }
        
        return changed;
    }
    
    std::string getName() const override { return "MyTargetOptimizations"; }

private:
    bool canOptimizeForMyTarget(const ir::Instruction& instr) {
        // Target-specific optimization detection
        return instr.getOpcode() == ir::Instruction::Add && 
               /* target-specific conditions */;
    }
    
    void optimizeInstruction(ir::Instruction& instr) {
        // Perform target-specific optimization
    }
};
```

---

## Creating Custom Optimization Passes

### Step 1: Inherit from TransformPass

```cpp
// include/transforms/MyOptimization.h
#pragma once
#include "TransformPass.h"

namespace transforms {

class MyOptimization : public TransformPass {
public:
    MyOptimization(std::shared_ptr<ErrorReporter> reporter);
    
    bool run(ir::Function& func) override;
    std::string getName() const override { return "MyOptimization"; }

private:
    bool analyzeFunction(ir::Function& func);
    bool optimizeBasicBlock(ir::BasicBlock& bb);
    bool optimizeInstruction(ir::Instruction& instr);
    
    // Analysis data structures
    std::unordered_set<ir::Instruction*> candidateInstructions;
    std::unordered_map<ir::Value*, int> valueScores;
};

} // namespace transforms
```

### Step 2: Implement the Optimization Logic

```cpp
// src/transforms/MyOptimization.cpp
#include "transforms/MyOptimization.h"

namespace transforms {

MyOptimization::MyOptimization(std::shared_ptr<ErrorReporter> reporter)
    : TransformPass(reporter) {}

bool MyOptimization::run(ir::Function& func) {
    if (!analyzeFunction(func)) {
        return false;
    }
    
    bool changed = false;
    
    for (auto& bb : func.getBasicBlocks()) {
        if (optimizeBasicBlock(*bb)) {
            changed = true;
        }
    }
    
    return changed;
}

bool MyOptimization::analyzeFunction(ir::Function& func) {
    candidateInstructions.clear();
    valueScores.clear();
    
    // Perform analysis to identify optimization opportunities
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (isOptimizationCandidate(*instr)) {
                candidateInstructions.insert(instr.get());
                computeValueScore(*instr);
            }
        }
    }
    
    return !candidateInstructions.empty();
}

bool MyOptimization::optimizeBasicBlock(ir::BasicBlock& bb) {
    bool changed = false;
    
    for (auto it = bb.begin(); it != bb.end(); ++it) {
        ir::Instruction& instr = **it;
        
        if (candidateInstructions.count(&instr) && optimizeInstruction(instr)) {
            changed = true;
            
            // Report the optimization
            if (errorReporter->isVerbose()) {
                errorReporter->reportInfo("MyOptimization", 
                    "Optimized instruction: " + instr.getName());
            }
        }
    }
    
    return changed;
}

bool MyOptimization::optimizeInstruction(ir::Instruction& instr) {
    // Implement specific optimization transformation
    switch (instr.getOpcode()) {
        case ir::Instruction::Add:
            return optimizeAddition(instr);
        case ir::Instruction::Mul:
            return optimizeMultiplication(instr);
        default:
            return false;
    }
}

} // namespace transforms
```

### Step 3: Register in Pass Pipeline

```cpp
void addMyOptimizationToPass(transforms::PassManager& passManager,
                            std::shared_ptr<transforms::ErrorReporter> reporter) {
    passManager.addPass(std::make_unique<transforms::MyOptimization>(reporter));
}
```

---

## Extending the IR Instruction Set

### Step 1: Add New Opcode

```cpp
// In include/ir/Instruction.h
enum Opcode {
    // ... existing opcodes
    MyNewInstruction,
    AnotherNewInstruction,
};
```

### Step 2: Implement IRBuilder Support

```cpp
// In include/ir/IRBuilder.h
class IRBuilder {
public:
    // ... existing methods
    Instruction* createMyNew(Value* operand1, Value* operand2);
    Instruction* createAnotherNew(Value* operand, Type* destType);
};

// In src/ir/IRBuilder.cpp
Instruction* IRBuilder::createMyNew(Value* operand1, Value* operand2) {
    // Type checking
    if (operand1->getType() != operand2->getType()) {
        throw std::runtime_error("Type mismatch in MyNew instruction");
    }
    
    // Create instruction
    auto instr = std::make_unique<Instruction>(
        operand1->getType(), 
        Instruction::MyNewInstruction,
        std::vector<Value*>{operand1, operand2}
    );
    
    // Insert at current insertion point
    return insertInstruction(std::move(instr));
}
```

### Step 3: Add Parser Support

```cpp
// In src/parser/Parser.cpp, in parseInstruction()
if (opcodeStr == "mynew") {
    ir::Value* op1 = parseValue();
    expectToken(Token::Comma);
    ir::Value* op2 = parseValue();
    instr = builder.createMyNew(op1, op2);
} else if (opcodeStr == "anothernew") {
    ir::Value* op = parseValue();
    expectToken(Token::Comma);
    ir::Type* destType = parseType();
    instr = builder.createAnotherNew(op, destType);
}
```

### Step 4: Add Target Support

```cpp
// In each target's implementation
void SystemV_x64::emitMyNew(CodeGen& cg, std::ostream& os, ir::Instruction& instr) const {
    // Generate x86-64 specific code for MyNew instruction
    std::string dest = cg.getRegisterForValue(&instr);
    std::string src1 = cg.getValueAsOperand(instr.getOperand(0));
    std::string src2 = cg.getValueAsOperand(instr.getOperand(1));
    
    os << "    ; MyNew instruction\n";
    os << "    mov " << dest << ", " << src1 << "\n";
    os << "    mynew_op " << dest << ", " << src2 << "\n";
}
```

### Step 5: Add to Optimization Passes

```cpp
// Update optimization passes to handle new instructions
void SCCP::visit(ir::Instruction* instr) {
    switch (instr->getOpcode()) {
        // ... existing cases
        case ir::Instruction::MyNewInstruction:
            handleMyNewInstruction(instr);
            break;
        case ir::Instruction::AnotherNewInstruction:
            handleAnotherNewInstruction(instr);
            break;
    }
}
```

---

## Adding Analysis Passes

### Step 1: Create Analysis Pass

```cpp
// include/transforms/MyAnalysis.h
#pragma once
#include "TransformPass.h"

namespace transforms {

class MyAnalysis : public TransformPass {
public:
    struct AnalysisResult {
        std::unordered_set<ir::Value*> importantValues;
        std::unordered_map<ir::BasicBlock*, int> blockScores;
        std::vector<ir::Instruction*> hotInstructions;
    };
    
    MyAnalysis(std::shared_ptr<ErrorReporter> reporter);
    
    bool run(ir::Function& func) override;
    std::string getName() const override { return "MyAnalysis"; }
    
    const AnalysisResult& getResult() const { return result; }

private:
    AnalysisResult result;
    
    void analyzeDataFlow(ir::Function& func);
    void analyzeControlFlow(ir::Function& func);
    void computeScores(ir::Function& func);
};

} // namespace transforms
```

### Step 2: Implement Analysis Logic

```cpp
// src/transforms/MyAnalysis.cpp
bool MyAnalysis::run(ir::Function& func) {
    result = AnalysisResult{};
    
    analyzeDataFlow(func);
    analyzeControlFlow(func);
    computeScores(func);
    
    return true; // Analysis passes typically don't modify IR
}

void MyAnalysis::analyzeDataFlow(ir::Function& func) {
    // Implement data flow analysis
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            // Analyze instruction's data dependencies
            if (hasInterestingDataFlow(*instr)) {
                result.importantValues.insert(instr.get());
            }
        }
    }
}
```

### Step 3: Use Analysis in Other Passes

```cpp
class OptimizationUsingMyAnalysis : public TransformPass {
private:
    std::unique_ptr<MyAnalysis> analysis;
    
public:
    bool run(ir::Function& func) override {
        // Run analysis first
        analysis = std::make_unique<MyAnalysis>(errorReporter);
        analysis->run(func);
        
        const auto& result = analysis->getResult();
        
        // Use analysis results for optimization
        for (ir::Value* importantValue : result.importantValues) {
            optimizeImportantValue(importantValue);
        }
        
        return true;
    }
};
```

---

## Contributing Guidelines

### Code Style

1. **Follow existing conventions**:
   - Use 4-space indentation
   - Class names in PascalCase
   - Method names in camelCase
   - Member variables with no prefix

2. **Header organization**:
```cpp
#pragma once

// System includes
#include <memory>
#include <vector>

// Project includes
#include "Base.h"
#include "Other.h"

namespace myproject {

class MyClass {
public:
    // Public methods
    
private:
    // Private members
};

} // namespace myproject
```

### Testing Requirements

1. **Unit tests for new features**:
```cpp
// tests/test_my_feature.cpp
#include "transforms/MyOptimization.h"
#include <gtest/gtest.h>

TEST(MyOptimizationTest, BasicOptimization) {
    ir::Module module("test");
    ir::IRBuilder builder;
    // ... set up test case
    
    transforms::MyOptimization opt(errorReporter);
    bool changed = opt.run(*func);
    
    EXPECT_TRUE(changed);
    // ... verify results
}
```

2. **Integration tests**:
```cpp
TEST(IntegrationTest, MyFeatureEndToEnd) {
    // Test complete compilation pipeline with new feature
}
```

### Documentation Requirements

1. **API documentation**:
```cpp
/**
 * @brief Performs my custom optimization on a function.
 * 
 * This optimization identifies patterns X and transforms them to Y,
 * resulting in improved performance for Z workloads.
 * 
 * @param func The function to optimize
 * @return true if any changes were made, false otherwise
 */
bool run(ir::Function& func) override;
```

2. **User documentation**:
   - Update relevant `.md` files
   - Add examples of new features
   - Document command-line options

### Submission Process

1. **Create feature branch**:
```bash
git checkout -b feature/my-new-feature
```

2. **Implement changes with tests**
3. **Update documentation**
4. **Run full test suite**:
```bash
make test
ctest --output-on-failure
```

5. **Submit pull request** with:
   - Clear description of changes
   - Test results
   - Performance impact analysis
   - Documentation updates

### Performance Considerations

1. **Benchmark new features**:
```cpp
// Use timing for performance-critical code
auto start = std::chrono::high_resolution_clock::now();
// ... code to measure
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
```

2. **Memory usage analysis**
3. **Compilation time impact**
4. **Generated code quality**

---

*This guide provides the foundation for extending Fyra. For specific questions, consult the existing codebase or reach out to the development team.*