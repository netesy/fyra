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
    int unnamed_counter = 0;
    // Pre-pass to name all unnamed instructions
    
    for (const auto& param : parameters) {
        if (param && param->getName().empty()) {
            param->setName(std::to_string(unnamed_counter++));
        }
    }
    
    for (const auto& bb : basicBlocks) {
        if (bb) {
            for (const auto& inst : bb->getInstructions()) {
                if (inst && inst->getName().empty()) {
                    inst->setName(std::to_string(unnamed_counter++));
                }
            }
        }
    }
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

int Function::getStackSlotForVreg(const Value* vreg) const {
    if (stackSlots.count(vreg)) {
        return stackSlots.at(vreg);
    }
    return -1;
}

void Function::setStackSlotForVreg(const Value* vreg, int slot) {
    stackSlots[vreg] = slot;
}

bool Function::hasStackSlot(const Value* vreg) const {
    return stackSlots.find(vreg) != stackSlots.end();
}

} // namespace ir
