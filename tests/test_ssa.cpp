#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/DominanceFrontier.h"
#include "transforms/PhiInsertion.h"
#include "transforms/SSARenamer.h"
#include "transforms/Mem2Reg.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/ErrorReporter.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <iostream>

int main() {
    std::string test_file = "tests/ssa.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("main");
    assert(func != nullptr);

    // Run SSA passes
    transforms::CFGBuilder::run(*func);
    transforms::DominatorTree domTree;
    domTree.run(*func);
    transforms::DominanceFrontier domFrontier;
    domFrontier.run(*func, domTree);
    transforms::PhiInsertion phiInserter;
    phiInserter.run(*func, domFrontier);
    transforms::SSARenamer ssaRenamer;
    ssaRenamer.run(*func, domTree);
    transforms::Mem2Reg mem2reg;
    mem2reg.run(*func);

    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    transforms::DeadInstructionElimination enhanced_dce(error_reporter);
    enhanced_dce.run(*func);


    bool has_phi = false;
    for (auto& bb : func->getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            assert(instr->getOpcode() != ir::Instruction::Alloc);
            assert(instr->getOpcode() != ir::Instruction::Load);
            assert(instr->getOpcode() != ir::Instruction::Store);
            if (instr->getOpcode() == ir::Instruction::Phi) {
                has_phi = true;
            }
        }
    }

    // This is not a very good test, as there are no branches,
    // so no phi nodes will be inserted.
    // A better test would have branches.
    // For now, we just check that alloc, load, store are gone.
    // A more complex test is needed for phi nodes.

    std::cout << "SSA test passed!" << std::endl;

    return 0;
}
