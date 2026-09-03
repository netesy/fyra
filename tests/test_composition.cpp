#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/FunctionInliner.h"
#include "transforms/SCCP.h"
#include "transforms/ControlFlowSimplification.h"
#include "transforms/CopyElimination.h"
#include "transforms/GVN.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/DeadInstructionElimination.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_inlining_sccp_cfg_gvn_dce_chain() {
    std::cout << "--- Testing Inlining -> SCCP -> CFGSimp -> GVN -> DCE Chain ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("comp_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);

    // int callee(int val) { if (val < 0) return 0; else return val * 0; }
    ir::Function* callee = builder.createFunction("callee", i32Ty, {i32Ty});
    ir::Parameter* pVal = callee->getParameters().front().get();

    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", callee);
    ir::BasicBlock* c_then = builder.createBasicBlock("c_then", callee);
    ir::BasicBlock* c_else = builder.createBasicBlock("c_else", callee);

    builder.setInsertPoint(c_entry);
    ir::Instruction* cond = builder.createCslt(pVal, ctx->getConstantInt(i32Ty, 0));
    builder.createBr(cond, c_then, c_else);

    builder.setInsertPoint(c_then);
    builder.createRet(ctx->getConstantInt(i32Ty, 0));

    builder.setInsertPoint(c_else);
    ir::Instruction* mul0 = builder.createMul(pVal, ctx->getConstantInt(i32Ty, 0));
    builder.createRet(mul0);

    // int caller() { return callee(5); }
    ir::Function* caller = builder.createFunction("caller", i32Ty, {});
    ir::BasicBlock* k_entry = builder.createBasicBlock("k_entry", caller);
    builder.setInsertPoint(k_entry);

    ir::Instruction* callInst = builder.createCall(callee, {ctx->getConstantInt(i32Ty, 5)});
    builder.createRet(callInst);

    transforms::CFGBuilder::run(*callee);
    transforms::CFGBuilder::run(*caller);

    std::cout << "[COMP] Starting inlining..." << std::endl;
    transforms::FunctionInliner inliner;
    assert(inliner.runOnModule(module));
    std::cout << "[COMP] Inlining done!" << std::endl;

    transforms::SCCP sccp;
    transforms::ControlFlowSimplification cfgSimp;
    transforms::GVN gvn;
    transforms::CopyElimination copyElim;
    transforms::DeadInstructionElimination dce;

    bool changed = true;
    int passCount = 0;
    while (changed && passCount++ < 10) {
        changed = false;
        std::cout << "[COMP " << passCount << "] Running SCCP..." << std::endl;
        if (sccp.run(*caller)) changed = true;
        std::cout << "[COMP " << passCount << "] Before CFGSimp, caller blocks = " << caller->getBasicBlocks().size() << std::endl;
        for (auto& bb : caller->getBasicBlocks()) {
            std::cout << "  Block " << bb->getName() << " instrs = " << bb->getInstructions().size() << std::endl;
        }
        std::cout << "[COMP " << passCount << "] Running CFGSimp..." << std::endl;
        if (cfgSimp.run(*caller)) {
            changed = true;
            std::cout << "  CFGSimp made caller blocks count = " << caller->getBasicBlocks().size() << std::endl;
            for (auto& bb : caller->getBasicBlocks()) {
                std::cout << "  Block " << bb->getName() << " instrs = " << bb->getInstructions().size() << std::endl;
            }
        }
        std::cout << "[COMP " << passCount << "] Running GVN..." << std::endl;
        if (gvn.run(*caller)) changed = true;
        std::cout << "[COMP " << passCount << "] Running CopyElim..." << std::endl;
        if (copyElim.run(*caller)) changed = true;
        std::cout << "[COMP " << passCount << "] Before DCE, k_entry instrs = " << k_entry->getInstructions().size() << std::endl;
        std::cout << "[COMP " << passCount << "] Running DCE..." << std::endl;
        if (dce.run(*caller)) {
            changed = true;
            std::cout << "  DCE made caller blocks count = " << caller->getBasicBlocks().size() << std::endl;
            for (auto& bb : caller->getBasicBlocks()) {
                std::cout << "  Block " << bb->getName() << " instrs = " << bb->getInstructions().size() << std::endl;
            }
        }
        std::cout << "[COMP " << passCount << "] After DCE, k_entry instrs = " << k_entry->getInstructions().size() << std::endl;
    }

    std::cout << "[COMP] k_entry ptr = " << k_entry << std::endl;
    std::cout << "[COMP] Reached end of loop, caller blocks = " << caller->getBasicBlocks().size() << std::endl;
    assert(caller->getBasicBlocks().size() == 1);
    ir::BasicBlock* finalBB = caller->getBasicBlocks().front().get();
    std::cout << "[COMP] finalBB ptr = " << finalBB << " name = " << finalBB->getName() << " instrs count = " << finalBB->getInstructions().size() << std::endl;
    for (auto& inst : finalBB->getInstructions()) {
        std::cout << "  instr op=" << inst->getOpcode() << " ptr=" << inst.get() << std::endl;
    }
    ir::Instruction* retInst = finalBB->getInstructions().back().get();
    assert(retInst->getOpcode() == ir::Instruction::Ret);
    std::cout << "[COMP] retInst operands count = " << retInst->getOperands().size() << std::endl;
    if (!retInst->getOperands().empty()) {
        std::cout << "[COMP] op0 ptr = " << retInst->getOperands()[0].get() << std::endl;
        std::cout << "[COMP] op0 val = " << retInst->getOperands()[0]->get() << std::endl;
    }
    auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
    assert(retConst != nullptr);
    assert(retConst->getValue() == 0);

    std::cout << "--- Inlining -> SCCP -> CFGSimp -> GVN -> DCE Chain Passed ---" << std::endl;
}

