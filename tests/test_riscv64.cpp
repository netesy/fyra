#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/target/RiscV64.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main() {
    std::string test_file = "tests/riscv64.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = std::make_unique<codegen::target::RiscV64>();
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

    return 0;
}
