# The IRBuilder Class

The `ir::IRBuilder` class is a comprehensive helper class that simplifies the process of creating and inserting IR objects (Functions, Basic Blocks, and Instructions) into a Module. It provides a fluent, type-safe interface for building Fyra IR programmatically and is the **recommended way** to construct IR from C++ applications.

The IRBuilder handles the details of object creation, type checking, operand management, and insertion into the correct containers, making it easy to generate correct and optimized IR.

## Overview

Using an `IRBuilder` is the recommended way to construct IR because it:

* **Ensures type safety** - Validates operand types at creation time
* **Manages insertion points** - Automatically inserts instructions in the correct location
* **Provides fluent API** - Clean, readable code for IR construction
* **Handles edge cases** - Proper SSA form maintenance and operand management
* **Supports all instruction types** - Complete coverage of Fyra IL instruction set

## Basic Usage Workflow

A typical workflow for using the `IRBuilder` in a parser or frontend is as follows:

1.  **Initialize the builder** - Create an `IRBuilder` instance
2.  **Set the module** - Associate a `Module` with the builder using `setModule()`
3.  **Create functions** - For each function to be parsed:
    a. Call `createFunction()` to create a new `Function` and add it to the module
    b. **Create basic blocks** - For each basic block within the function:
        i. Call `createBasicBlock()` to create a new `BasicBlock` and add it to the function
        ii. Call `setInsertPoint()` to set the current basic block where new instructions will be added
        iii. **Create instructions** - For each instruction in the block, call the corresponding `create<InstructionName>()` method (e.g., `createAdd`, `createStore`). The builder will automatically insert the new instruction at the current insertion point.

### Quick Start Example

```cpp
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"

void quickStartExample() {
    // 1. Create module and builder
    ir::Module module("example");
    ir::IRBuilder builder;
    builder.setModule(&module);
    
    // 2. Create function
    ir::IntegerType* i32 = ir::IntegerType::get(32);
    ir::Function* func = builder.createFunction("test", i32);
    
    // 3. Create basic block and set insertion point
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);
    
    // 4. Create instructions
    ir::Value* a = ir::ConstantInt::get(i32, 10);
    ir::Value* b = ir::ConstantInt::get(i32, 20);
    ir::Value* sum = builder.createAdd(a, b);
    builder.createRet(sum);
}
```

## Core API Reference

### Setup and Configuration

#### Module and Insertion Point Management

**`void setModule(Module* m)`**
- Sets the current module the builder will be adding functions to
- Must be called before creating any functions
- The module will own all created functions

**`void setInsertPoint(BasicBlock* bb)`**
- Sets the insertion point for new instructions to the end of the specified basic block
- All subsequent instruction creation will insert at this point
- Must be called before creating instructions

**`void setInsertPoint(BasicBlock* bb, BasicBlock::iterator it)`**
- Sets insertion point to a specific position within a basic block
- Allows inserting instructions at any point, not just the end
- Useful for instruction transformation passes

### Function and Basic Block Creation

#### Function Creation

**`Function* createFunction(const std::string& name, Type* returnType)`**
- Creates a new `Function` with the given name and return type
- Automatically adds the function to the current module
- Returns a pointer to the created function for further manipulation

```cpp
// Create different function types
ir::Function* voidFunc = builder.createFunction("helper", ir::VoidType::get());
ir::Function* intFunc = builder.createFunction("compute", ir::IntegerType::get(32));
ir::Function* floatFunc = builder.createFunction("calculate", ir::FloatType::get());
```

#### Basic Block Creation

**`BasicBlock* createBasicBlock(const std::string& name, Function* parent)`**
- Creates a new `BasicBlock` with the given name
- Adds the block to the specified parent function
- Does not set it as the insertion point (use `setInsertPoint()` separately)

```cpp
// Create control flow structure
ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
ir::BasicBlock* thenBlock = builder.createBasicBlock("then", func);
ir::BasicBlock* elseBlock = builder.createBasicBlock("else", func);
ir::BasicBlock* merge = builder.createBasicBlock("merge", func);
```

### Instruction Creation

The builder provides a comprehensive set of methods for creating all Fyra IL instructions. These methods create the instruction and insert it at the current insertion point.

#### Terminator Instructions

Terminator instructions end basic blocks and control program flow.

