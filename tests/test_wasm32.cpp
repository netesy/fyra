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
    std::string test_file = "tests/wasm32.fyra";
    std::ifstream input(test_file);
    if (!input.good()) return 1;

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    if (!module) return 1;

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for wasm32.fyra:\n" << generated_asm << std::endl;

    // Test for WebAssembly module structure
    assert(generated_asm.find("(module") != std::string::npos);
    assert(generated_asm.find("(func ") != std::string::npos);
    
    // Test for function calls and arithmetic operations
    assert(generated_asm.find("call ") != std::string::npos);
    assert(generated_asm.find("i32.add") != std::string::npos);
    
    // Test for control flow
    assert(generated_asm.find("br") != std::string::npos);
    assert(generated_asm.find("if") != std::string::npos);
    assert(generated_asm.find("end") != std::string::npos);
    assert(generated_asm.find("(export \"main\" (func $main))") != std::string::npos || generated_asm.find("(export \"main\" (func $") != std::string::npos);
    
    std::cout << "All WebAssembly tests passed!\n";

    return 0;
}
