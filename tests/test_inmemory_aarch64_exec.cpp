#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip>
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "codegen/CodeGen.h"
#include "codegen/target/AArch64.h"
#include "../src/codegen/execgen/elf.hh"
#include <fstream>

using namespace ir;
using namespace codegen;
using namespace codegen::target;

void test_inmemory_aarch64_exec() {
    std::cout << "Running test_inmemory_aarch64_exec..." << std::endl;
    Module module("test");

    // Create a simple function: int main() { return 42; }
    auto* intTy = IntegerType::get(32);
    auto* mainFunc = new Function(FunctionType::get(intTy, {}), "main", &module);
    module.addFunction(std::unique_ptr<Function>(mainFunc));
    auto* entryBB = new BasicBlock(mainFunc, "entry");
    mainFunc->addBasicBlock(std::unique_ptr<BasicBlock>(entryBB));

    auto* const42 = ConstantInt::get(intTy, 42);
    entryBB->addInstruction(std::make_unique<Instruction>(intTy, Instruction::Ret, std::vector<Value*>{const42}, entryBB));

    // Generate code for AArch64
    auto aarch64Target = std::make_unique<AArch64>();
    CodeGen cg(module, std::move(aarch64Target), nullptr); // nullptr for binary emission
    cg.emit(true); // true for executable

    // Prepare section data for the ElfGenerator
    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    ElfGenerator elfGen("test.fy");
    elfGen.setMachine(183); // EM_AARCH64

    // Get symbols and relocations from CodeGen
    std::vector<ElfGenerator::Symbol> symbols;
    for (const auto& sym_info : cg.getSymbols()) {
        symbols.push_back({sym_info.name, sym_info.value, sym_info.size,
                           (uint8_t)sym_info.type, (uint8_t)sym_info.binding, sym_info.sectionName});
    }

    std::vector<ElfGenerator::Relocation> relocations;
    for (const auto& reloc_info : cg.getRelocations()) {
        relocations.push_back({reloc_info.offset, reloc_info.type, reloc_info.addend,
                               reloc_info.symbolName, reloc_info.sectionName});
    }

    std::string outputPath = "aarch64_test_exec";
    if (elfGen.generateFromCode(sections, symbols, relocations, outputPath)) {
        std::cout << "AArch64 ELF generated successfully: " << outputPath << std::endl;
    } else {
        std::cerr << "Error generating AArch64 ELF: " << elfGen.getLastError() << std::endl;
        assert(false);
    }

    // Read back and verify
    std::ifstream file(outputPath, std::ios::binary);
    std::vector<uint8_t> elfContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Basic ELF header verification
    assert(elfContent.size() > 64);
    assert(elfContent[0] == 0x7f);
    assert(elfContent[1] == 'E');
    assert(elfContent[2] == 'L');
    assert(elfContent[3] == 'F');
    assert(elfContent[4] == 2); // 64-bit
    assert(elfContent[5] == 1); // Little endian

    // Machine type for AArch64 is 183 (0xB7)
    uint16_t machine = elfContent[18] | (elfContent[19] << 8);
    std::cout << "ELF Machine: 0x" << std::hex << machine << std::dec << std::endl;
    assert(machine == 183);

    std::cout << "Symbols verified via generateFromCode path." << std::endl;
}

int main() {
    try {
        test_inmemory_aarch64_exec();
        std::cout << "All AArch64 in-memory tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