**`Instruction* createRet(Value* val = nullptr)`**
- Creates a `ret` instruction
- If `val` is provided, returns that value; otherwise creates a void return
- Must be the last instruction in a basic block

**`Instruction* createJmp(Value* target)`**
- Creates an unconditional jump to the target basic block
- Target must be a `BasicBlock*` value
- Transfers control to the target block

**`Instruction* createJnz(Value* cond, Value* targetTrue, Value* targetFalse)`**
- Creates a conditional branch instruction
- Jumps to `targetTrue` if `cond` is non-zero, otherwise to `targetFalse`
- Condition should be an integer value

**`Instruction* createHlt()`**
- Creates a halt instruction that stops program execution
- Used for error conditions or program termination

```cpp
// Example terminator usage
builder.createRet(result);                              // Return value
builder.createRet();                                    // Void return
builder.createJmp(continueBlock);                       // Unconditional jump
builder.createJnz(condition, thenBlock, elseBlock);     // Conditional branch
builder.createHlt();                                    // Program halt
```

#### Arithmetic Instructions

##### Integer Arithmetic

**`Instruction* createAdd(Value* lhs, Value* rhs)`**
- Creates integer addition instruction
- Both operands must have the same integer type
- Result type matches operand types

**`Instruction* createSub(Value* lhs, Value* rhs)`**
- Creates integer subtraction instruction
- Computes `lhs - rhs`

**`Instruction* createMul(Value* lhs, Value* rhs)`**
- Creates integer multiplication instruction
- Both operands must be integers

**`Instruction* createDiv(Value* lhs, Value* rhs)`**
- Creates signed integer division instruction
- Division by zero behavior is undefined

**`Instruction* createUdiv(Value* lhs, Value* rhs)`**
- Creates unsigned integer division instruction
- Treats operands as unsigned values

**`Instruction* createRem(Value* lhs, Value* rhs)`**
- Creates signed remainder (modulo) instruction

**`Instruction* createUrem(Value* lhs, Value* rhs)`**
- Creates unsigned remainder instruction

**`Instruction* createNeg(Value* op)`**
- Creates integer negation instruction
- Computes `-op`

##### Floating-Point Arithmetic

**`Instruction* createFAdd(Value* lhs, Value* rhs)`**
- Creates floating-point addition
- Operands must be float or double type

**`Instruction* createFSub(Value* lhs, Value* rhs)`**
- Creates floating-point subtraction

**`Instruction* createFMul(Value* lhs, Value* rhs)`**
- Creates floating-point multiplication

**`Instruction* createFDiv(Value* lhs, Value* rhs)`**
- Creates floating-point division

```cpp
// Integer arithmetic examples
ir::Value* sum = builder.createAdd(a, b);           // a + b
ir::Value* diff = builder.createSub(x, y);          // x - y
ir::Value* product = builder.createMul(p, q);       // p * q
ir::Value* quotient = builder.createDiv(m, n);      // m / n (signed)
ir::Value* uquotient = builder.createUdiv(m, n);    // m / n (unsigned)
ir::Value* remainder = builder.createRem(a, b);     // a % b
ir::Value* negative = builder.createNeg(val);       // -val

// Floating-point arithmetic examples
ir::Value* fsum = builder.createFAdd(x, y);         // x + y (float)
ir::Value* fdiff = builder.createFSub(a, b);        // a - b (float)
```

##### Bitwise Operations

**`Instruction* createAnd(Value* lhs, Value* rhs)`**
- Creates bitwise AND operation
- Both operands must be integers

**`Instruction* createOr(Value* lhs, Value* rhs)`**
- Creates bitwise OR operation

**`Instruction* createXor(Value* lhs, Value* rhs)`**
- Creates bitwise XOR operation

**`Instruction* createShl(Value* lhs, Value* rhs)`**
- Creates left shift operation
- Shifts `lhs` left by `rhs` positions

**`Instruction* createShr(Value* lhs, Value* rhs)`**
- Creates logical right shift operation
- Zero-fills from the left

**`Instruction* createSar(Value* lhs, Value* rhs)`**
- Creates arithmetic right shift operation
- Sign-extends from the left

