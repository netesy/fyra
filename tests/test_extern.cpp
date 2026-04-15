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
    bool foundFsMkdir = false;
    bool foundFsStat = false;
    bool foundThreadSpawn = false;
    bool foundNetConnect = false;

    for (auto& bb : func->getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::ExternCall) {
                auto* ei = dynamic_cast<ir::ExternCallInstruction*>(instr.get());
                if (ei->getCapability() == "io.write") foundIoWrite = true;
                if (ei->getCapability() == "io.open" || ei->getCapability() == "fs.open") foundIoOpen = true;
                if (ei->getCapability() == "process.exit") foundProcessExit = true;
                if (ei->getCapability() == "memory.alloc") foundMemoryAlloc = true;
                if (ei->getCapability() == "memory.free") foundMemoryFree = true;
                if (ei->getCapability() == "random.u64") foundRandomU64 = true;
                if (ei->getCapability() == "time.now") foundTimeNow = true;
                if (ei->getCapability() == "sync.mutex.lock") foundSyncMutexLock = true;
                if (ei->getCapability() == "fs.mkdir") foundFsMkdir = true;
                if (ei->getCapability() == "fs.stat") foundFsStat = true;
                if (ei->getCapability() == "thread.spawn") foundThreadSpawn = true;
                if (ei->getCapability() == "net.connect") foundNetConnect = true;
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
        std::cerr << "io.open/fs.open extern call not found" << std::endl;
        return 1;
    }
    // Note: We might be using tests/capability_audit.fyra which has more caps
    // The original test used tests/test_capabilities_all.fyra
    
    // Let's also test the new audit file if it exists in a second pass or just swap it
    
    std::cout << "Initial Extern test passed!" << std::endl;
    
    // Second pass with the new audit file
    std::string audit_file = "tests/capability_audit.fyra";
    std::ifstream audit_input(audit_file);
    if (audit_input.good()) {
        parser::Parser p2(audit_input, parser::FileFormat::FYRA);
        std::cout << "Parsing audit file..." << std::endl;
        auto mod2 = p2.parseModule();
        if (mod2) {
            std::cout << "Audit module parsed successfully." << std::endl;
            auto* f2 = mod2->getFunction("main");
            if (!f2) {
                std::cerr << "Audit function 'main' not found" << std::endl;
                return 1;
            }
            for (auto& bb : f2->getBasicBlocks()) {
                for (auto& instr : bb->getInstructions()) {
                    if (instr->getOpcode() == ir::Instruction::ExternCall) {
                        auto* ei = dynamic_cast<ir::ExternCallInstruction*>(instr.get());
                        if (ei->getCapability() == "fs.mkdir") foundFsMkdir = true;
                        if (ei->getCapability() == "fs.stat") foundFsStat = true;
                        if (ei->getCapability() == "thread.spawn") foundThreadSpawn = true;
                        // net.connect is in audit
                        if (ei->getCapability() == "net.connect") foundNetConnect = true;
                    }
                }
            }
            if (!foundFsMkdir || !foundFsStat || !foundThreadSpawn) {
                std::cerr << "New filesystem or threading capabilities not found in audit file" << std::endl;
                return 1;
            }
            std::cout << "Capability audit test passed!" << std::endl;
        }
    }

    std::cout << "All Extern tests passed!" << std::endl;
    return 0;
}
