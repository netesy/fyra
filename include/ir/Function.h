#pragma once

#include "Value.h"
#include "BasicBlock.h"
#include "Parameter.h"
#include <list>
#include <string>
#include <memory>
#include <map>

namespace ir {

class Module; // Forward declaration
class Instruction; // Forward declaration

class Function : public Value {
public:
    Function(Type* ty, const std::string& name, Module* parent = nullptr);
    Module* getParent() const { return parent; }

    std::list<std::unique_ptr<BasicBlock>>& getBasicBlocks() { return basicBlocks; }
    const std::list<std::unique_ptr<BasicBlock>>& getBasicBlocks() const { return basicBlocks; }

    void addBasicBlock(std::unique_ptr<BasicBlock> bb);

    // Stack frame management
    int getStackFrameSize() const { return stackFrameSize; }
    void setStackFrameSize(int size) { stackFrameSize = size; }
    int getStackSlotForVreg(const Instruction* vreg) const;
    bool hasStackSlot(const Instruction* vreg) const;
    void setStackSlotForVreg(const Instruction* vreg, int slot);

    void print(std::ostream& os) const override;

    bool isVariadic() const { return variadic; }
    void setVariadic(bool v) { variadic = v; }

    std::list<std::unique_ptr<Parameter>>& getParameters() { return parameters; }
    const std::list<std::unique_ptr<Parameter>>& getParameters() const { return parameters; }
    void addParameter(std::unique_ptr<Parameter> p);

    void setExported(bool e) { exported = e; }
    bool isExported() const { return exported; }
    void setReturnType(Type* ty) { type = ty; }

private:
    bool exported = false;
    bool variadic = false;
    std::list<std::unique_ptr<Parameter>> parameters;
    std::list<std::unique_ptr<BasicBlock>> basicBlocks;
    Module* parent;

    int stackFrameSize = 0;
    std::map<const Instruction*, int> stackSlots;
};

} // namespace ir