```cpp
// Bitwise operation examples
ir::Value* mask = builder.createAnd(flags, 0xFF);       // flags & 0xFF
ir::Value* combined = builder.createOr(a, b);           // a | b
ir::Value* toggled = builder.createXor(val, mask);      // val ^ mask
ir::Value* doubled = builder.createShl(num, 1);         // num << 1
ir::Value* halved = builder.createShr(num, 1);          // num >> 1 (logical)
ir::Value* divided = builder.createSar(num, 2);         // num >> 2 (arithmetic)
```

#### Memory Operations

Memory operations handle stack allocation, loading, and storing values.

##### Stack Allocation

**`Instruction* createAlloc(Type* type)`**
- Creates a stack allocation with default alignment (8 bytes)
- Returns a pointer to the allocated memory
- Memory is automatically deallocated when function returns

**`Instruction* createAlloc4(Type* type)`**
- Creates 4-byte aligned stack allocation
- Suitable for 32-bit values

**`Instruction* createAlloc16(Type* type)`**
- Creates 16-byte aligned stack allocation
- Required for SIMD operations on some targets

##### Load Operations

**`Instruction* createLoad(Value* ptr)`**
- Creates a generic load operation
- Type is inferred from pointer type

**`Instruction* createLoadw(Value* ptr)`**
- Loads a 32-bit word from memory

**`Instruction* createLoadl(Value* ptr)`**
- Loads a 64-bit long from memory

**`Instruction* createLoads(Value* ptr)`**
- Loads a 32-bit float from memory

**`Instruction* createLoadd(Value* ptr)`**
- Loads a 64-bit double from memory

**`Instruction* createLoadub(Value* ptr)`**
- Loads an unsigned byte and zero-extends to word

**`Instruction* createLoadsb(Value* ptr)`**
- Loads a signed byte and sign-extends to word

**`Instruction* createLoaduh(Value* ptr)`**
- Loads an unsigned halfword and zero-extends

**`Instruction* createLoadsh(Value* ptr)`**
- Loads a signed halfword and sign-extends

**`Instruction* createLoaduw(Value* ptr)`**
- Loads an unsigned word and zero-extends to long

##### Store Operations

**`Instruction* createStore(Value* value, Value* ptr)`**
- Creates a generic store operation
- Stores value to the memory location pointed to by ptr

**`Instruction* createStorew(Value* value, Value* ptr)`**
- Stores a 32-bit word to memory

**`Instruction* createStorel(Value* value, Value* ptr)`**
- Stores a 64-bit long to memory

**`Instruction* createStores(Value* value, Value* ptr)`**
- Stores a 32-bit float to memory

**`Instruction* createStored(Value* value, Value* ptr)`**
- Stores a 64-bit double to memory

**`Instruction* createStoreh(Value* value, Value* ptr)`**
- Stores a 16-bit halfword to memory

**`Instruction* createStoreb(Value* value, Value* ptr)`**
- Stores an 8-bit byte to memory

##### Memory Copy

**`Instruction* createBlit(Value* dst, Value* src, Value* count)`**
- Creates a memory copy operation (like memcpy)
- Copies `count` bytes from `src` to `dst`
- Efficient for bulk memory operations

```cpp
// Memory operation examples
ir::Value* ptr = builder.createAlloc(ir::IntegerType::get(32));     // Allocate int
ir::Value* aligned_ptr = builder.createAlloc16(ir::FloatType::get());  // 16-byte aligned

builder.createStore(value, ptr);                    // Store value
ir::Value* loaded = builder.createLoad(ptr);        // Load value

// Type-specific operations
builder.createStorew(word_val, ptr);                // Store 32-bit word
ir::Value* byte_val = builder.createLoadub(ptr);    // Load unsigned byte

// Memory copy
builder.createBlit(dest_ptr, src_ptr, size);        // Copy memory
```

#### Comparison Operations

Comparison operations produce boolean results (represented as integers).

##### Integer Comparisons

**`Instruction* createCeq(Value* lhs, Value* rhs)`**
- Creates equality comparison
- Returns 1 if operands are equal, 0 otherwise

**`Instruction* createCne(Value* lhs, Value* rhs)`**
- Creates inequality comparison

**`Instruction* createCslt(Value* lhs, Value* rhs)`**
- Creates signed less-than comparison

**`Instruction* createCsle(Value* lhs, Value* rhs)`**
- Creates signed less-than-or-equal comparison

