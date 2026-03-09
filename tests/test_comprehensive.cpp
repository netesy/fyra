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
    std::ifstream input(TEST_FILE);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    // --- Verify data sections ---
    ir::GlobalVariable* my_string = nullptr;
    ir::GlobalVariable* my_array = nullptr;
    for (auto& gv : module->getGlobalVariables()) {
        if (gv->getName() == "my_string") {
            my_string = gv.get();
        } else if (gv->getName() == "my_array") {
            my_array = gv.get();
        }
    }
    assert(my_string != nullptr);
    assert(my_string->getType()->isArrayTy());
    assert(my_array != nullptr);
    assert(my_array->getType()->isArrayTy());

    // --- Verify type definitions ---
    ir::Type* my_struct = module->getType("my_struct");
    assert(my_struct != nullptr);
    assert(my_struct->isStructTy());

    // --- Verify functions ---
    ir::Function* test_params = module->getFunction("test_params");
    assert(test_params != nullptr);
    assert(test_params->getParameters().size() == 4);

    ir::Function* variadic_func = module->getFunction("variadic_func");
    assert(variadic_func != nullptr);
    assert(variadic_func->isVariadic());

    ir::Function* main_func = module->getFunction("main");
    assert(main_func != nullptr);
    assert(main_func->isExported());

    assert(main_func->getBasicBlocks().size() == 2);
    ir::BasicBlock* start_bb = main_func->getBasicBlocks().front().get();
    assert(start_bb->getName() == "start");

    auto& instructions = start_bb->getInstructions();
    auto it = instructions.begin();

    // Arithmetic
    assert((*it)->getOpcode() == ir::Instruction::Add); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Sub); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Mul); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Div); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Udiv); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Rem); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Urem); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Neg); ++it;

    // Floating point arithmetic
    assert((*it)->getOpcode() == ir::Instruction::FAdd); ++it;
    assert((*it)->getOpcode() == ir::Instruction::FSub); ++it;
    assert((*it)->getOpcode() == ir::Instruction::FMul); ++it;
    assert((*it)->getOpcode() == ir::Instruction::FDiv); ++it;

    // Bitwise
    assert((*it)->getOpcode() == ir::Instruction::And); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Or); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Xor); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Shl); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Shr); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Sar); ++it;

    // Comparisons
    assert((*it)->getOpcode() == ir::Instruction::Ceq); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cne); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cslt); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Csle); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Csgt); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Csge); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cult); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cule); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cugt); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cuge); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Clt); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cle); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cgt); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cge); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Co); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Cuo); ++it;

    // Memory
    assert((*it)->getOpcode() == ir::Instruction::Alloc); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Store); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Load); ++it;
    assert((*it)->getOpcode() == ir::Instruction::Blit); ++it;

    // Control flow
    assert((*it)->getOpcode() == ir::Instruction::Jmp); ++it;

    ir::BasicBlock* end_bb = main_func->getBasicBlocks().back().get();
    assert(end_bb->getName() == "end");
    auto& end_instructions = end_bb->getInstructions();
    auto end_it = end_instructions.begin();

    assert((*end_it)->getOpcode() == ir::Instruction::Phi); ++end_it;
    assert((*end_it)->getOpcode() == ir::Instruction::Hlt); ++end_it;
    assert((*end_it)->getOpcode() == ir::Instruction::Ret); ++end_it;

    std::cout << "Comprehensive parser test passed!" << std::endl;

    return 0;
}
