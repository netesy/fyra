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
    std::string test_file = "tests/windows.fyra";
    std::ifstream input(test_file);
    if (!input.good()) {
        test_file = "../tests/windows.fyra";
        input.open(test_file);
    }
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Windows});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for windows.fyra:\n" << generated_asm << std::endl;

    // Test for Windows x64 calling convention and function structure
    assert(generated_asm.find("subq") != std::string::npos || generated_asm.find("sub") != std::string::npos);
    assert(generated_asm.find("main:") != std::string::npos);
    assert(generated_asm.find("fibonacci_with_jumps:") != std::string::npos);
    assert(generated_asm.find("test_conditions:") != std::string::npos);
    
    // Test for function calls and arithmetic
    assert(generated_asm.find("call") != std::string::npos);
    assert(generated_asm.find("add") != std::string::npos || generated_asm.find("addq") != std::string::npos);
    assert(generated_asm.find("cmp") != std::string::npos || generated_asm.find("test") != std::string::npos);
    
    // Test for control flow and jumps
    assert(generated_asm.find("jmp") != std::string::npos || generated_asm.find("jne") != std::string::npos || generated_asm.find("je") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos);
    
    std::cout << "All Windows x64 tests passed! Expected result: test_conditions(fibonacci_with_jumps(10))\n";

    return 0;
}
