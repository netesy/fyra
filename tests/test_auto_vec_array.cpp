#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <sstream>

using namespace ir;

enum class ArrayOp { Add, Sub, Mul };

void run_array_loop_test(ArrayOp op, const std::string& opName, const std::string& expectedAsmOp) {
    std::cout << "--- Testing 256-bit AVX2 Array " << opName << " Loop ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_arr_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);
    Type* voidTy = ctx->getVoidType();

    // void vec_op(int32_t* a, int32_t* b, int32_t* c, int32_t n)
    Function* func = builder.createFunction("vec_op", voidTy, {i64Ty, i64Ty, i64Ty, i32Ty});
    auto pIt = func->getParameters().begin();
    Value* pA = (pIt++)->get();
    Value* pB = (pIt++)->get();
    Value* pC = (pIt++)->get();
    Value* pN = (pIt++)->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();
    loopHeader->getInstructions().push_back(std::move(phiI));

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);

    Instruction* cond = builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* i64I = builder.createExtSW(rawPhiI, i64Ty);
    Instruction* offset = builder.createMul(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));

    Instruction* ptrB = builder.createAdd(pB, offset);
    Instruction* valB = builder.createLoaduw(ptrB);

    Instruction* ptrC = builder.createAdd(pC, offset);
    Instruction* valC = builder.createLoaduw(ptrC);

    Instruction* valA = nullptr;
    if (op == ArrayOp::Add) valA = builder.createAdd(valB, valC);
    else if (op == ArrayOp::Sub) valA = builder.createSub(valB, valC);
    else if (op == ArrayOp::Mul) valA = builder.createMul(valB, valC);

    Instruction* ptrA = builder.createAdd(pA, offset);
    builder.createStore(valA, ptrA);

    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
    rawPhiI->addIncoming(iNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);

    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    assert(vectorized && "Array loop MUST be vectorized!");

    transforms::LinearScanAllocator allocator;
    allocator.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    std::cout << asmCode << std::endl;

    assert(asmCode.find(expectedAsmOp) != std::string::npos && "Expected 256-bit YMM operation in assembly!");
    std::cout << "PASSED: " << expectedAsmOp << " present in assembly!" << std::endl;
}

int main() {
    run_array_loop_test(ArrayOp::Add, "Add", "vpaddd");
    run_array_loop_test(ArrayOp::Sub, "Sub", "vpsubd");
    run_array_loop_test(ArrayOp::Mul, "Mul", "vpmulld");
    return 0;
}
