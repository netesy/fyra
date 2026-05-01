#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <vector>
#include <iostream>
#include <sstream>

int main() {
    std::string source_code = "export function $main() : w {\n@start\n    ret 42 : w\n}";
    std::stringstream input(source_code);

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
    codegen::CodeGen codeGen(*module, std::move(targetInfo));
    codeGen.emit();

    const auto& code = codeGen.getAssembler().getCode();
    assert(code.size() > 8);

    // Check Wasm header
    assert(code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);
    // Check Wasm version
    assert(code[4] == 0x01 && code[5] == 0x00 && code[6] == 0x00 && code[7] == 0x00);

    // Write to file for inspection
    std::ofstream out("test.wasm", std::ios::binary);
    out.write(reinterpret_cast<const char*>(code.data()), code.size());
    out.close();

    return 0;
}