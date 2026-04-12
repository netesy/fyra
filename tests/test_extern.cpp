#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    std::string test_file = "tests/test_extern.fyra";
    std::ifstream input(test_file);
    if (!input.good()) {
        std::cerr << "Could not open test file: " << test_file << std::endl;
        return 1;
    }

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    if (module == nullptr) {
        std::cerr << "Failed to parse module" << std::endl;
        return 1;
    }

    ir::Function* func = module->getFunction("main");
    if (func == nullptr) {
        std::cerr << "Function 'main' not found" << std::endl;
        return 1;
    }

    bool foundIoWrite = false;
    bool foundProcessExit = false;

    for (auto& bb : func->getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::ExternCall) {
                auto* ei = dynamic_cast<ir::ExternCallInstruction*>(instr.get());
                if (ei->getCapability() == "io.write") foundIoWrite = true;
                if (ei->getCapability() == "process.exit") foundProcessExit = true;
            }
        }
    }

    if (!foundIoWrite) {
        std::cerr << "io.write extern call not found" << std::endl;
        return 1;
    }
    if (!foundProcessExit) {
        std::cerr << "process.exit extern call not found" << std::endl;
        return 1;
    }

    std::cout << "Extern test passed!" << std::endl;
    return 0;
}
