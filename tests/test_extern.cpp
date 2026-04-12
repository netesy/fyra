#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    std::string test_file = "tests/test_capabilities_all.fyra";
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
    bool foundMemoryAlloc = false;
    bool foundMemoryFree = false;
    bool foundRandomU64 = false;
    bool foundTimeNow = false;
    bool foundSyncMutexLock = false;
    bool foundIoOpen = false;

    for (auto& bb : func->getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::ExternCall) {
                auto* ei = dynamic_cast<ir::ExternCallInstruction*>(instr.get());
                if (ei->getCapability() == "io.write") foundIoWrite = true;
                if (ei->getCapability() == "io.open") foundIoOpen = true;
                if (ei->getCapability() == "process.exit") foundProcessExit = true;
                if (ei->getCapability() == "memory.alloc") foundMemoryAlloc = true;
                if (ei->getCapability() == "memory.free") foundMemoryFree = true;
                if (ei->getCapability() == "random.u64") foundRandomU64 = true;
                if (ei->getCapability() == "time.now") foundTimeNow = true;
                if (ei->getCapability() == "sync.mutex.lock") foundSyncMutexLock = true;
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
    if (!foundMemoryAlloc) {
        std::cerr << "memory.alloc extern call not found" << std::endl;
        return 1;
    }
    if (!foundMemoryFree) {
        std::cerr << "memory.free extern call not found" << std::endl;
        return 1;
    }
    if (!foundRandomU64) {
        std::cerr << "random.u64 extern call not found" << std::endl;
        return 1;
    }
    if (!foundTimeNow) {
        std::cerr << "time.now extern call not found" << std::endl;
        return 1;
    }
    if (!foundSyncMutexLock) {
        std::cerr << "sync.mutex.lock extern call not found" << std::endl;
        return 1;
    }
    if (!foundIoOpen) {
        std::cerr << "io.open extern call not found" << std::endl;
        return 1;
    }

    std::cout << "Extern test passed!" << std::endl;
    return 0;
}