void test_licm_gvn_dce_chain() {
    std::cout << "--- Testing LICM -> GVN -> DCE Chain ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("licm_gvn_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("loop_func", i32Ty, {i32Ty, i32Ty});
    ir::Parameter* paramA = func->getParameters().front().get();
    ir::Parameter* paramB = (*std::next(func->getParameters().begin())).get();

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry: jmp header
    builder.setInsertPoint(entry);
    builder.createJmp(header);

    // header:
    auto phi_ptr = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* phi = phi_ptr.get();
    header->getInstructions().push_back(std::move(phi_ptr));

    builder.setInsertPoint(header);
    phi->addIncoming(ctx->getConstantInt(i32Ty, 0), entry);
    ir::Instruction* cond = builder.createCslt(phi, ctx->getConstantInt(i32Ty, 10));
    builder.createBr(cond, body, exit);

    // body:
    builder.setInsertPoint(body);
    ir::Instruction* inv1 = builder.createAdd(paramA, paramB); // invariant 1
    ir::Instruction* inv2 = builder.createAdd(paramB, paramA); // invariant 2 (identical)
    ir::Instruction* inc = builder.createAdd(inv1, inv2);
    ir::Instruction* phiInc = builder.createAdd(phi, inc);
    phi->addIncoming(phiInc, body);
    builder.createJmp(header);

    // exit:
    builder.setInsertPoint(exit);
    builder.createRet(phi);

    transforms::CFGBuilder::run(*func);

    transforms::LoopInvariantCodeMotion licm;
    transforms::GVN gvn;
    transforms::DeadInstructionElimination dce;

    assert(licm.run(*func));
    assert(gvn.run(*func));
    dce.run(*func);

    // Verify inv1 was hoisted to entry/preheader and inv2 was eliminated as redundant
    assert(inv1->getParent() != body);
    assert(inv2->getUseList().empty());

    std::cout << "--- LICM -> GVN -> DCE Chain Passed ---" << std::endl;
}

int main() {
    test_inlining_sccp_cfg_gvn_dce_chain();
    test_licm_gvn_dce_chain();
    return 0;
}
