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
    // The operands are stored as [block1, value1, block2, value2, ...]
    for (size_t i = 0; i < getOperands().size(); i += 2) {
        if (getOperands()[i]->get() == bb) {
            return getOperands()[i + 1]->get();
        }
    }
    return nullptr;
}

void PhiNode::setIncomingValueForBlock(BasicBlock* bb, Value* value) {
    // Find the operand for the block and set its value
    for (size_t i = 0; i < getOperands().size(); i += 2) {
        if (getOperands()[i]->get() == bb) {
            getOperands()[i + 1]->set(value);
            return;
        }
    }
    // If not found, add a new entry.
    addOperand(bb);
    addOperand(value);
}

void PhiNode::addIncoming(Value* value, BasicBlock* bb) {
    setIncomingValueForBlock(bb, value);
}

} // namespace ir
