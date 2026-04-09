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

    auto targetInfo = codegen::CodeGenFactory::createTargetInfo(targetName);
    if (!targetInfo) {
        std::cerr << "  Failed to create target info for " << targetName << std::endl;
        return false;
    }

    // 1. Test Textual Assembly
    std::stringstream ss;
    auto& func = *module.getFunctions().front();
    if (!func.getBasicBlocks().empty()) {
        auto& bb = *func.getBasicBlocks().front();
        auto add = std::make_unique<ir::Instruction>(ir::IntegerType::get(32), ir::Instruction::Add, std::vector<ir::Value*>{}, &bb);
        // Add dummy call to prevent leaf optimization
        auto call = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Call, std::vector<ir::Value*>{&func}, &bb);
        bb.getInstructions().insert(bb.getInstructions().begin(), std::move(add));
        bb.getInstructions().insert(bb.getInstructions().begin(), std::move(call));
    }
    codegen::CodeGen codeGenText(module, codegen::CodeGenFactory::createTargetInfo(targetName), &ss);
    codeGenText.emit();
    std::string generated_asm = ss.str();

    if (generated_asm.empty()) {
        std::cerr << "  Generated assembly is empty for " << targetName << std::endl;
        return false;
    }

    // Deeper Verification of Textual Assembly
    std::vector<std::pair<std::string, std::string>> checks;
    if (targetName == "linux") {
        checks = {{"push %rbp", "prologue"}, {"ret", "return"}};
    } else if (targetName == "aarch64") {
        checks = {{"stp x29, x30", "prologue"}, {"ret", "return"}};
    } else if (targetName == "riscv64") {
        checks = {{"addi sp, sp, -", "prologue"}, {"sd ra,", "save ra"}, {"jr ra", "return"}};
    } else if (targetName.find("windows") != std::string::npos) {
        if (targetName.find("arm64") != std::string::npos) {
             checks = {{"stp x29, x30", "prologue"}, {"ret", "return"}};
        } else {
             checks = {{"push %rbp", "prologue"}, {"leave", "epilogue"}, {"ret", "return"}};
        }
    } else if (targetName == "wasm32") {
        checks = {{"(module", "wasm header"}, {"(func", "function declaration"}};
    }

    for (const auto& check : checks) {
        if (generated_asm.find(check.first) == std::string::npos) {
            std::cerr << "  Missing " << check.second << " ('" << check.first << "') in assembly for " << targetName << std::endl;
            return false;
        }
    }

    std::cout << "  Textual assembly verified (" << checks.size() << " architectural hallmarks found)." << std::endl;

    // 2. Test In-Memory Binary Generation
    auto targetInfoBin = codegen::CodeGenFactory::createTargetInfo(targetName);
    if (!targetInfoBin) {
        std::cerr << "  Failed to create target info for binary generation " << targetName << std::endl;
        return false;
    }
    codegen::CodeGen codeGenBin(module, std::move(targetInfoBin), nullptr);
    codeGenBin.emit();
    const auto& code = codeGenBin.getAssembler().getCode();

    if (code.empty()) {
        std::cerr << "  Generated binary code is empty for " << targetName << std::endl;
        return false;
    }

    if (targetName == "wasm32") {
        if (code.size() < 8 || code[0] != 0x00 || code[1] != 0x61 || code[2] != 0x73 || code[3] != 0x6d) {
            std::cerr << "  Invalid WASM magic bytes for " << targetName << std::endl;
            return false;
        }
    } else {
        // Basic check for binary instruction density
        // A function with many instructions should have a reasonable size
        if (code.size() < 100) {
            std::cerr << "  Binary code seems too small (" << code.size() << " bytes) for " << targetName << std::endl;
            return false;
        }
    }

    std::cout << "  In-memory binary verified (size: " << code.size() << " bytes)." << std::endl;
    return true;
}

int main() {
    std::string test_file = "tests/benchmark_suite.fyra";
    std::ifstream input(test_file);
    if (!input.good()) {
        std::cerr << "Could not open " << test_file << std::endl;
        return 1;
    }

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    if (!module) {
        std::cerr << "Failed to parse " << test_file << std::endl;
        return 1;
    }

    std::vector<std::string> targets = {
        "linux",
        "windows-amd64",
        "windows-arm64",
        "aarch64",
        "wasm32",
        "riscv64"
    };

    bool all_passed = true;
    for (const auto& target : targets) {
        // Re-parse module for each target to avoid target-dependent IR modifications
        // (like instruction fusion) from interfering with other targets.
        input.clear();
        input.seekg(0);
        parser::Parser p(input, parser::FileFormat::FYRA);
        auto m = p.parseModule();
        if (!m) {
            std::cerr << "Failed to re-parse module for target " << target << std::endl;
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
        std::cerr << "\nSome targets failed the benchmark suite test." << std::endl;
        return 1;
    }
}
