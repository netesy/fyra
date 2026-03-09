#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/target/AArch64.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main() {
    std::string test_file = "tests/aarch64.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = std::make_unique<codegen::target::AArch64>();
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for aarch64.fyra:\n" << generated_asm << std::endl;

    // Test for AArch64 function structure
    assert(generated_asm.find("main:") != std::string::npos);
    assert(generated_asm.find("fibonacci_optimized:") != std::string::npos);
    assert(generated_asm.find("sum_fibonacci_series:") != std::string::npos);
    assert(generated_asm.find("factorial:") != std::string::npos);
    
    // Test for AArch64 instructions and operations
    assert(generated_asm.find("mov") != std::string::npos);
    assert(generated_asm.find("add") != std::string::npos);
    assert(generated_asm.find("mul") != std::string::npos);
    assert(generated_asm.find("cmp") != std::string::npos);
    
    // Test for function calls and control flow
    assert(generated_asm.find("bl") != std::string::npos || generated_asm.find("call") != std::string::npos);
    assert(generated_asm.find("b.") != std::string::npos || generated_asm.find("b ") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos);
    
    std::cout << "All AArch64 tests passed! Expected result: fibonacci(10) + sum_series(10) + factorial(5)\n";

    return 0;
}
