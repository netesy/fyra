#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/BasicBlock.h"
#include <iostream>

namespace ir {

Function::Function(Type* ty, const std::string& name, Module* parent)
    : Value(ty), name(name), parent(parent) {
}

void Function::addBasicBlock(std::unique_ptr<BasicBlock> bb) {
    basicBlocks.push_back(std::move(bb));
}

void Function::addParameter(std::unique_ptr<Parameter> p) {
    parameters.push_back(std::move(p));
}

void Function::print(std::ostream& os) const {
    os << "function " << name << "() {\n";
    for (const auto& bb : basicBlocks) {
        bb->print(os);
    }
    os << "}\n";
}

int Function::getStackSlotForVreg(const Instruction* vreg) const {
    if (stackSlots.count(vreg)) {
        return stackSlots.at(vreg);
    }
    return -1; // Not found
}

void Function::setStackSlotForVreg(const Instruction* vreg, int slot) {
    stackSlots[vreg] = slot;
}

} // namespace ir
