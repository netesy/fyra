#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "transforms/DivisionStrengthReduction.h"
#include "transforms/ErrorReporter.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <climits>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::abort();
    }
}

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

    std::vector<int32_t> test_values = {-100, 0, 1, -1, INT_MAX, INT_MIN};
    std::vector<int32_t> divisors = {2, -2, 4, 8, 16, 7, 11, 13, 5, 9, 17, 23, -7, -11};

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

    ir::Function* nonPowerFunc = builder.createFunction("signed_non_power_func", i32Ty, {i32Ty});
    ir::BasicBlock* npBB = builder.createBasicBlock("entry", nonPowerFunc);
    builder.setInsertPoint(npBB);
    ir::Value* input = nonPowerFunc->getParameters().front().get();
    builder.createRem(input, ir::ConstantInt::get(i32Ty, 7));
    builder.createRem(input, ir::ConstantInt::get(i32Ty, 11));
    builder.createRem(input, ir::ConstantInt::get(i32Ty, 13));
    builder.createDiv(input, ir::ConstantInt::get(i32Ty, 17));
    builder.createRet(ir::ConstantInt::get(i32Ty, 0));

    bool npChanged = pass.run(*nonPowerFunc);
    assert(npChanged && "Signed non-power-of-two div/rem should be lowered via magic multiplication");

    for (auto& inst : npBB->getInstructions()) {
        assert(inst->getOpcode() != ir::Instruction::Div && "Div should be eliminated via magic multiplier!");
        assert(inst->getOpcode() != ir::Instruction::Rem && "Rem should be eliminated via magic multiplier!");
    }
    std::cout << "Signed Non-Power-of-Two Magic Division Test Passed!\n";
}

