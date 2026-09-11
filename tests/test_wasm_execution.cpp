#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/architecture/wasm32/WasmModule.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <cstdlib>

static std::string toHex(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    for (uint8_t b : data) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

int main() {
    std::cout << "=== WASM Target Execution & Verification Test Suite ===" << std::endl;

    // Test 1: Minimal fixture (ret 42)
    {
        std::string src = "export function $main() : i32 {\n@start\n    ret 42 : i32\n}";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        std::string hex = toHex(code);
        std::cout << "Fixture WASM hex (" << code.size() << " bytes): " << hex << std::endl;

        assert(code.size() == 38 || code.size() == 37 || code.size() == 39);
        assert(code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);

        // Test determinism
        auto targetInfo2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen2(*module, std::move(targetInfo2));
        codeGen2.emit();
        assert(codeGen.getAssembler().getCode() == codeGen2.getAssembler().getCode());
        std::cout << "Determinism check passed for minimal fixture." << std::endl;
    }

    // Test 2: Multi-function parameterized call ($add(20, 22) -> 42)
    {
        std::string src = R"(
function $add(%a : i32, %b : i32) : i32 {
@start
    %r = add %a, %b : i32
    ret %r : i32
}

export function $main() : i32 {
@start
    %r = call $add(i32 20, i32 22) : i32
    ret %r : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        // WAT text generation test
        std::stringstream watStream;
        auto targetInfoWat = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat(*module, std::move(targetInfoWat), &watStream);
        codeGenWat.emit();
        std::string wat = watStream.str();
        std::cout << "Generated WAT:\n" << wat << std::endl;
        assert(wat.find("i32.add") != std::string::npos);
        assert(wat.find("call $add") != std::string::npos);

        // Binary generation test
        auto targetInfoBin = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin(*module, std::move(targetInfoBin));
        codeGenBin.emit();

        const auto& code = codeGenBin.getAssembler().getCode();
        std::cout << "Multi-function WASM size: " << code.size() << " bytes" << std::endl;

        std::ofstream out("multifunc.wasm", std::ios::binary);
        out.write(reinterpret_cast<const char*>(code.data()), code.size());
        out.close();

        // Independent runtime validation & execution check
        int sysRes = std::system("node -e 'const fs=require(\"fs\"); const m=new WebAssembly.Module(fs.readFileSync(\"multifunc.wasm\")); const i=new WebAssembly.Instance(m); if (i.exports.main() !== 42) process.exit(1);'");
        assert(sysRes == 0);
        std::cout << "Node.js WebAssembly.instantiate verification passed: main() == 42" << std::endl;

        // Determinism check
        std::stringstream watStream2;
        auto targetInfoWat2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat2(*module, std::move(targetInfoWat2), &watStream2);
        codeGenWat2.emit();
        assert(watStream.str() == watStream2.str());

        auto targetInfoBin2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin2(*module, std::move(targetInfoBin2));
        codeGenBin2.emit();
        assert(codeGenBin.getAssembler().getCode() == codeGenBin2.getAssembler().getCode());
        std::cout << "Determinism check passed for multi-function module." << std::endl;
    }

    std::cout << "All WASM target execution and verification tests passed successfully!" << std::endl;
    return 0;
}
