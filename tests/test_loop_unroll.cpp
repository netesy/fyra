#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/Validator.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopUnroll.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

void test_loop_unroll_basic() {
    std::cout << "--- Running LoopUnroll Basic Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("unroll_basic_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("unroll_basic_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry:
    builder.setInsertPoint(entry);
    builder.createJmp(header);

    // header:
    builder.setInsertPoint(header);
    auto i_phi = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* i_phi_ptr = i_phi.get();
    i_phi_ptr->setName("i");
    header->getInstructions().push_back(std::move(i_phi));

    auto sum_phi = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* sum_phi_ptr = sum_phi.get();
    sum_phi_ptr->setName("sum");
    header->getInstructions().push_back(std::move(sum_phi));

    i_phi_ptr->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), entry);
    sum_phi_ptr->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), entry);

    ir::Instruction* cond = builder.createCslt(i_phi_ptr, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    cond->setName("cond");
    builder.createBr(cond, body, exit);

    // body:
    builder.setInsertPoint(body);
    ir::Instruction* sum_next = builder.createAdd(sum_phi_ptr, i_phi_ptr);
    sum_next->setName("sum_next");
    ir::Instruction* i_next = builder.createAdd(i_phi_ptr, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    i_next->setName("i_next");

    i_phi_ptr->addIncoming(i_next, body);
    sum_phi_ptr->addIncoming(sum_next, body);
    builder.createJmp(header);

    // exit:
    builder.setInsertPoint(exit);
    builder.createRet(sum_phi_ptr);

    transforms::CFGBuilder::run(*func);

    // Validate IR before unrolling
    std::vector<std::string> errors;
    bool valid_before = ir::Validator::validateModule(module, errors);
    assert(valid_before);

    transforms::LoopUnroll unroller;
    bool changed = unroller.run(*func);
    assert(changed);

    // Validate IR after unrolling
    errors.clear();
    bool valid_after = ir::Validator::validateModule(module, errors);
    for (const auto& err : errors) {
        std::cerr << "Validation error: " << err << std::endl;
    }
    assert(valid_after);

    std::cout << "--- LoopUnroll Basic Test Passed ---" << std::endl;
}

void test_loop_unroll_call_bail() {
    std::cout << "--- Running LoopUnroll Call Bail Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("unroll_call_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* dummyCallee = builder.createFunction("dummy_callee", i32Ty, {});
    ir::BasicBlock* dummyBB = builder.createBasicBlock("entry", dummyCallee);
    builder.setInsertPoint(dummyBB);
    builder.createRet(ctx->getConstantInt(ctx->getIntegerType(32), 42));

    ir::Function* func = builder.createFunction("unroll_call_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry:
    builder.setInsertPoint(entry);
    builder.createJmp(header);

    // header:
    builder.setInsertPoint(header);
    auto i_phi = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* i_phi_ptr = i_phi.get();
    i_phi_ptr->setName("i");
    header->getInstructions().push_back(std::move(i_phi));

    i_phi_ptr->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), entry);

    ir::Instruction* cond = builder.createCslt(i_phi_ptr, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createBr(cond, body, exit);

    // body:
    builder.setInsertPoint(body);
    builder.createCall(dummyCallee, {}); // Call inside loop! Must bail out.
    ir::Instruction* i_next = builder.createAdd(i_phi_ptr, ctx->getConstantInt(ctx->getIntegerType(32), 1));

    i_phi_ptr->addIncoming(i_next, body);
    builder.createJmp(header);

    // exit:
    builder.setInsertPoint(exit);
    builder.createRet(i_phi_ptr);

    transforms::CFGBuilder::run(*func);

    transforms::LoopUnroll unroller;
    bool changed = unroller.run(*func);
    assert(!changed); // Must NOT unroll loop with call!

    std::cout << "--- LoopUnroll Call Bail Test Passed ---" << std::endl;
}

void test_loop_unroll_large_body_bail() {
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("unroll_large_module", ctx);
    ir::IRBuilder builder(ctx); builder.setModule(&module);
    auto* i32 = ctx->getIntegerType(32);
    auto* func = builder.createFunction("unroll_large", i32, {});
    auto* entry = builder.createBasicBlock("entry", func);
    auto* header = builder.createBasicBlock("header", func);
    auto* body = builder.createBasicBlock("body", func);
    auto* exit = builder.createBasicBlock("exit", func);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto phi = std::make_unique<ir::PhiNode>(i32, 0, nullptr, header);
    auto* induction = phi.get(); header->getInstructions().push_back(std::move(phi));
    induction->addIncoming(ctx->getConstantInt(i32, 0), entry);
    auto* cond = builder.createCslt(induction, ctx->getConstantInt(i32, 100));
    builder.createBr(cond, body, exit);
    builder.setInsertPoint(body);
    ir::Value* value = induction;
    for (unsigned n = 0; n < 35; ++n)
        value = builder.createAdd(value, ctx->getConstantInt(i32, n + 2));
    auto* next = builder.createAdd(induction, ctx->getConstantInt(i32, 1));
    induction->addIncoming(next, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(induction);
    transforms::CFGBuilder::run(*func);
    transforms::LoopUnroll unroller;
    assert(!unroller.run(*func) && "large loop bodies must remain canonical until live-range repair is supported");
}

int main() {
    test_loop_unroll_basic();
    test_loop_unroll_call_bail();
    test_loop_unroll_large_body_bail();
    return 0;
}
