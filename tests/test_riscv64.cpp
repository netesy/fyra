#include "parser/Parser.h"
#include "ir/Module.h"
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

int main() {
    std::string test_file = "tests/riscv64.fyra";
    std::ifstream input(test_file);
    if (!input.good()) input.open("../" + test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::RISCV64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for riscv64.fyra:\n" << generated_asm << std::endl;

    // Test for RISC-V function structure
    assert(generated_asm.find("main:") != std::string::npos);
    assert(generated_asm.find("fibonacci_wrapper:") != std::string::npos);
    assert(generated_asm.find("fibonacci_tail_recursive:") != std::string::npos);
    assert(generated_asm.find("power_of_two:") != std::string::npos);
    assert(generated_asm.find("gcd:") != std::string::npos);
    
    // Test for RISC-V instructions and operations
    assert(generated_asm.find("add") != std::string::npos || generated_asm.find("addi") != std::string::npos);
    assert(generated_asm.find("mul") != std::string::npos);
    assert(generated_asm.find("mv") != std::string::npos || generated_asm.find("li") != std::string::npos);
    
    // Test for function calls and control flow
    assert(generated_asm.find("call") != std::string::npos || generated_asm.find("jal") != std::string::npos);
    assert(generated_asm.find("beq") != std::string::npos || generated_asm.find("bne") != std::string::npos || generated_asm.find("j ") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos || generated_asm.find("jr ra") != std::string::npos);
    
    std::cout << "All RISC-V tests passed! Expected result: fibonacci(10) + power_of_two(4) + gcd(fibonacci(10), power_of_two(4))\n";

    // Additional Lowering & Fusion Tests for RISC-V 64
    {
        auto testContext = std::make_shared<ir::IRContext>();
        ir::Module testModule("riscv64_test_advanced", testContext);
        ir::IRBuilder builder(testContext);
        builder.setModule(&testModule);

        auto* i32 = testContext->getIntegerType(32);
        auto* i64 = testContext->getIntegerType(64);
        auto* f32 = testContext->getFloatType();

        ir::Function* func = builder.createFunction("test_riscv64_advanced", i32, {i32, i32, i64});
        auto paramIt = func->getParameters().begin();
        ir::Value* a = paramIt->get(); ++paramIt;
        ir::Value* b = paramIt->get(); ++paramIt;
        ir::Value* c = paramIt->get();

        ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
        ir::BasicBlock* thenBB = builder.createBasicBlock("then", func);
        ir::BasicBlock* elseBB = builder.createBasicBlock("else", func);

        builder.setInsertPoint(entry);
        ir::Instruction* mul = builder.createMul(a, b);
        ir::Instruction* madd = builder.createAdd(mul, a);

        ir::Instruction* minVal = builder.createSMin(a, b);
        ir::Instruction* maxVal = builder.createSMax(minVal, a);

        ir::Instruction* cmp = builder.createCslt(madd, maxVal);
        builder.createBr(cmp, thenBB, elseBB);

        builder.setInsertPoint(thenBB);
        ir::Instruction* ext = builder.createExtSW(madd, i64);
        ir::Instruction* fcvt = builder.createSWtoF(ext, f32);
        ir::Instruction* icvt = builder.createSToUI(fcvt, i32);
        builder.createRet(icvt);

        builder.setInsertPoint(elseBB);
        builder.createRet(maxVal);

        std::stringstream asmStream;
        codegen::CodeGen testCodeGen(testModule, target::TargetResolver::resolve({target::Arch::RISCV64, target::OS::Linux}), &asmStream);
        testCodeGen.emit();
        std::string testAsm = asmStream.str();

        assert(testAsm.find("blt") != std::string::npos || testAsm.find("bge") != std::string::npos && "RISC-V branch fusion MUST generate blt or bge!");
        assert(testAsm.find("min") != std::string::npos && "RISC-V smin MUST generate min!");
        assert(testAsm.find("max") != std::string::npos && "RISC-V smax MUST generate max!");
        assert(testAsm.find("sext.w") != std::string::npos && "RISC-V ExtSW MUST generate sext.w!");
        std::cout << "RISC-V 64 advanced lowering and fusion tests passed!" << std::endl;
    }

    return 0;
}
