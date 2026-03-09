#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/target/Wasm32.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <iostream>
#include <sstream>

int main() {
    // Create a simple test case directly in code
    std::stringstream input(R"(
export function $main() : w {
@start
    ret 42 : w
}
)");

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    std::cout << "Parsed module successfully\n";
    std::cout << "Functions in module: " << module->getFunctions().size() << "\n";

    // Generate .wat file
    {
        auto targetInfo = std::make_unique<codegen::target::Wasm32>();
        std::ofstream out("simple.wat");
        assert(out.good());
        codegen::CodeGen codeGen(*module, std::move(targetInfo), &out);
        codeGen.emit();
        std::cout << "Generated simple.wat\n";
    }

    // Generate .wasm file
    {
        auto targetInfo = std::make_unique<codegen::target::Wasm32>();
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        std::cout << "Generated binary size: " << code.size() << " bytes\n";

        if (code.size() >= 8) {
            // Check Wasm header
            bool headerOk = (code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);
            bool versionOk = (code[4] == 0x01 && code[5] == 0x00 && code[6] == 0x00 && code[7] == 0x00);
            
            std::cout << "WASM header valid: " << (headerOk ? "YES" : "NO") << "\n";
            std::cout << "WASM version valid: " << (versionOk ? "YES" : "NO") << "\n";

            // Write to file for inspection
            std::ofstream out("simple.wasm", std::ios::binary);
            out.write(reinterpret_cast<const char*>(code.data()), code.size());
            out.close();
            std::cout << "Written simple.wasm\n";
        } else {
            std::cout << "ERROR: Generated binary too small\n";
        }
    }

    return 0;
}