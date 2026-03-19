#pragma once

#include "Module.h"
#include "Function.h"
#include "BasicBlock.h"
#include "Instruction.h"
#include "Value.h"
#include "Type.h"
#include <memory>

#include "IRContext.h"

namespace ir {

class IRBuilder {
public:
    IRBuilder(std::shared_ptr<IRContext> ctx);

    void setModule(Module* m);
    void setInsertPoint(BasicBlock* bb);
    void setInsertPoint(BasicBlock* bb, BasicBlock::instr_iterator it);
    BasicBlock* getInsertPoint() const { return insertPoint; }

    // --- Create methods for top-level objects ---
    Function* createFunction(const std::string& name, Type* returnType);
    Function* createFunction(const std::string& name, Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic = false);
    BasicBlock* createBasicBlock(const std::string& name, Function* parent);

    // --- Create methods for instructions ---
    // Each of these will create an instruction and insert it at the
    // current insertion point. They return the new instruction.
    Instruction* createRet(Value* val);
    Instruction* createAdd(Value* lhs, Value* rhs);
    Instruction* createAdd(Value* lhs, Value* rhs, Type* resultType); // Explicit type override
    Instruction* createSub(Value* lhs, Value* rhs);
    Instruction* createSub(Value* lhs, Value* rhs, Type* resultType); // Explicit type override
    Instruction* createMul(Value* lhs, Value* rhs);
    Instruction* createMul(Value* lhs, Value* rhs, Type* resultType); // Explicit type override
    Instruction* createDiv(Value* lhs, Value* rhs);
    Instruction* createDiv(Value* lhs, Value* rhs, Type* resultType); // Explicit type override
    Instruction* createUdiv(Value* lhs, Value* rhs);
    Instruction* createUdiv(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createRem(Value* lhs, Value* rhs);
    Instruction* createRem(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createUrem(Value* lhs, Value* rhs);
    Instruction* createUrem(Value* lhs, Value* rhs, Type* resultType);
    // Floating-point arithmetic
    Instruction* createFAdd(Value* lhs, Value* rhs);
    Instruction* createFSub(Value* lhs, Value* rhs);
    Instruction* createFMul(Value* lhs, Value* rhs);
    Instruction* createFDiv(Value* lhs, Value* rhs);
    Instruction* createAnd(Value* lhs, Value* rhs);
    Instruction* createAnd(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createOr(Value* lhs, Value* rhs);
    Instruction* createOr(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createXor(Value* lhs, Value* rhs);
    Instruction* createXor(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createShl(Value* lhs, Value* rhs);
    Instruction* createShl(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createShr(Value* lhs, Value* rhs);
    Instruction* createShr(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createSar(Value* lhs, Value* rhs);
    Instruction* createSar(Value* lhs, Value* rhs, Type* resultType);
    Instruction* createNeg(Value* op);
    Instruction* createCopy(Value* operand);
    Instruction* createCopy(Value* operand, Type* resultType); // Explicit type override
    Instruction* createJmp(Value* target);
    Instruction* createJnz(Value* cond, Value* targetTrue, Value* targetFalse);
    Instruction* createBr(Value* cond, Value* targetTrue, Value* targetFalse);
    Instruction* createHlt();
    Instruction* createAlloc(Value* size, Type* type);
    Instruction* createAlloc4(Type* type);
    Instruction* createAlloc16(Type* type);
    Instruction* createStore(Value* value, Value* ptr);
    Instruction* createStored(Value* value, Value* ptr);
    Instruction* createStores(Value* value, Value* ptr);
    Instruction* createStorel(Value* value, Value* ptr);
    Instruction* createStoreh(Value* value, Value* ptr);
    Instruction* createStoreb(Value* value, Value* ptr);
    Instruction* createStoreStack(Value* value, int slot);
    Instruction* createLoad(Value* ptr);
    Instruction* createLoadd(Value* ptr);
    Instruction* createLoads(Value* ptr);
    Instruction* createLoadl(Value* ptr);
    Instruction* createLoaduw(Value* ptr);
    Instruction* createLoadsh(Value* ptr);
    Instruction* createLoaduh(Value* ptr);
    Instruction* createLoadsb(Value* ptr);
    Instruction* createLoadub(Value* ptr);
    Instruction* createLoadStack(Type* type, int slot);
    class PhiNode* createPhi(Type* type, unsigned numOperands, Instruction* alloc);
    Instruction* createCall(Value* callee, const std::vector<Value*>& args, Type* retType = nullptr);
    Instruction* createBlit(Value* dst, Value* src, Value* count);

    // --- Create methods for comparison instructions ---
    Instruction* createCeq(Value* lhs, Value* rhs);
    Instruction* createCne(Value* lhs, Value* rhs);
    Instruction* createCsle(Value* lhs, Value* rhs);
    Instruction* createCslt(Value* lhs, Value* rhs);
    Instruction* createCsge(Value* lhs, Value* rhs);
    Instruction* createCsgt(Value* lhs, Value* rhs);
    Instruction* createCule(Value* lhs, Value* rhs);
    Instruction* createCult(Value* lhs, Value* rhs);
    Instruction* createCuge(Value* lhs, Value* rhs);
    Instruction* createCugt(Value* lhs, Value* rhs);
    Instruction* createCeqf(Value* lhs, Value* rhs);
    Instruction* createCnef(Value* lhs, Value* rhs);
    Instruction* createCle(Value* lhs, Value* rhs);
    Instruction* createClt(Value* lhs, Value* rhs);
    Instruction* createCge(Value* lhs, Value* rhs);
    Instruction* createCgt(Value* lhs, Value* rhs);
    Instruction* createCo(Value* lhs, Value* rhs);
    Instruction* createCuo(Value* lhs, Value* rhs);

    Instruction* createExtUB(Value* val, Type* destTy);
    Instruction* createExtUH(Value* val, Type* destTy);
    Instruction* createExtUW(Value* val, Type* destTy);
    Instruction* createExtSB(Value* val, Type* destTy);
    Instruction* createExtSH(Value* val, Type* destTy);
    Instruction* createExtSW(Value* val, Type* destTy);
    Instruction* createExtS(Value* val, Type* destTy);
    Instruction* createTruncD(Value* val, Type* destTy);
    Instruction* createSWtoF(Value* val, Type* destTy);
    Instruction* createUWtoF(Value* val, Type* destTy);
    Instruction* createDToSI(Value* val, Type* destTy);
    Instruction* createDToUI(Value* val, Type* destTy);
    Instruction* createSToSI(Value* val, Type* destTy);
    Instruction* createSToUI(Value* val, Type* destTy);
    Instruction* createSltof(Value* val, Type* destTy);
    Instruction* createUltof(Value* val, Type* destTy);
    Instruction* createCast(Value* val, Type* destTy);
    Instruction* createVAStart(Value* val);
    Instruction* createVAArg(Value* val, Type* destTy);
    Instruction* createSyscall(const std::vector<Value*>& args, Type* retType = nullptr);
    Instruction* createSyscall(SyscallId id, const std::vector<Value*>& args, Type* retType = nullptr);
    // ... more instruction builder methods to come ...

private:
    std::shared_ptr<IRContext> context;
    Module* currentModule = nullptr;
    BasicBlock* insertPoint = nullptr;
    BasicBlock::instr_iterator insertIterator;
};

} // namespace ir
