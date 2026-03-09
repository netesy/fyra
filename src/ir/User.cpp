#include "ir/User.h"
#include "ir/Use.h"
#include <memory>

namespace ir {

User::User(Type* ty) : Value(ty) {}

User::~User() = default;

void User::addOperand(Value* v) {
    operands.push_back(std::make_unique<Use>(this, v));
}

} // namespace ir
