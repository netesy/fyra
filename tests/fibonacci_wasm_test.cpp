#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/target/Wasm32.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>

int main() {
    std::ifstream in("tests/fibonacci.fyra");
    if (!in.good()) {
        std::cerr << "Could not open fibonacci.fyra file\n";
        return 1;
    }

    parser::Parser parser(in, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    // Generate .wat file
    {
        auto targetInfo = std::make_unique<codegen::target::Wasm32>();
        std::ofstream out("fibonacci.wat");
        assert(out.good());
        codegen::CodeGen codeGen(*module, std::move(targetInfo), &out);
        codeGen.emit();
    }

    std::ifstream watIn("fibonacci.wat");
    assert(watIn.good());
    std::string wat((std::istreambuf_iterator<char>(watIn)), std::istreambuf_iterator<char>());
    assert(wat.find("if (result i32)") != std::string::npos);
    assert(wat.find("block $base_case") == std::string::npos);
    assert(wat.find("local.set") == std::string::npos);

    // Generate .wasm file
    {
        auto targetInfo = std::make_unique<codegen::target::Wasm32>();
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        assert(code.size() > 8);

        // Check Wasm header
        assert(code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);
        // Check Wasm version
        assert(code[4] == 0x01 && code[5] == 0x00 && code[6] == 0x00 && code[7] == 0x00);

        // Write to file for inspection
        std::ofstream out("fibonacci.wasm", std::ios::binary);
        out.write(reinterpret_cast<const char*>(code.data()), code.size());
        out.close();

        // Print hex values
        std::cout << "Generated .wasm binary (hex):" << std::endl;
        for (uint8_t byte : code) {
            std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}