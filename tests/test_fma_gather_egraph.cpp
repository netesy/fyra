#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/SIMDInstruction.h"
#include "transforms/CFGBuilder.h"
#include "transforms/EGraphPass.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "codegen/CodeGen.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>

void test_fma_lowering() {
    std::cout << "--- Test 1: Native FMA Assembly Lowering ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_fma_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* f32Ty = ctx->getFloatType();
    ir::VectorType* v4f32Ty = ctx->getVectorType(f32Ty, 4);

    ir::Function* func = builder.createFunction("test_fma", v4f32Ty, {v4f32Ty, v4f32Ty, v4f32Ty});
    auto pIt = func->getParameters().begin();
    ir::Parameter* pA = (pIt++)->get();
    ir::Parameter* pB = (pIt++)->get();
    ir::Parameter* pC = pIt->get();

    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    ir::Instruction* fmaInst = builder.createFMA(pA, pB, pC);
    builder.createRet(fmaInst);

    transforms::CFGBuilder::run(*func);

    std::ostringstream ss;
    auto targetInfo = std::make_unique<target::CompositeTargetInfo>(
        std::make_unique<target::X64Architecture>(target::X64ABI::SystemV),
        std::make_unique<target::LinuxOS>()
    );

    codegen::CodeGen cg(module, std::move(targetInfo), &ss);
    cg.emit();

    std::string asm_code = ss.str();
    assert(asm_code.find("vfmadd213ps") != std::string::npos && "Assembly output must contain native vfmadd213ps instruction!");

    std::cout << "--- Test 1 Passed: Native vfmadd213ps lowering verified ---" << std::endl;
}

void test_vgather_vscatter_lowering() {
    std::cout << "--- Test 2: VGather / VScatter Lowering ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_gather_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Type* ptrTy = ctx->getPointerType(i32Ty);
    ir::VectorType* v4i32Ty = ctx->getVectorType(i32Ty, 4);

    ir::Function* func = builder.createFunction("test_gather", v4i32Ty, {ptrTy, v4i32Ty});
    auto pIt = func->getParameters().begin();
    ir::Parameter* pBase = (pIt++)->get();
    ir::Parameter* pIdx = pIt->get();

    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    ir::VectorInstruction* gatherInst = builder.createVGather(v4i32Ty, pBase, pIdx);
    builder.createVScatter(gatherInst, pBase, pIdx);
    builder.createRet(gatherInst);

    transforms::CFGBuilder::run(*func);

    std::ostringstream ss;
    auto targetInfo = std::make_unique<target::CompositeTargetInfo>(
        std::make_unique<target::X64Architecture>(target::X64ABI::SystemV),
        std::make_unique<target::LinuxOS>()
    );

    codegen::CodeGen cg(module, std::move(targetInfo), &ss);
    cg.emit();

    std::string asm_code = ss.str();
    assert(asm_code.find("vpgatherdd") != std::string::npos && "Assembly output must contain vpgatherdd instruction!");
    assert(asm_code.find("vpscatterdd") != std::string::npos && "Assembly output must contain vpscatterdd instruction!");

    std::cout << "--- Test 2 Passed: VGather / VScatter lowering verified ---" << std::endl;
}

void test_egraph_pass() {
    std::cout << "--- Test 3: Scoped E-Graph Equality Saturation Pass ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_egraph_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_div_strength", i32Ty, {i32Ty});
    ir::Parameter* pX = func->getParameters().front().get();

    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    // udiv pX, 8 -> EGraph pass should rewrite to shr pX, 3
    ir::Instruction* divInst = builder.createUdiv(pX, ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 8));
    builder.createRet(divInst);

    transforms::CFGBuilder::run(*func);

    transforms::EGraphPass egraphPass;
    bool changed = egraphPass.run(*func);
    assert(changed);

    // Verify divInst was replaced by a shift right instruction
    ir::Instruction* retInst = nullptr;
    for (const auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() == ir::Instruction::Ret) {
            retInst = inst.get();
            break;
        }
    }
    assert(retInst != nullptr);
    ir::Value* retVal = retInst->getOperands()[0]->get();
    auto* resInst = dynamic_cast<ir::Instruction*>(retVal);
    assert(resInst && resInst->getOpcode() == ir::Instruction::Shr);

    std::cout << "--- Test 3 Passed: EGraph Equality Saturation rewritten Udiv to Shr ---" << std::endl;
}

int main() {
    test_fma_lowering();
    test_vgather_vscatter_lowering();
    test_egraph_pass();
    std::cout << "=== All FMA, VGather/VScatter, and EGraph Tests Passed ===" << std::endl;
    return 0;
}
