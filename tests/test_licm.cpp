#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <algorithm>

void test_licm_positive() {
    std::cout << "--- Running LICM Positive Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("licm_pos_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("licm_pos_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry: jmp header
    builder.setInsertPoint(entry);
    ir::Value* const100 = ctx->getConstantInt(ctx->getIntegerType(32), 100);
    ir::Value* const200 = ctx->getConstantInt(ctx->getIntegerType(32), 200);
    builder.createJmp(header);

    // header:
    auto phi_ptr = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* phi = phi_ptr.get();
    header->getInstructions().push_back(std::move(phi_ptr));

    builder.setInsertPoint(header);
    phi->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), entry);
    ir::Instruction* cond = builder.createCslt(phi, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createBr(cond, body, exit);

    // body:
    builder.setInsertPoint(body);
    ir::Instruction* inv_add = builder.createAdd(const100, const200); // Loop-invariant!
    ir::Instruction* dep_inc = builder.createAdd(phi, inv_add); // Dependent on phi!
    phi->addIncoming(dep_inc, body);
    builder.createJmp(header);

    // exit:
    builder.setInsertPoint(exit);
    builder.createRet(phi);

    transforms::CFGBuilder::run(*func);

    transforms::LoopInvariantCodeMotion licm;
    bool changed = licm.run(*func);

    assert(changed);
    // Assert invariant instruction is moved outside body block
    assert(inv_add->getParent() != body);
    // Assert dependent instruction remains inside body block
    assert(dep_inc->getParent() == body);

    std::cout << "--- LICM Positive Test Passed ---" << std::endl;
}

void test_licm_negative() {
    std::cout << "--- Running LICM Negative Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("licm_neg_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("licm_neg_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry:
    builder.setInsertPoint(entry);
    ir::Instruction* alloc = builder.createAlloc(ctx->getConstantInt(ctx->getIntegerType(32), 4), i32Ty);
    builder.createJmp(header);

    // header:
    auto phi_ptr = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* phi = phi_ptr.get();
    header->getInstructions().push_back(std::move(phi_ptr));

    builder.setInsertPoint(header);
    phi->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), entry);
    ir::Instruction* cond = builder.createCslt(phi, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createBr(cond, body, exit);

    // body:
    builder.setInsertPoint(body);
    ir::Instruction* store = builder.createStore(phi, alloc); // Store has side effects!
    ir::Instruction* inc = builder.createAdd(phi, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    phi->addIncoming(inc, body);
    builder.createJmp(header);

    // exit:
    builder.setInsertPoint(exit);
    builder.createRet(phi);

    transforms::CFGBuilder::run(*func);

    transforms::LoopInvariantCodeMotion licm;
    bool changed = licm.run(*func);

    assert(!changed);
    assert(store->getParent() == body);
    assert(inc->getParent() == body);

    std::cout << "--- LICM Negative Test Passed ---" << std::endl;
}

int main() {
    test_licm_positive();
    test_licm_negative();
    return 0;
}