**`Instruction* createCsgt(Value* lhs, Value* rhs)`**
- Creates signed greater-than comparison

**`Instruction* createCsge(Value* lhs, Value* rhs)`**
- Creates signed greater-than-or-equal comparison

**`Instruction* createCult(Value* lhs, Value* rhs)`**
- Creates unsigned less-than comparison

**`Instruction* createCule(Value* lhs, Value* rhs)`**
- Creates unsigned less-than-or-equal comparison

**`Instruction* createCugt(Value* lhs, Value* rhs)`**
- Creates unsigned greater-than comparison

**`Instruction* createCuge(Value* lhs, Value* rhs)`**
- Creates unsigned greater-than-or-equal comparison

##### Floating-Point Comparisons

**`Instruction* createCeqf(Value* lhs, Value* rhs)`**
- Creates floating-point equality comparison

**`Instruction* createCnef(Value* lhs, Value* rhs)`**
- Creates floating-point inequality comparison

**`Instruction* createClt(Value* lhs, Value* rhs)`**
- Creates floating-point less-than comparison

**`Instruction* createCle(Value* lhs, Value* rhs)`**
- Creates floating-point less-than-or-equal comparison

**`Instruction* createCgt(Value* lhs, Value* rhs)`**
- Creates floating-point greater-than comparison

**`Instruction* createCge(Value* lhs, Value* rhs)`**
- Creates floating-point greater-than-or-equal comparison

**`Instruction* createCo(Value* lhs, Value* rhs)`**
- Creates ordered comparison (true if neither operand is NaN)

**`Instruction* createCuo(Value* lhs, Value* rhs)`**
- Creates unordered comparison (true if either operand is NaN)

```cpp
// Integer comparison examples
ir::Value* is_equal = builder.createCeq(a, b);          // a == b
ir::Value* is_less = builder.createCslt(x, y);          // x < y (signed)
ir::Value* is_uless = builder.createCult(x, y);         // x < y (unsigned)
ir::Value* is_greater = builder.createCsgt(p, q);       // p > q

// Floating-point comparison examples
ir::Value* f_equal = builder.createCeqf(x, y);          // x == y (float)
ir::Value* f_less = builder.createClt(a, b);            // a < b (float)
ir::Value* is_ordered = builder.createCo(x, y);         // neither is NaN
```

#### Type Conversion Operations

Type conversions change the representation or size of values.

##### Integer Extensions

**`Instruction* createExtUB(Value* val, Type* destTy)`**
- Zero-extends byte to larger integer type

**`Instruction* createExtUH(Value* val, Type* destTy)`**
- Zero-extends halfword to larger integer type

**`Instruction* createExtUW(Value* val, Type* destTy)`**
- Zero-extends word to long

**`Instruction* createExtSB(Value* val, Type* destTy)`**
- Sign-extends byte to larger integer type

**`Instruction* createExtSH(Value* val, Type* destTy)`**
- Sign-extends halfword to larger integer type

**`Instruction* createExtSW(Value* val, Type* destTy)`**
- Sign-extends word to long

##### Floating-Point Conversions

**`Instruction* createExtS(Value* val, Type* destTy)`**
- Extends single precision to double precision

**`Instruction* createTruncD(Value* val, Type* destTy)`**
- Truncates double precision to single precision

##### Integer/Float Conversions

**`Instruction* createSWtoF(Value* val, Type* destTy)`**
- Converts signed word to float

**`Instruction* createUWtoF(Value* val, Type* destTy)`**
- Converts unsigned word to float

**`Instruction* createDToSI(Value* val, Type* destTy)`**
- Converts double to signed integer

**`Instruction* createDToUI(Value* val, Type* destTy)`**
- Converts double to unsigned integer

**`Instruction* createSToSI(Value* val, Type* destTy)`**
- Converts single precision to signed integer

**`Instruction* createSToUI(Value* val, Type* destTy)`**
- Converts single precision to unsigned integer

**`Instruction* createSltof(Value* val, Type* destTy)`**
- Converts signed long to float

**`Instruction* createUltof(Value* val, Type* destTy)`**
- Converts unsigned long to float

##### Bitwise Casting

