#include "transforms/IdempotentLoopCollapse.h"

#include "transforms/AffineAnalysis.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Module.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace transforms {
namespace {

bool isLoad(ir::Instruction::Opcode opcode) {
    switch (opcode) {
        case ir::Instruction::Load: case ir::Instruction::Loadd:
        case ir::Instruction::Loads: case ir::Instruction::Loadl:
        case ir::Instruction::Loaduw: case ir::Instruction::Loadsh:
        case ir::Instruction::Loaduh: case ir::Instruction::Loadsb:
        case ir::Instruction::Loadub:
            return true;
        default:
            return false;
    }
}

bool isStore(ir::Instruction::Opcode opcode) {
    switch (opcode) {
        case ir::Instruction::Store: case ir::Instruction::Stored:
        case ir::Instruction::Stores: case ir::Instruction::Storel:
        case ir::Instruction::Storeh: case ir::Instruction::Storeb:
            return true;
        default:
            return false;
    }
}

void collectAllocationRoots(ir::Value* value, const Loop& loop,
                            std::set<ir::Instruction*>& roots,
                            std::set<ir::Value*>& visited, bool& unknownOutsideLeaf) {
    if (!value || !visited.insert(value).second || dynamic_cast<ir::Constant*>(value) ||
        dynamic_cast<ir::BasicBlock*>(value)) return;
    auto* instruction = dynamic_cast<ir::Instruction*>(value);
    if (!instruction) {
        unknownOutsideLeaf = true; // Parameter/global pointers may alias or escape.
        return;
    }
    const auto opcode = instruction->getOpcode();
    if (opcode == ir::Instruction::Alloc || opcode == ir::Instruction::Alloc4 ||
        opcode == ir::Instruction::Alloc16) {
        roots.insert(instruction);
        return;
    }
    const bool addressComposition = opcode == ir::Instruction::Copy ||
        opcode == ir::Instruction::Add || opcode == ir::Instruction::Sub ||
        opcode == ir::Instruction::Mul || opcode == ir::Instruction::Shl ||
        opcode == ir::Instruction::ExtSW || opcode == ir::Instruction::ExtUW;
    if (!addressComposition) {
        if (!loop.blocks.count(instruction->getParent())) unknownOutsideLeaf = true;
        return; // Loop-local indices are not pointer provenance roots.
    }
    for (const auto& operand : instruction->getOperands())
        collectAllocationRoots(operand->get(), loop, roots, visited, unknownOutsideLeaf);
}

ir::Instruction* localAllocationRoot(ir::Value* value, const Loop& loop) {
    std::set<ir::Instruction*> roots;
    std::set<ir::Value*> visited;
    bool unknownOutsideLeaf = false;
    collectAllocationRoots(value, loop, roots, visited, unknownOutsideLeaf);
    return !unknownOutsideLeaf && roots.size() == 1 ? *roots.begin() : nullptr;
}

bool hasOnlyControlUses(ir::PhiNode* induction, ir::Instruction* condition,
                        ir::Instruction* step) {
    for (ir::Use* use : induction->getUseList()) {
        auto* user = dynamic_cast<ir::Instruction*>(use->getUser());
        if (user != condition && user != step) return false;
    }
    return true;
}

struct CountedLoop {
    ir::PhiNode* induction = nullptr;
    ir::Instruction* condition = nullptr;
    ir::Instruction* step = nullptr;
    ir::ConstantInt* initial = nullptr;
    ir::ConstantInt* bound = nullptr;
};

bool findCountedLoop(const Loop& loop, CountedLoop& result) {
    std::vector<ir::PhiNode*> phis;
    for (auto& instruction : loop.header->getInstructions()) {
        auto* phi = dynamic_cast<ir::PhiNode*>(instruction.get());
        if (!phi) break;
        phis.push_back(phi);
    }
    if (phis.size() != 1) return false;
    result.induction = phis.front();

    ir::Value* initialValue = nullptr;
    ir::Value* latchValue = nullptr;
    for (size_t index = 0; index + 1 < result.induction->getOperands().size(); index += 2) {
        auto* block = dynamic_cast<ir::BasicBlock*>(result.induction->getOperands()[index]->get());
        ir::Value* value = result.induction->getOperands()[index + 1]->get();
        if (!block) {
            block = dynamic_cast<ir::BasicBlock*>(result.induction->getOperands()[index + 1]->get());
            value = result.induction->getOperands()[index]->get();
        }
        if (!block || !value) continue;
        if (loop.blocks.count(block)) latchValue = value;
        else initialValue = value;
    }
    result.initial = dynamic_cast<ir::ConstantInt*>(initialValue);
    result.step = dynamic_cast<ir::Instruction*>(latchValue);
    if (!result.initial || !result.step || result.step->getOpcode() != ir::Instruction::Add ||
        result.step->getOperands().size() != 2) return false;
    ir::Value* other = nullptr;
    if (result.step->getOperands()[0]->get() == result.induction)
        other = result.step->getOperands()[1]->get();
    else if (result.step->getOperands()[1]->get() == result.induction)
        other = result.step->getOperands()[0]->get();
    auto* increment = dynamic_cast<ir::ConstantInt*>(other);
    if (!increment || increment->getValue() != 1) return false;

    for (auto& instruction : loop.header->getInstructions()) {
        auto opcode = instruction->getOpcode();
        if (opcode != ir::Instruction::Cslt && opcode != ir::Instruction::Cult) continue;
        if (instruction->getOperands().size() != 2 ||
            instruction->getOperands()[0]->get() != result.induction) continue;
        result.bound = dynamic_cast<ir::ConstantInt*>(instruction->getOperands()[1]->get());
        if (result.bound) {
            result.condition = instruction.get();
            break;
        }
    }
    if (!result.condition || !result.bound) return false;
    const uint64_t initial = result.initial->getValue();
    const uint64_t bound = result.bound->getValue();
    return initial != std::numeric_limits<uint64_t>::max() && bound > initial + 1 &&
           hasOnlyControlUses(result.induction, result.condition, result.step);
}

bool hasIdempotentLocalMemory(const Loop& loop, ir::PhiNode* repetitionInduction) {
    AffineAnalysis affine;
    std::set<ir::Instruction*> loadRoots;
    std::set<ir::Instruction*> storeRoots;

    for (ir::BasicBlock* block : loop.blocks) {
        for (auto& owned : block->getInstructions()) {
            ir::Instruction* instruction = owned.get();
            const auto opcode = instruction->getOpcode();
            if (opcode == ir::Instruction::Call || opcode == ir::Instruction::ExternCall ||
                opcode == ir::Instruction::Syscall || opcode == ir::Instruction::Ret ||
                opcode == ir::Instruction::Div || opcode == ir::Instruction::Udiv ||
                opcode == ir::Instruction::Rem || opcode == ir::Instruction::Urem ||
                opcode == ir::Instruction::Alloc || opcode == ir::Instruction::Alloc4 ||
                opcode == ir::Instruction::Alloc16)
                return false;
            if (!isLoad(opcode) && !isStore(opcode)) continue;
            const size_t pointerIndex = isStore(opcode) ? 1 : 0;
            if (instruction->getOperands().size() <= pointerIndex) return false;
            ir::Value* pointer = instruction->getOperands()[pointerIndex]->get();
            // Shared AffineAnalysis remains the authoritative affine
            // decomposition.  Provenance is intentionally separate because a
            // wrapping narrow index may be non-affine while its local
            // allocation root is still unambiguous.
            const auto expression = affine.analyze(pointer);
            if (expression.isValid) {
                const auto coefficient = expression.getCoefficient(repetitionInduction);
                if (coefficient && *coefficient != 0) return false;
            }
            auto* root = localAllocationRoot(pointer, loop);
            if (!root) return false; // Parameters/globals may alias or escape.
            if (isStore(opcode)) storeRoots.insert(root);
            else loadRoots.insert(root);
        }
    }
    if (storeRoots.empty()) return false;
    for (auto* root : storeRoots)
        if (loadRoots.count(root)) return false;
    return true;
}

} // namespace

