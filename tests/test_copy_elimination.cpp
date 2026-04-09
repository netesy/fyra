#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "transforms/CopyElimination.h"
#include "codegen/CodeGen.h"
#include "codegen/target/SystemV_x64.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <iostream>

int main() {
    std::ifstream input(TEST_FILE);
    assert(input.good());

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("main");
    assert(func != nullptr);

    transforms::CopyElimination copyElim;
    copyElim.run(*func);

    auto targetInfo = std::make_unique<codegen::target::SystemV_x64>();
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for copy_elimination.fyra:\n" << generated_asm << std::endl;

    // After copy elimination and SCCP, the operation may be folded or registers renamed.
    // We check for the final constant result 15 being moved into the return register.
    assert(generated_asm.find("movl $15, %eax") != std::string::npos ||
           generated_asm.find("movl $c, %eax") != std::string::npos);

    return 0;
}