**`Instruction* createCast(Value* val, Type* destTy)`**
- Performs bitwise reinterpretation
- Source and destination must have same bit width

```cpp
// Type conversion examples
ir::Value* extended = builder.createExtSB(byte_val, ir::IntegerType::get(32));
ir::Value* double_val = builder.createExtS(float_val, ir::DoubleType::get());
ir::Value* int_val = builder.createDToSI(double_val, ir::IntegerType::get(32));
#### Other Essential Operations

##### Function Calls

**`Instruction* createCall(Value* callee, const std::vector<Value*>& args, Type* retType = nullptr)`**
- Creates a function call instruction
- `callee` should be a function value or function pointer
- `args` contains the arguments to pass
- `retType` specifies return type (nullptr for void functions)

```cpp
// Function call examples
std::vector<ir::Value*> args = {arg1, arg2, arg3};
ir::Value* result = builder.createCall(function, args, ir::IntegerType::get(32));
builder.createCall(void_function, {arg1});  // Void function call
```

##### SSA Phi Nodes

**`PhiNode* createPhi(Type* type, unsigned numOperands, Instruction* alloc)`**
- Creates a phi node for SSA form
- Used to merge values from different control flow paths
- Must be added to with `addPhiIncoming()` after creation

```cpp
// Phi node example
ir::PhiNode* phi = builder.createPhi(ir::IntegerType::get(32), 2, nullptr);
builder.addPhiIncoming(phi, value1, block1);
builder.addPhiIncoming(phi, value2, block2);
```

##### Value Copy

**`Instruction* createCopy(Value* operand)`**
- Creates a copy instruction
- Useful for explicit value copying
- Often optimized away by later passes

##### Variadic Function Support

**`Instruction* createVAStart(Value* val)`**
- Initializes variadic argument processing
- Used in functions with `...` parameters

**`Instruction* createVAArg(Value* val, Type* destTy)`**
- Extracts next argument from variadic argument list
- Returns value of specified type

```cpp
// Variadic function implementation
builder.createVAStart(ap_list);
ir::Value* next_arg = builder.createVAArg(ap_list, ir::IntegerType::get(32));
```

---

## Advanced Usage Patterns

### Building Control Flow

#### If-Then-Else Pattern

```cpp
void buildIfThenElse(ir::IRBuilder& builder, ir::Function* func,
                     ir::Value* condition, ir::Value* trueVal, ir::Value* falseVal) {
    // Create blocks
    ir::BasicBlock* thenBlock = builder.createBasicBlock("then", func);
    ir::BasicBlock* elseBlock = builder.createBasicBlock("else", func);
    ir::BasicBlock* mergeBlock = builder.createBasicBlock("merge", func);
    
    // Conditional branch
    builder.createJnz(condition, thenBlock, elseBlock);
    
    // Then block
    builder.setInsertPoint(thenBlock);
    builder.createJmp(mergeBlock);
    
    // Else block
    builder.setInsertPoint(elseBlock);
    builder.createJmp(mergeBlock);
    
    // Merge block with phi
    builder.setInsertPoint(mergeBlock);
    ir::PhiNode* result = builder.createPhi(trueVal->getType(), 2, nullptr);
    builder.addPhiIncoming(result, trueVal, thenBlock);
    builder.addPhiIncoming(result, falseVal, elseBlock);
}
```

#### Loop Pattern

```cpp
void buildLoop(ir::IRBuilder& builder, ir::Function* func,
               ir::Value* initial, ir::Value* limit) {
    ir::BasicBlock* loopHeader = builder.createBasicBlock("loop.header", func);
    ir::BasicBlock* loopBody = builder.createBasicBlock("loop.body", func);
    ir::BasicBlock* loopExit = builder.createBasicBlock("loop.exit", func);
    
    // Jump to loop header
    builder.createJmp(loopHeader);
    
    // Loop header with phi
    builder.setInsertPoint(loopHeader);
    ir::PhiNode* counter = builder.createPhi(initial->getType(), 2, nullptr);
    builder.addPhiIncoming(counter, initial, builder.getCurrentBlock());
    
    // Loop condition
    ir::Value* cond = builder.createCslt(counter, limit);
    builder.createJnz(cond, loopBody, loopExit);
    
    // Loop body
    builder.setInsertPoint(loopBody);
    ir::Value* nextCounter = builder.createAdd(counter, 
        ir::ConstantInt::get(counter->getType(), 1));
    builder.addPhiIncoming(counter, nextCounter, loopBody);
    builder.createJmp(loopHeader);
    
    // Loop exit
    builder.setInsertPoint(loopExit);
}
```

### Error Handling and Best Practices

#### Type Safety Checks

```cpp
ir::Value* safeCreateAdd(ir::IRBuilder& builder, ir::Value* lhs, ir::Value* rhs) {
    // Verify types match
    if (lhs->getType() != rhs->getType()) {
        throw std::runtime_error("Type mismatch in addition");
    }
    
    // Verify they're numeric types
    if (!lhs->getType()->isIntegerTy() && !lhs->getType()->isFloatTy()) {
        throw std::runtime_error("Addition requires numeric types");
    }
    
    return builder.createAdd(lhs, rhs);
}
```

#### Insertion Point Management

```cpp
class InsertionPointGuard {
    ir::IRBuilder& builder;
    ir::BasicBlock* savedBlock;
    ir::BasicBlock::iterator savedIterator;
    
public:
    InsertionPointGuard(ir::IRBuilder& b) : builder(b) {
        savedBlock = builder.getCurrentBlock();
        savedIterator = builder.getCurrentIterator();
    }
    
