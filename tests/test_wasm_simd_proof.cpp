#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <iostream>
#include <sstream>

int main() {
    std::stringstream input(R"(
export function $vector_add(%a: v4i32, %b: v4i32) : v4i32 {
@start
    %res = vadd.128 %a, %b : v4i32
    ret %res : v4i32
}
)");

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});

    // 1. Generate WAT
    std::stringstream watStream;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &watStream);
    codeGen.emit();

    std::cout << "--- Generated WAT Output ---" << std::endl;
    std::cout << watStream.str() << std::endl;

    // 2. Generate WASM Binary
    auto wasmTarget = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
    codegen::CodeGen codeGenBin(*module, std::move(wasmTarget));
    codeGenBin.emit();

    const auto& code = codeGenBin.getAssembler().getCode();
    std::cout << "Generated WASM Binary Size: " << code.size() << " bytes" << std::endl;

    std::cout << "--- WASM Binary Hex ---" << std::endl;
    for (size_t i = 0; i < code.size(); ++i) {
        printf("%02x ", code[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    return 0;
}
