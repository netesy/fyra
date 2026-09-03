#pragma once

#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Value.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include <unordered_map>
#include <memory>
#include <vector>

namespace transforms {

using ValueMapper = std::unordered_map<ir::Value*, ir::Value*>;

class InstructionCloner {
public:
    static std::unique_ptr<ir::Instruction> cloneInstruction(
        const ir::Instruction* srcInstr,
        const ValueMapper& vmap,
        ir::BasicBlock* targetBB = nullptr
    );
};

} // namespace transforms
