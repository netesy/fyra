#pragma once

#include "Value.h"
#include <vector>
#include <memory>

namespace ir {

class Use; // Forward declaration

class User : public Value {
public:
    ~User();
    const std::vector<std::unique_ptr<Use>>& getOperands() const { return operands; }
    std::vector<std::unique_ptr<Use>>& getOperands() { return operands; }

protected:
    User(Type* ty);
    void addOperand(Value* v);

private:
    std::vector<std::unique_ptr<Use>> operands;
};

} // namespace ir