    ~InsertionPointGuard() {
        if (savedBlock) {
            builder.setInsertPoint(savedBlock, savedIterator);
        }
    }
};

void temporaryInsertion(ir::IRBuilder& builder, ir::BasicBlock* tempBlock) {
    InsertionPointGuard guard(builder);  // Saves current position
    builder.setInsertPoint(tempBlock);
    // ... create instructions in temporary location ...
    // Guard destructor automatically restores original position
}
```

### Integration with Optimization Passes

```cpp
void buildOptimizedFunction(ir::Module& module) {
    ir::IRBuilder builder;
    builder.setModule(&module);
    
    // Build IR
    ir::Function* func = builder.createFunction("optimized", ir::IntegerType::get(32));
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);
    
    // ... build function body ...
    
    // Apply optimization passes
    transforms::CFGBuilder::run(*func);
    
    transforms::DominatorTree domTree;
    domTree.run(*func);
    
    transforms::SCCP sccp(std::make_shared<transforms::ErrorReporter>(std::cerr, false));
    sccp.run(*func);
    
    transforms::DeadInstructionElimination dce(
        std::make_shared<transforms::ErrorReporter>(std::cerr, false));
    dce.run(*func);
}
```

---

## Complete Example: Building a Factorial Function

```cpp
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"

ir::Function* buildFactorialFunction(ir::Module& module) {
    ir::IRBuilder builder;
    builder.setModule(&module);
    
    // Create function: int factorial(int n)
    ir::IntegerType* i32 = ir::IntegerType::get(32);
    ir::Function* factorial = builder.createFunction("factorial", i32);
    
    // Add parameter
    ir::Parameter* n = new ir::Parameter(i32, "n");
    factorial->addParameter(n);
    
    // Create basic blocks
    ir::BasicBlock* entry = builder.createBasicBlock("entry", factorial);
    ir::BasicBlock* baseCase = builder.createBasicBlock("base_case", factorial);
    ir::BasicBlock* recursive = builder.createBasicBlock("recursive", factorial);
    
    // Entry block: check if n <= 1
    builder.setInsertPoint(entry);
    ir::Value* one = ir::ConstantInt::get(i32, 1);
    ir::Value* isBase = builder.createCsle(n, one);
    builder.createJnz(isBase, baseCase, recursive);
    
    // Base case: return 1
    builder.setInsertPoint(baseCase);
    builder.createRet(one);
    
    // Recursive case: return n * factorial(n-1)
    builder.setInsertPoint(recursive);
    ir::Value* nMinus1 = builder.createSub(n, one);
    
    std::vector<ir::Value*> args = {nMinus1};
    ir::Value* recursiveResult = builder.createCall(factorial, args, i32);
    
    ir::Value* result = builder.createMul(n, recursiveResult);
    builder.createRet(result);
    
    return factorial;
}
```

---

*For more examples and advanced techniques, see the [Fyra IL Specification](fyra_il.md) and [API Reference](api_reference.md).*
