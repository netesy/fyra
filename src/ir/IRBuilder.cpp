#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/FunctionType.h"
#include "ir/Parameter.h"

namespace ir {

IRBuilder::IRBuilder(std::shared_ptr<IRContext> ctx) : context(ctx) {}

void IRBuilder::setModule(Module* m) {
    currentModule = m;
}

void IRBuilder::setInsertPoint(BasicBlock* bb) {
    insertPoint = bb;
    insertIterator = bb->getInstructions().end();
}

void IRBuilder::setInsertPoint(BasicBlock* bb, BasicBlock::instr_iterator it) {
    insertPoint = bb;
    insertIterator = it;
}

Function* IRBuilder::createFunction(const std::string& name, Type* returnType) {
    if (auto* existing = currentModule->getFunction(name)) return existing;
    FunctionType* funcType = context->getFunctionType(returnType, {}, false);
    auto func = std::unique_ptr<Function>(new Function(funcType, name, currentModule));
    Function* funcPtr = func.get();
    currentModule->addFunction(std::move(func));
    return funcPtr;
}

Function* IRBuilder::createFunction(const std::string& name, Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic) {
    if (auto* existing = currentModule->getFunction(name)) return existing;
    FunctionType* funcType = context->getFunctionType(returnType, paramTypes, isVariadic);
    auto func = std::unique_ptr<Function>(new Function(funcType, name, currentModule));
    Function* funcPtr = func.get();

    for (Type* paramType : paramTypes) {
        func->addParameter(std::make_unique<Parameter>(paramType, ""));
    }
    func->setVariadic(isVariadic);

    currentModule->addFunction(std::move(func));
    return funcPtr;
}

BasicBlock* IRBuilder::createBasicBlock(const std::string& name, Function* parent) {
    auto bb = std::unique_ptr<BasicBlock>(new BasicBlock(parent, name));
    BasicBlock* bbPtr = bb.get();
    parent->addBasicBlock(std::move(bb));
    return bbPtr;
}

Instruction* IRBuilder::createRet(Value* val) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Ret, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSyscall(const std::vector<Value*>& args, Type* retType) {
    Type* returnType = retType ? retType : context->getVoidType();
    auto instr = std::make_unique<SyscallInstruction>(returnType, args, SyscallId::None, insertPoint);
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSyscall(SyscallId id, const std::vector<Value*>& args, Type* retType) {
    Type* returnType = retType ? retType : context->getVoidType();
    auto instr = std::make_unique<SyscallInstruction>(returnType, args, id, insertPoint);
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUdiv(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Udiv, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createRem(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Rem, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUrem(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Urem, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAnd(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::And, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createOr(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Or, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createXor(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Xor, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createShl(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Shl, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createShr(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Shr, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSar(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Sar, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createBr(Value* cond, Value* targetTrue, Value* targetFalse) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Br, {cond, targetTrue, targetFalse}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSltof(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::Sltof, {val}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUltof(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::Ultof, {val}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createHlt() {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Hlt, {}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUdiv(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Udiv, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createRem(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Rem, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUrem(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Urem, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

// --- Create methods for comparison instructions ---
Instruction* IRBuilder::createCeq(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Ceq, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCne(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cne, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCsle(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Csle, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCslt(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cslt, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCsge(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Csge, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCsgt(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Csgt, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCule(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cule, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCult(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cult, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCuge(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cuge, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCugt(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cugt, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCeqf(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Ceqf, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCnef(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cnef, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCle(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cle, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createClt(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Clt, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCge(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cge, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCgt(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cgt, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCo(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Co, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}
Instruction* IRBuilder::createCuo(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Cuo, {lhs, rhs}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAdd(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Add, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAdd(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Add, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSub(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Sub, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSub(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Sub, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createMul(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Mul, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createMul(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Mul, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createDiv(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Div, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createDiv(Value* lhs, Value* rhs, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Div, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createFAdd(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::FAdd, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createFSub(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::FSub, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createFMul(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::FMul, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createFDiv(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::FDiv, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAnd(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::And, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createOr(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Or, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createXor(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Xor, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createShl(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Shl, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createShr(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Shr, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSar(Value* lhs, Value* rhs) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(lhs->getType(), Instruction::Sar, {lhs, rhs}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createNeg(Value* op) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(op->getType(), Instruction::Neg, {op}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createJmp(Value* target) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Jmp, {target}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createJnz(Value* cond, Value* targetTrue, Value* targetFalse) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Jnz, {cond, targetTrue, targetFalse}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAlloc(Value* size, Type* type) {
    auto instr = std::make_unique<Instruction>(context->getIntegerType(64), Instruction::Alloc, std::vector<Value*>{size}, insertPoint);
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAlloc4(Type* type) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(type, Instruction::Alloc4, {}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createAlloc16(Type* type) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(type, Instruction::Alloc16, {}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStore(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Store, {value, ptr}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoad(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Load, {ptr}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStoreStack(Value* value, int slot) {
    ir::Value* slot_val = context->getConstantInt(context->getIntegerType(32), slot);
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Store, {value, slot_val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadStack(Type* type, int slot) {
    ir::Value* slot_val = context->getConstantInt(context->getIntegerType(32), slot);
    auto instr = std::unique_ptr<Instruction>(new Instruction(type, Instruction::Load, {slot_val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

PhiNode* IRBuilder::createPhi(Type* type, unsigned numOperands, Instruction* alloc) {
    auto instr = std::make_unique<PhiNode>(type, numOperands, alloc, insertPoint);
    PhiNode* instrPtr = instr.get();
    insertPoint->getInstructions().push_front(std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createCall(Value* callee, const std::vector<Value*>& args, Type* retType) {
    Type* returnType = retType ? retType : context->getVoidType();
    std::vector<Value*> all_operands;
    all_operands.push_back(callee);
    all_operands.insert(all_operands.end(), args.begin(), args.end());
    auto instr = std::unique_ptr<Instruction>(new Instruction(returnType, Instruction::Call, all_operands, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createCopy(Value* operand) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(operand->getType(), Instruction::Copy, {operand}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createCopy(Value* operand, Type* resultType) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(resultType, Instruction::Copy, {operand}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createBlit(Value* dst, Value* src, Value* count) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Blit, {dst, src, count}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtUB(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtUB, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtUH(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtUH, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtUW(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtUW, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtSB(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtSB, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtSH(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtSH, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtSW(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtSW, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createExtS(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::ExtS, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createTruncD(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::TruncD, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSWtoF(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::SWtoF, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createUWtoF(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::UWtoF, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createDToSI(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::DToSI, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createDToUI(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::DToUI, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSToSI(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::SToSI, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createSToUI(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::SToUI, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createCast(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::Cast, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStored(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Stored, {value, ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStores(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Stores, {value, ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStorel(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Storel, {value, ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStoreh(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Storeh, {value, ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createStoreb(Value* value, Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::Storeb, {value, ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createVAStart(Value* val) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getVoidType(), Instruction::VAStart, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadd(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getDoubleType(), Instruction::Loadd, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoads(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getFloatType(), Instruction::Loads, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadl(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(64), Instruction::Loadl, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoaduw(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Loaduw, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadsh(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Loadsh, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoaduh(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Loaduh, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadsb(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Loadsb, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createLoadub(Value* ptr) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(context->getIntegerType(32), Instruction::Loadub, {ptr}, insertPoint));
    auto* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

Instruction* IRBuilder::createVAArg(Value* val, Type* destTy) {
    auto instr = std::unique_ptr<Instruction>(new Instruction(destTy, Instruction::VAArg, {val}, insertPoint));
    Instruction* instrPtr = instr.get();
    insertPoint->addInstruction(insertIterator, std::move(instr));
    return instrPtr;
}

} // namespace ir