void test_signed_magic_execution() {
    std::cout << "Testing signed magic division and remainder execution...\n";
    ir::Module mod("signed_magic_execution");
    ir::IRBuilder builder;
    builder.setModule(&mod);
    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    const std::vector<int32_t> divisors = {
        3, 5, 7, 9, 11, 13, 17, 23, 31, 37, -3, -5, -7, -23, -1
    };

    transforms::DivisionStrengthReduction pass;
    for (size_t index = 0; index < divisors.size(); ++index) {
        for (bool remainder : {false, true}) {
            const std::string name = std::string(remainder ? "magic_rem_" : "magic_div_") +
                                     std::to_string(index);
            ir::Function* func = builder.createFunction(name, i32Ty, {i32Ty});
            ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
            builder.setInsertPoint(bb);
            ir::Value* input = func->getParameters().front().get();
            ir::Value* result = remainder
                ? static_cast<ir::Value*>(builder.createRem(input, ir::ConstantInt::get(i32Ty, divisors[index])))
                : static_cast<ir::Value*>(builder.createDiv(input, ir::ConstantInt::get(i32Ty, divisors[index])));
            builder.createRet(result);
            require(pass.run(*func), "signed constant division was not transformed");
            for (const auto& inst : bb->getInstructions()) {
                require(inst->getOpcode() != ir::Instruction::Div, "Div survived transformation");
                require(inst->getOpcode() != ir::Instruction::Rem, "Rem survived transformation");
            }
            transforms::LinearScanAllocator allocator;
            allocator.run(*func);
        }
    }

    auto targetInfo = target::TargetResolver::resolve({target::Arch::X64, target::OS::Linux});
    std::ostringstream assembly;
    codegen::CodeGen codegen(mod, std::move(targetInfo), &assembly);
    codegen.emit(false);
    const std::string assemblyText = assembly.str();
    require(assemblyText.find("idivl") == std::string::npos,
            "supported signed constants emitted idivl");

    const std::string stem = "/tmp/fyra_signed_magic_" + std::to_string(getpid());
    const std::string asmPath = stem + ".s";
    const std::string harnessPath = stem + ".c";
    const std::string binaryPath = stem;
    { std::ofstream output(asmPath); output << assemblyText; }
    {
        std::ofstream output(harnessPath);
        output << "#include <stdint.h>\n#include <limits.h>\n#include <stdio.h>\n";
        for (size_t index = 0; index < divisors.size(); ++index) {
            output << "extern int32_t magic_div_" << index << "(int32_t);\n"
                   << "extern int32_t magic_rem_" << index << "(int32_t);\n";
        }
        output << R"(
static uint32_t state = UINT32_C(0x6d2b79f5);
static int32_t next_value(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (int32_t)state;
}

static int check(int32_t n, int32_t d, int32_t q, int32_t r) {
    if (d == -1 && n == INT32_MIN)
        return (q == INT32_MIN && r == 0) ? 0 : 1;
    int32_t expected_q = n / d;
    int32_t expected_r = n % d;
    if (q == expected_q && r == expected_r) return 0;
    fprintf(stderr, "n=%d d=%d q=%d/%d r=%d/%d\n",
            n, d, q, expected_q, r, expected_r);
    return 1;
}
int main(void) {
    static const int32_t divisors[] = {3,5,7,9,11,13,17,23,31,37,-3,-5,-7,-23,-1};
    static const int32_t fixed[] = {0,1,-1,INT32_MAX,INT32_MIN,1235463397};
    int failed = 0;
)";
        for (size_t index = 0; index < divisors.size(); ++index) {
            output << "  { const int32_t d=divisors[" << index << "];\n"
                   << "    for (unsigned j=0;j<sizeof(fixed)/sizeof(fixed[0]);++j) { int32_t n=fixed[j]; failed |= check(n,d,magic_div_" << index << "(n),magic_rem_" << index << "(n)); }\n"
                   << "    const int64_t centers[] = {0,d,-(int64_t)d,2*(int64_t)d,-2*(int64_t)d,INT32_MIN,INT32_MAX};\n"
                   << "    for (unsigned j=0;j<sizeof(centers)/sizeof(centers[0]);++j) for (int k=-4;k<=4;++k) { int64_t wide=centers[j]+k; if (wide>=INT32_MIN && wide<=INT32_MAX) { int32_t n=(int32_t)wide; failed |= check(n,d,magic_div_" << index << "(n),magic_rem_" << index << "(n)); } }\n"
                   << "    for (unsigned j=0;j<20000;++j) { int32_t n=next_value(); failed |= check(n,d,magic_div_" << index << "(n),magic_rem_" << index << "(n)); }\n"
                   << "  }\n";
        }
        output << "  return failed != 0;\n}\n";
    }

    const std::string compile = "gcc -O2 -no-pie " + asmPath + " " + harnessPath + " -o " + binaryPath;
    require(std::system(compile.c_str()) == 0, "could not compile signed magic execution harness");
    require(std::system(binaryPath.c_str()) == 0, "signed magic execution mismatch");
    std::remove(asmPath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binaryPath.c_str());
    std::cout << "Signed magic execution test passed.\n";
}

void test_unsupported_divisor_is_unchanged() {
    ir::Module mod("division_fallback");
    ir::IRBuilder builder;
    builder.setModule(&mod);
    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    ir::Function* func = builder.createFunction("division_by_zero", i32Ty, {i32Ty});
    ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(bb);
    ir::Instruction* div = builder.createDiv(func->getParameters().front().get(),
                                              ir::ConstantInt::get(i32Ty, 0));
    builder.createRet(div);
    transforms::DivisionStrengthReduction pass;
    require(!pass.run(*func), "division by zero must not be transformed");
    require(div->getParent() == bb, "fallback removed division by zero");
    require(div->getOpcode() == ir::Instruction::Div, "fallback changed division by zero");
}

int main() {
    test_unsigned_div_rem();
    test_signed_div_rem();
    test_signed_magic_execution();
    test_unsupported_divisor_is_unchanged();
    std::cout << "All Division Strength Reduction Unit Tests Passed Successfully!\n";
    return 0;
}
