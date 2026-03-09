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
    std::string test_file = "tests/div.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = std::make_unique<codegen::target::SystemV_x64>();
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for div.fyra:\n" << generated_asm << std::endl;

    assert(generated_asm.find("idiv") != std::string::npos);
    assert(generated_asm.find("100") != std::string::npos);
    assert(generated_asm.find("10") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos);

    return 0;
}
