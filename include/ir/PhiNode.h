#pragma once

#include "Instruction.h"

namespace ir {

class PhiNode : public Instruction {
public:
    PhiNode(Type* ty, unsigned numOperands, Instruction* alloc, BasicBlock* parent = nullptr);

    Instruction* getVariable() const { return variable; }

    // Helper to get the incoming value for a given predecessor block
    Value* getIncomingValueForBlock(BasicBlock* bb);
    void setIncomingValueForBlock(BasicBlock* bb, Value* value);
    void addIncoming(Value* value, BasicBlock* bb);

private:
    Instruction* variable; // The alloc instruction this phi is for
};

} // namespace ir
