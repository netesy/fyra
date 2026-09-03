#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "transforms/DivisionStrengthReduction.h"
#include "transforms/ErrorReporter.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <climits>

void test_unsigned_div_rem() {
    std::cout << "Testing Unsigned Division and Remainder Strength Reduction...\n";
    ir::Module mod("test_udiv");
    ir::IRBuilder builder;
    builder.setModule(&mod);

    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    ir::Function* func = builder.createFunction("test_udiv_fn", i32Ty);
    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    ir::Value* x = ir::ConstantInt::get(i32Ty, 100);

    std::vector<uint32_t> constants = {3, 5, 7, 10, 11};
    std::vector<ir::Value*> results;

    for (uint32_t c : constants) {
        ir::ConstantInt* cVal = ir::ConstantInt::get(i32Ty, c);
        ir::Instruction* udivInst = builder.createUdiv(x, cVal);
        ir::Instruction* uremInst = builder.createUrem(x, cVal);
        results.push_back(udivInst);
        results.push_back(uremInst);
    }

    builder.createRet(results[0]);

    transforms::DivisionStrengthReduction pass;
    bool changed = pass.run(*func);
    assert(changed && "DivisionStrengthReduction should modify IR for udiv/urem by constant");

    for (auto& inst : bb->getInstructions()) {
        assert(inst->getOpcode() != ir::Instruction::Udiv && "Udiv should be eliminated!");
        assert(inst->getOpcode() != ir::Instruction::Urem && "Urem should be eliminated!");
    }
    std::cout << "Unsigned Div/Rem Strength Reduction Test Passed!\n";
}

void test_signed_div_rem() {
    std::cout << "Testing Signed Division and Remainder Strength Reduction...\n";
    ir::Module mod("test_sdiv");
    ir::IRBuilder builder;
    builder.setModule(&mod);

    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    ir::Function* func = builder.createFunction("test_sdiv_fn", i32Ty);
    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);

    // Test with edge case values: negative values, INT_MAX, INT_MIN
    std::vector<int32_t> test_values = {-100, 0, 1, -1, INT_MAX, INT_MIN};
    std::vector<int32_t> divisors = {3, -3, 5, 7, 11};

    for (int32_t val : test_values) {
        ir::Value* x = ir::ConstantInt::get(i32Ty, val);
        for (int32_t d : divisors) {
            ir::ConstantInt* cVal = ir::ConstantInt::get(i32Ty, d);
            builder.createDiv(x, cVal);
            builder.createRem(x, cVal);
        }
    }

    builder.createRet(ir::ConstantInt::get(i32Ty, 0));

    transforms::DivisionStrengthReduction pass;
    bool changed = pass.run(*func);
    assert(changed && "DivisionStrengthReduction should modify IR for sdiv/srem by constant");

    for (auto& inst : bb->getInstructions()) {
        assert(inst->getOpcode() != ir::Instruction::Div && "Div should be eliminated!");
        assert(inst->getOpcode() != ir::Instruction::Rem && "Rem should be eliminated!");
    }
    std::cout << "Signed Div/Rem Strength Reduction Test Passed!\n";
}

int main() {
    test_unsigned_div_rem();
    test_signed_div_rem();
    std::cout << "All Division Strength Reduction Unit Tests Passed Successfully!\n";
    return 0;
}
