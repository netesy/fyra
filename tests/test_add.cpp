#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "transforms/CFGBuilder.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "codegen/abi/ABIAnalysis.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    std::string test_file;
    if (std::string("tests/test_add.cpp").find("windows") != std::string::npos) test_file = "tests/windows.fyra";
    else if (std::string("tests/test_add.cpp").find("aarch64") != std::string::npos) test_file = "tests/aarch64.fyra";
    else if (std::string("tests/test_add.cpp").find("extern") != std::string::npos) test_file = "tests/test_capabilities_all.fyra";
    else if (std::string("tests/test_add.cpp").find("functions") != std::string::npos) test_file = "tests/functions.fyra";
    else if (std::string("tests/test_add.cpp").find("add") != std::string::npos) test_file = "tests/add.fyra";

    std::ifstream input(test_file);
    if (!input.good()) input.open("../" + test_file);
    if (!input.good()) {
        std::cerr << "Could not open test file: " << test_file << std::endl;
        return 1;
    }

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    if (!module) return 1;

    target::TargetDescriptor desc;
    if (std::string("tests/test_add.cpp").find("windows") != std::string::npos) desc = {target::Arch::X64, target::OS::Windows};
    else if (std::string("tests/test_add.cpp").find("aarch64") != std::string::npos) desc = {target::Arch::AArch64, target::OS::Linux};
    else desc = {target::Arch::X64, target::OS::Linux};

    auto targetInfo = target::TargetResolver::resolve(desc);

    for (auto& func : module->getFunctions()) {
        transforms::CFGBuilder::run(*func);
        transforms::ABIAnalysis abi(target::TargetResolver::resolve(desc));
        abi.run(*func);
        transforms::RegAllocRewriter rewriter;
        rewriter.run(*func);
    }

    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();
    std::string generated_asm = ss.str();

    if (std::string("tests/test_add.cpp").find("extern") != std::string::npos) {
        assert(generated_asm.find("io.write") != std::string::npos || generated_asm.find("syscall") != std::string::npos || generated_asm.find("call") != std::string::npos);
        std::cout << "Extern test passed!" << std::endl;
    } else {
        assert(generated_asm.find("ret") != std::string::npos);
    }

    return 0;
}
