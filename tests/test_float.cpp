#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/target/SystemV_x64.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main() {
    std::string test_file = "tests/float.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = std::make_unique<codegen::target::SystemV_x64>();
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for float.fyra:\n" << generated_asm << std::endl;

    assert(generated_asm.find("addss") != std::string::npos);
    assert(generated_asm.find("subss") != std::string::npos);
    assert(generated_asm.find("mulss") != std::string::npos);
    assert(generated_asm.find("divss") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos);

    return 0;
}
