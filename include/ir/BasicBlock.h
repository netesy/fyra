#pragma once

#include "Value.h"
#include "Instruction.h"
#include <list>
#include <memory>
#include <vector>

namespace ir {

class Function; // Forward declaration
class BasicBlock; // Forward declaration for pointers

class BasicBlock : public Value {
public:
    BasicBlock(Function* parent = nullptr, const std::string& name = "");

    Function* getParent() const { return parent; }
    std::list<std::unique_ptr<Instruction>>& getInstructions() { return instructions; }
    const std::list<std::unique_ptr<Instruction>>& getInstructions() const { return instructions; }

    using instr_iterator = std::list<std::unique_ptr<Instruction>>::iterator;
    void addInstruction(instr_iterator it, std::unique_ptr<Instruction> instr);
    void addInstruction(std::unique_ptr<Instruction> instr);

    const std::vector<BasicBlock*>& getPredecessors() const { return predecessors; }
    const std::vector<BasicBlock*>& getSuccessors() const { return successors; }
    void addPredecessor(BasicBlock* pred);
    void addSuccessor(BasicBlock* succ);

    void removeInstructions(const std::vector<Instruction*>& to_remove);

    void print(std::ostream& os) const override;

private:
    std::list<std::unique_ptr<Instruction>> instructions;
    Function* parent;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;
};

} // namespace ir
