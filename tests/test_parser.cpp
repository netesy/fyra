#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <iostream>

int main() {
    std::string test_file = "tests/simple.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("main");
    assert(func != nullptr);

    assert(func->getBasicBlocks().size() == 1);
    ir::BasicBlock* bb = func->getBasicBlocks().front().get();
    assert(bb != nullptr);

    // There should be one instruction: ret i32 42
    assert(bb->getInstructions().size() == 1);
    ir::Instruction* instr = bb->getInstructions().front().get();
    assert(instr->getOpcode() == ir::Instruction::Ret);
    assert(instr->getOperands().size() == 1);
    ir::Value* retVal = instr->getOperands()[0]->get();
    ir::ConstantInt* constInt = dynamic_cast<ir::ConstantInt*>(retVal);
    assert(constInt != nullptr);
    assert(constInt->getValue() == 42);

    std::cout << "Parser test passed!" << std::endl;

    return 0;
}
