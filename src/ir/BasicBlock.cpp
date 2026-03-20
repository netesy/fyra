#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/IRContext.h"
#include "ir/Type.h"
#include <algorithm>
#include <iostream>

namespace ir {

BasicBlock::BasicBlock(Function* parent, const std::string& name)
    : Value(parent->getParent()->getContext()->getLabelType()), parent(parent) {
    setName(name);
}

void BasicBlock::addInstruction(instr_iterator it, std::unique_ptr<Instruction> instr) {
    instructions.insert(it, std::move(instr));
}

void BasicBlock::addInstruction(std::unique_ptr<Instruction> instr) {
    instructions.push_back(std::move(instr));
}

void BasicBlock::addPredecessor(BasicBlock* pred) {
    if (std::find(predecessors.begin(), predecessors.end(), pred) == predecessors.end()) {
        predecessors.push_back(pred);
    }
}

void BasicBlock::addSuccessor(BasicBlock* succ) {
    if (std::find(successors.begin(), successors.end(), succ) == successors.end()) {
        successors.push_back(succ);
    }
}

void BasicBlock::removeInstructions(const std::vector<Instruction*>& to_remove) {
    instructions.remove_if([&](const std::unique_ptr<Instruction>& instr) {
        return std::find(to_remove.begin(), to_remove.end(), instr.get()) != to_remove.end();
    });
}

void BasicBlock::print(std::ostream& os) const {
    os << "@" << getName() << "\n";
    for (const auto& instr : instructions) {
        if (instr) {
            os << "  ";
            instr->print(os);
            os << "\n";
        }
    }
}

} // namespace ir
