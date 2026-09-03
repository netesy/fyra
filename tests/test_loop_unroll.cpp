#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "transforms/LoopUnroll.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/DominanceFrontier.h"
#include "transforms/PhiInsertion.h"
#include "transforms/SSARenamer.h"
#include "transforms/Mem2Reg.h"
#include <iostream>
#include <cassert>

void test_loop_unroll_even_trip_count() {
    std::cout << "Testing Loop Unroll Pass on Even Trip Count Loop...\n";
    ir::Module mod("test_unroll_mod");
    ir::IRBuilder builder;
    builder.setModule(&mod);

    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    ir::IntegerType* i64Ty = ir::IntegerType::get(64);
    ir::Function* func = builder.createFunction("test_unroll_fn", i32Ty);

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* loop = builder.createBasicBlock("loop", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    // entry
    builder.setInsertPoint(entry);
    builder.createJmp(loop);

    // loop header
    builder.setInsertPoint(loop);
    ir::PhiNode* iPhi = builder.createPhi(i32Ty, 2, nullptr);
    ir::PhiNode* sumPhi = builder.createPhi(i64Ty, 2, nullptr);

    iPhi->addIncoming(ir::ConstantInt::get(i32Ty, 1), entry);
    sumPhi->addIncoming(ir::ConstantInt::get(i64Ty, 0), entry);

    ir::Instruction* cond = builder.createCsle(iPhi, ir::ConstantInt::get(i32Ty, 100));
    builder.createJnz(cond, body, exit);

    // loop body
    builder.setInsertPoint(body);
    ir::Instruction* iExt = builder.createExtSW(iPhi, i64Ty);
    ir::Instruction* sumNext = builder.createAdd(sumPhi, iExt);
    ir::Instruction* iNext = builder.createAdd(iPhi, ir::ConstantInt::get(i32Ty, 1));

    iPhi->addIncoming(iNext, body);
    sumPhi->addIncoming(sumNext, body);

    builder.createJmp(loop);

    // exit
    builder.setInsertPoint(exit);
    builder.createRet(iPhi);

    // Run unroll pass
    transforms::LoopUnrollPass unrollPass;
    bool changed = unrollPass.run(*func);

    assert(changed && "LoopUnrollPass should transform loop with 100 trip count!");

    // Verify step updated to 2
    bool foundStep2 = false;
    for (auto& instPtr : body->getInstructions()) {
        ir::Instruction* inst = instPtr.get();
        if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() >= 2) {
            if (auto* c = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[1]->get())) {
                if (c->getValue() == 2) {
                    foundStep2 = true;
                }
            }
        }
    }

    assert(foundStep2 && "Unrolled loop induction step should be updated to 2!");
    std::cout << "Loop Unroll Even Trip Count Test Passed!\n";
}

int main() {
    test_loop_unroll_even_trip_count();
    std::cout << "All Loop Unroll Unit Tests Passed Successfully!\n";
    return 0;
}
