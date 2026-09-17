#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
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
    if (!input.good()) {
        test_file = "../tests/ssa.fyra";
        input.open(test_file);
    }
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("main");
    if (!func) func = module->getFunction("$main");
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

    // Mem2Reg may remove stores only to local allocas.  Stores through pointer
    // parameters are externally observable even when they have no SSA users.
    auto storeCtx = std::make_shared<ir::IRContext>();
    ir::Module storeModule("external_stores", storeCtx);
    ir::IRBuilder storeBuilder(storeCtx); storeBuilder.setModule(&storeModule);
    std::vector<ir::Type*> elementTypes = {storeCtx->getIntegerType(32), storeCtx->getFloatType(), storeCtx->getDoubleType()};
    std::vector<ir::Instruction::Opcode> storeOps = {ir::Instruction::Store, ir::Instruction::Stores, ir::Instruction::Stored};
    for (size_t index = 0; index < elementTypes.size(); ++index) {
        auto* pointer = storeCtx->getPointerType(elementTypes[index]);
        auto* storeFunc = storeBuilder.createFunction("external_store_" + std::to_string(index), storeCtx->getVoidType(), {pointer, elementTypes[index]});
        auto* block = storeBuilder.createBasicBlock("entry", storeFunc); storeBuilder.setInsertPoint(block);
        auto parameter = storeFunc->getParameters().begin();
        ir::Value* address = parameter->get(); ++parameter;
        ir::Value* value = parameter->get();
        if (index == 0) storeBuilder.createStore(value, address);
        else if (index == 1) storeBuilder.createStores(value, address);
        else storeBuilder.createStored(value, address);
        storeBuilder.createRet(nullptr);
        transforms::Mem2Reg externalMem2Reg;
        externalMem2Reg.run(*storeFunc);
        bool foundStore = false;
        for (const auto& instruction : block->getInstructions())
            foundStore |= instruction->getOpcode() == storeOps[index];
        assert(foundStore && "externally observable pointer-parameter store was removed");
    }

    return 0;
}
