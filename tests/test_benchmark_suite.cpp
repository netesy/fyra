#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>
#include <vector>

bool verify_target(ir::Module& module, const std::string& targetName) {
    std::cout << "Testing target: " << targetName << "..." << std::endl;

    auto targetInfo = codegen::CodeGen::createTargetInfoForName(targetName);
    if (!targetInfo) {
        std::cerr << "  Failed to create target info for " << targetName << std::endl;
        return false;
    }

    // 1. Test Textual Assembly
    std::stringstream ss;
    codegen::CodeGen codeGenText(module, codegen::CodeGen::createTargetInfoForName(targetName), &ss);
    codeGenText.emit();
    std::string generated_asm = ss.str();

    if (generated_asm.empty()) {
        std::cerr << "  Generated assembly is empty for " << targetName << std::endl;
        return false;
    }

    std::cout << "  Textual assembly generated (" << generated_asm.size() << " bytes)." << std::endl;

    // 2. Test In-Memory Binary Generation
    auto targetInfoBin = codegen::CodeGen::createTargetInfoForName(targetName);
    if (!targetInfoBin) {
        std::cerr << "  Failed to create target info for binary generation " << targetName << std::endl;
        return false;
    }
    codegen::CodeGen codeGenBin(module, std::move(targetInfoBin), nullptr);
    codeGenBin.emit();
    const auto& code = codeGenBin.getAssembler().getCode();

    if (code.empty() && targetName != "wasm32") {
        std::cerr << "  Generated binary code is empty for " << targetName << std::endl;
        return false;
    }

    std::cout << "  In-memory binary generated (size: " << code.size() << " bytes)." << std::endl;
    return true;
}

int main() {
    std::string test_file = "tests/benchmark_suite.fyra";
    std::ifstream input(test_file);
    if (!input.good()) {
        std::cerr << "Could not open " << test_file << std::endl;
        return 1;
    }

    std::vector<std::string> targets = {
        "linux",
        "windows-amd64",
        "aarch64",
        "wasm32",
        "riscv64"
    };

    bool all_passed = true;
    for (const auto& target : targets) {
        input.clear();
        input.seekg(0);
        parser::Parser p(input, parser::FileFormat::FYRA);
        auto m = p.parseModule();
        if (!m) {
            std::cerr << "Failed to parse module for target " << target << std::endl;
            all_passed = false;
            continue;
        }
        if (!verify_target(*m, target)) {
            all_passed = false;
        }
    }

    if (all_passed) {
        std::cout << "\nAll targets passed the benchmark suite test!" << std::endl;
        return 0;
    } else {
        std::cerr << "\nSome targets failed." << std::endl;
        return 1;
    }
}
