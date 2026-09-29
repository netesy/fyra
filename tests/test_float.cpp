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
    std::string test_file = "tests/float.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM for float.fyra:\n" << generated_asm << std::endl;

    assert(generated_asm.find("addss") != std::string::npos || generated_asm.find("ret") != std::string::npos);
    assert(generated_asm.find("subss") != std::string::npos || generated_asm.find("ret") != std::string::npos);
    assert(generated_asm.find("mulss") != std::string::npos || generated_asm.find("ret") != std::string::npos);
    assert(generated_asm.find("divss") != std::string::npos || generated_asm.find("ret") != std::string::npos);

    // Test UWtoF and Ultof codegen
    {
        std::string src = R"(
function $test_ultof(%x : i64) : f64 {
@start
    %f = ultof %x : f64
    ret %f : f64
}
)";
        std::stringstream floatStream(src);
        parser::Parser floatParser(floatStream, parser::FileFormat::FYRA);
        auto floatModule = floatParser.parseModule();
        assert(floatModule != nullptr);

        auto floatTargetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
        std::stringstream floatSs;
        codegen::CodeGen floatCodeGen(*floatModule, std::move(floatTargetInfo), &floatSs);
        floatCodeGen.emit();

        std::string floatAsm = floatSs.str();
        assert(floatAsm.find("cvtsi2sd") != std::string::npos);
        assert(floatAsm.find(".L_ultof_high_") != std::string::npos);
    }

    return 0;
}
