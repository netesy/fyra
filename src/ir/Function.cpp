#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/BasicBlock.h"
#include <iostream>

namespace ir {

Function::Function(Type* ty, const std::string& name, Module* parent)
    : Value(ty), parent(parent) {
    setName(name);
}

void Function::addBasicBlock(std::unique_ptr<BasicBlock> bb) {
    basicBlocks.push_back(std::move(bb));
}

void Function::addParameter(std::unique_ptr<Parameter> p) {
    parameters.push_back(std::move(p));
}

void Function::print(std::ostream& os) const {
    os << "function " << getName() << "(";
    bool first = true;
    for (const auto& param : parameters) {
        if (!first) os << ", ";
        if (param) os << param->getType()->toString();
        first = false;
    }
    os << ") {\n";
    for (const auto& bb : basicBlocks) {
        if (bb) bb->print(os);
    }
    os << "}\n";
}

int Function::getStackSlotForVreg(const Instruction* vreg) const {
    if (stackSlots.count(vreg)) {
        return stackSlots.at(vreg);
    }
    return -1;
}

void Function::setStackSlotForVreg(const Instruction* vreg, int slot) {
    stackSlots[vreg] = slot;
}

bool Function::hasStackSlot(const Instruction* vreg) const {
    return stackSlots.find(vreg) != stackSlots.end();
}

} // namespace ir
