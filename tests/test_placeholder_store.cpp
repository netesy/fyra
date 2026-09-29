#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <iostream>

int main() {
    std::string test_file = "tests/test_placeholder_store.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("test_forward_ref");
    assert(func != nullptr);

    assert(func->getBasicBlocks().size() == 1);
    ir::BasicBlock* bb = func->getBasicBlocks().front().get();
    assert(bb != nullptr);

    // Instructions should be:
    // 1. store %val, %ptr : i32
    // 2. %ptr = add %base, 8 : i64
    // 3. ret 0 : i32
    auto& instrs = bb->getInstructions();
    assert(instrs.size() == 3);

    auto it = instrs.begin();
    ir::Instruction* store_instr = it->get();
    assert(store_instr->getOpcode() == ir::Instruction::Store);

    it++;
    ir::Instruction* add_instr = it->get();
    assert(add_instr->getOpcode() == ir::Instruction::Add);

    // Check that store's second operand (pointer) is exactly add_instr, and not null
    ir::Value* store_ptr = store_instr->getOperands()[1]->get();
    assert(store_ptr != nullptr);
    assert(store_ptr == add_instr);

    std::cout << "Placeholder store regression test passed!" << std::endl;
    return 0;
}