bool IdempotentLoopCollapse::performTransformation(ir::Function& function) {
    LoopInvariantCodeMotion loopDiscovery;
    std::vector<std::unique_ptr<Loop>> loops;
    loopDiscovery.findLoops(function, loops);
    std::sort(loops.begin(), loops.end(), [](const auto& left, const auto& right) {
        return left->blocks.size() > right->blocks.size();
    });

    for (const auto& loop : loops) {
        CountedLoop counted;
        const bool countedLoop = findCountedLoop(*loop, counted);
        const bool idempotent = countedLoop &&
                                hasIdempotentLocalMemory(*loop, counted.induction);
        if (std::getenv("FYRA_IDEMPOTENT_LOOP_DIAG"))
            std::cerr << "[IdempotentLoopCollapse] header=" << loop->header->getName()
                      << " blocks=" << loop->blocks.size()
                      << " counted=" << countedLoop << " idempotent=" << idempotent << "\n";
        if (!countedLoop || !idempotent) continue;
        auto* integerType = dynamic_cast<ir::IntegerType*>(counted.bound->getType());
        if (!integerType || !function.getParent()) continue;
        auto* oneIterationBound = function.getParent()->getContext()->getConstantInt(
            integerType, counted.initial->getValue() + 1);
        counted.condition->getOperands()[1]->set(oneIterationBound);
        CFGBuilder::run(function);
        return true;
    }
    return false;
}

} // namespace transforms
