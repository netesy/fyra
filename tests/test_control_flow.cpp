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
    std::string test_file = "tests/control_flow.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = codegen::target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for control_flow.fyra:\n" << generated_asm << std::endl;

    assert(generated_asm.find("jmp") != std::string::npos);
    assert(generated_asm.find("jne") != std::string::npos); // br is translated to jne
    assert(generated_asm.find("cmp") != std::string::npos); // slt is translated to cmp
    assert(generated_asm.find("ret") != std::string::npos);

    return 0;
}
