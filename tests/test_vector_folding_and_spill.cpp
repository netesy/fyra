#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/SIMDInstruction.h"
#include "transforms/CFGBuilder.h"
#include "transforms/SCCP.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_vector_sccp_folding() {
    std::cout << "--- Test 1: Vector SCCP Constant Folding ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_vec_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::VectorType* v4i32Ty = ctx->getVectorType(i32Ty, 4);

    ir::Function* func = builder.createFunction("vector_fold", v4i32Ty, {});
    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    // Create ConstantVector v1 = <10, 20, 30, 40>
    ir::Constant* c10 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 10);
    ir::Constant* c20 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 20);
    ir::Constant* c30 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 30);
    ir::Constant* c40 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 40);
    ir::ConstantVector* vec1 = ctx->getConstantVector(v4i32Ty, {c10, c20, c30, c40});

    // Create ConstantVector v2 = <1, 2, 3, 4>
    ir::Constant* c1 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 1);
    ir::Constant* c2 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 2);
    ir::Constant* c3 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 3);
    ir::Constant* c4 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 4);
    ir::ConstantVector* vec2 = ctx->getConstantVector(v4i32Ty, {c1, c2, c3, c4});

    // vAdd = VAdd(vec1, vec2) -> should fold to <11, 22, 33, 44>
    ir::Instruction* vAdd = builder.createVAdd(vec1, vec2);
    // vSub = VSub(vAdd, vec2) -> should fold to <10, 20, 30, 40>
    ir::Instruction* vSub = builder.createVSub(vAdd, vec2);
    
    // Broadcast scalar 5 to <5, 5, 5, 5>
    ir::Constant* c5 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 5);
    ir::Instruction* vBcast = builder.createVBroadcast(v4i32Ty, c5);

    // vMul = VMul(vSub, vBcast) -> <50, 100, 150, 200>
    ir::Instruction* vMul = builder.createVMul(vSub, vBcast);

    // vExt = VExtract(vMul, 2) -> should fold to 150
    ir::Instruction* vExt = builder.createVExtract(vMul, ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 2));

    builder.createRet(vExt);

    transforms::CFGBuilder::run(*func);

    transforms::SCCP sccp;
    bool changed = sccp.run(*func);
    assert(changed);

    // Check that return operand is now constant 150
    ir::Instruction* retInst = func->getBasicBlocks().front()->getInstructions().back().get();
    assert(retInst->getOpcode() == ir::Instruction::Ret);
    ir::Value* retVal = retInst->getOperands()[0]->get();
    auto* ci = dynamic_cast<ir::ConstantInt*>(retVal);
    assert(ci && ci->getValue() == 150);

    std::cout << "--- Test 1 Passed: Vector SCCP Folding Folded VAdd/VSub/VMul/VBroadcast/VExtract to 150 ---" << std::endl;
}

void test_vector_spill_alignment() {
    std::cout << "--- Test 2: Vector Spilling and Stack Frame Alignment ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_spill_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::VectorType* v4i32Ty = ctx->getVectorType(i32Ty, 4);  // 128-bit, 16-byte
    ir::VectorType* v8i32Ty = ctx->getVectorType(i32Ty, 8);  // 256-bit, 32-byte
    ir::VectorType* v16i32Ty = ctx->getVectorType(i32Ty, 16); // 512-bit, 64-byte

    ir::Function* func = builder.createFunction("vector_spill_func", i32Ty, {});
    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    // Force register pressure by creating many vector instructions that are all live simultaneously
    std::vector<ir::Instruction*> v128_insts;
    std::vector<ir::Instruction*> v256_insts;
    std::vector<ir::Instruction*> v512_insts;

    // Create 20 128-bit, 20 256-bit, and 20 512-bit vector instructions (exceeding XMM register count)
    ir::Constant* zero32 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 0);
    ir::Instruction* base128 = builder.createVBroadcast(v4i32Ty, zero32);
    ir::Instruction* base256 = builder.createVBroadcast(v8i32Ty, zero32);
    ir::Instruction* base512 = builder.createVBroadcast(v16i32Ty, zero32);

    for (int i = 0; i < 20; ++i) {
        v128_insts.push_back(builder.createVAdd(base128, base128));
        v256_insts.push_back(builder.createVAdd(base256, base256));
        v512_insts.push_back(builder.createVAdd(base512, base512));
    }

    // Keep all vectors live by accumulating them into a final result
    ir::Instruction* acc128 = v128_insts[0];
    for (size_t i = 1; i < v128_insts.size(); ++i) {
        acc128 = builder.createVAdd(acc128, v128_insts[i]);
    }

    ir::Instruction* acc256 = v256_insts[0];
    for (size_t i = 1; i < v256_insts.size(); ++i) {
        acc256 = builder.createVAdd(acc256, v256_insts[i]);
    }

    ir::Instruction* acc512 = v512_insts[0];
    for (size_t i = 1; i < v512_insts.size(); ++i) {
        acc512 = builder.createVAdd(acc512, v512_insts[i]);
    }

    ir::Instruction* ext128 = builder.createVExtract(acc128, zero32);
    builder.createRet(ext128);

    transforms::CFGBuilder::run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    target::CompositeTargetInfo targetInfo(std::move(x64Arch), std::move(linuxOS));

    // Run RegAllocRewriter which runs LinearScanAllocator
    transforms::RegAllocRewriter rewriter;
    bool rewrote = rewriter.run(*func, &targetInfo);
    assert(rewrote);

    // Verify stack slot alignment for spilled vector instructions
    for (const auto& [vreg, slotOffset] : func->getStackSlots()) {
        if (vreg && vreg->getType()) {
            if (auto* vt = dynamic_cast<const ir::VectorType*>(vreg->getType())) {
                size_t bits = vt->getSize() * 8;
                if (bits >= 512) {
                    assert(slotOffset % 64 == 0 && "512-bit vector stack slot must be 64-byte aligned!");
                } else if (bits >= 256) {
                    assert(slotOffset % 32 == 0 && "256-bit vector stack slot must be 32-byte aligned!");
                } else if (bits >= 128) {
                    assert(slotOffset % 16 == 0 && "128-bit vector stack slot must be 16-byte aligned!");
                }
            }
        }
    }

    std::cout << "--- Test 2 Passed: Spilled Vector Stack Slots Correctly Aligned (16, 32, 64 bytes) ---" << std::endl;
}

int main() {
    test_vector_sccp_folding();
    test_vector_spill_alignment();
    std::cout << "=== All Vector Constant Folding & Spilling Alignment Tests Passed ===" << std::endl;
    return 0;
}
