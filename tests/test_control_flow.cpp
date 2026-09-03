#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "transforms/CFGBuilder.h"
#include "transforms/ControlFlowSimplification.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>
#include <algorithm>

void test_cfg_validity_after_mutation() {
    std::cout << "--- Testing CFG Validity After Control-Flow Mutations ---" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("cfg_test_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_cfg_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* b_cond = builder.createBasicBlock("b_cond", func);
    ir::BasicBlock* b_true = builder.createBasicBlock("b_true", func);
    ir::BasicBlock* b_unreachable = builder.createBasicBlock("b_unreachable", func);
    ir::BasicBlock* b_loop_header = builder.createBasicBlock("b_loop_header", func);
    ir::BasicBlock* b_loop_body = builder.createBasicBlock("b_loop_body", func);
    ir::BasicBlock* b_exit = builder.createBasicBlock("b_exit", func);

    // entry:
    builder.setInsertPoint(entry);
    builder.createJmp(b_cond);

    // b_cond: constant branch 1 -> b_true / b_unreachable
    builder.setInsertPoint(b_cond);
    builder.createBr(ctx->getConstantInt(ctx->getIntegerType(32), 1), b_true, b_unreachable);

    // b_true:
    builder.setInsertPoint(b_true);
    builder.createJmp(b_loop_header);

    // b_unreachable:
    builder.setInsertPoint(b_unreachable);
    ir::Instruction* dead_val = builder.createAdd(ctx->getConstantInt(ctx->getIntegerType(32), 10), ctx->getConstantInt(ctx->getIntegerType(32), 20));
    builder.createJmp(b_exit);

    // b_loop_header:
    auto phi_ptr = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, b_loop_header);
    ir::PhiNode* phi = phi_ptr.get();
    b_loop_header->getInstructions().push_back(std::move(phi_ptr));

    builder.setInsertPoint(b_loop_header);
    phi->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), b_true);
    ir::Instruction* inc = builder.createAdd(phi, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    phi->addIncoming(inc, b_loop_body);
    ir::Instruction* cond = builder.createCslt(inc, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createBr(cond, b_loop_body, b_exit);

    // b_loop_body:
    builder.setInsertPoint(b_loop_body);
    builder.createJmp(b_loop_header);

    // b_exit:
    auto exit_phi_ptr = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, b_exit);
    ir::PhiNode* exit_phi = exit_phi_ptr.get();
    b_exit->getInstructions().push_back(std::move(exit_phi_ptr));

    builder.setInsertPoint(b_exit);
    exit_phi->addIncoming(inc, b_loop_header);
    exit_phi->addIncoming(dead_val, b_unreachable);
    builder.createRet(exit_phi);

    // Initial CFG build
    transforms::CFGBuilder::run(*func);

    // Verify initial CFG state
    assert(b_cond->getSuccessors().size() == 2);
    assert(b_unreachable->getPredecessors().size() == 1);

    // Run ControlFlowSimplification
    transforms::ControlFlowSimplification cfgSimp;
    bool changed = cfgSimp.run(*func);
    assert(changed);

    // Verify downstream CFG invariant:
    // 1. b_unreachable should be removed
    bool found_unreachable = false;
    for (auto& bb : func->getBasicBlocks()) {
        if (bb.get() == b_unreachable) found_unreachable = true;
    }
    assert(!found_unreachable);

    // 2. All predecessors and successors must be mutual and accurate across remaining blocks
    for (auto& bb : func->getBasicBlocks()) {
        for (ir::BasicBlock* succ : bb->getSuccessors()) {
            auto& succ_preds = succ->getPredecessors();
            assert(std::find(succ_preds.begin(), succ_preds.end(), bb.get()) != succ_preds.end());
        }
        for (ir::BasicBlock* pred : bb->getPredecessors()) {
            auto& pred_succs = pred->getSuccessors();
            assert(std::find(pred_succs.begin(), pred_succs.end(), bb.get()) != pred_succs.end());
        }
    }

    // 3. b_exit should no longer have b_unreachable as a predecessor
    auto& exit_preds = b_exit->getPredecessors();
    assert(std::find(exit_preds.begin(), exit_preds.end(), b_unreachable) == exit_preds.end());

    std::cout << "--- CFG Validity Test Passed Successfully! ---" << std::endl;
}

int main() {
    test_cfg_validity_after_mutation();

    std::string test_file = "tests/control_flow.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for control_flow.fyra:\n" << generated_asm << std::endl;

    assert(generated_asm.find("jmp") != std::string::npos);
    assert(generated_asm.find("jne") != std::string::npos || generated_asm.find("jl") != std::string::npos); // br is translated to jne or fused conditional branch (jl/jne/jle)
    assert(generated_asm.find("cmp") != std::string::npos); // slt is translated to cmp
    assert(generated_asm.find("ret") != std::string::npos);

    return 0;
}
