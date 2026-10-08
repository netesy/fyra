#pragma once
#include "ir/Instruction.h"
#include "ir/Use.h"

namespace transforms {
// Scalar SSA promotion is valid only when every use is a direct, full-width
// load/store. Passing the address to a call makes its contents observable.
inline bool isPromotableAlloca(ir::Instruction* allocation) {
    using I = ir::Instruction;
    if (allocation->getOpcode() != I::Alloc && allocation->getOpcode() != I::Alloc4 && allocation->getOpcode() != I::Alloc16)
        return false;
    auto* pointer = dynamic_cast<ir::PointerType*>(allocation->getType());
    if (!pointer) return false;
    auto* element = pointer->getElementType();
    for (auto* use : allocation->getUseList()) {
        auto* user = dynamic_cast<I*>(use->getUser());
        if (!user) return false;
        auto op = user->getOpcode();
        if ((op == I::Load || op == I::Loadl || op == I::Loadd || op == I::Loads) &&
            user->getOperands().size() == 1 && user->getOperands()[0]->get() == allocation &&
            user->getType() == element) continue;
        if ((op == I::Store || op == I::Storel || op == I::Stored || op == I::Stores) &&
            user->getOperands().size() == 2 && user->getOperands()[1]->get() == allocation &&
            user->getOperands()[0]->get() != allocation && user->getOperands()[0]->get()->getType() == element) continue;
        return false;
    }
    return true;
}
}
