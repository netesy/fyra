#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"

namespace ir {

PhiNode::PhiNode(Type* ty, unsigned numOperands, Instruction* alloc, BasicBlock* parent)
    : Instruction(ty, Instruction::Phi, {}, parent), variable(alloc) {
    // The operands will be added by the renamer pass.
    // We can reserve space for them.
    getOperands().reserve(numOperands);
}

Value* PhiNode::getIncomingValueForBlock(BasicBlock* bb) {
    if (!bb) return nullptr;
    for (size_t i = 0; i < getOperands().size(); i += 2) {
        if (!getOperands()[i]) continue;
        Value* predVal = getOperands()[i]->get();
        if (predVal == bb || (predVal && predVal->getName() == bb->getName())) {
            return (i + 1 < getOperands().size()) ? getOperands()[i + 1]->get() : nullptr;
        }
    }
    return nullptr;
}

void PhiNode::setIncomingValueForBlock(BasicBlock* bb, Value* value) {
    if (!bb) return;
    for (size_t i = 0; i < getOperands().size(); i += 2) {
        if (!getOperands()[i]) continue;
        Value* predVal = getOperands()[i]->get();
        if (predVal == bb || (predVal && predVal->getName() == bb->getName())) {
            if (i + 1 < getOperands().size()) {
                getOperands()[i + 1]->set(value);
            }
            return;
        }
    }
    addOperand(bb);
    addOperand(value);
}

void PhiNode::addIncoming(Value* value, BasicBlock* bb) {
    setIncomingValueForBlock(bb, value);
}

void PhiNode::removeIncomingValue(BasicBlock* bb) {
    if (!bb) return;
    auto& ops = getOperands();
    for (size_t i = 0; i < ops.size(); i += 2) {
        if (!ops[i]) continue;
        Value* predVal = ops[i]->get();
        if (predVal == bb || (predVal && predVal->getName() == bb->getName())) {
            ops.erase(ops.begin() + i, ops.begin() + std::min(i + 2, ops.size()));
            return;
        }
    }
}

} // namespace ir
