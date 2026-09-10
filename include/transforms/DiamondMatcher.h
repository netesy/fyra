#pragma once

#include <optional>
#include <vector>

namespace ir {
class BasicBlock;
class Function;
class Instruction;
class PhiNode;
class Value;
}

namespace transforms {

struct SingleDiamond {
    ir::BasicBlock* conditionBlock = nullptr;
    ir::BasicBlock* trueBlock = nullptr;
    ir::BasicBlock* falseBlock = nullptr;
    ir::BasicBlock* mergeBlock = nullptr;
    ir::Instruction* branch = nullptr;
    ir::Value* condition = nullptr;
    std::vector<ir::PhiNode*> mergePhis;
};

// Matches a strict, speculation-safe, non-nested diamond. The function's CFG
// metadata must already be current (normally via CFGBuilder::run). Rejection is
// conservative: every arm instruction and every escaping arm value is proven.
std::optional<SingleDiamond> matchSpeculatableSingleDiamond(
    ir::Function& function, ir::BasicBlock* conditionBlock);

} // namespace transforms
