#pragma once

#include "Value.h"

namespace ir {

class Parameter : public Value {
public:
    Parameter(Type* ty, const std::string& name) : Value(ty) {
        setName(name);
    }
};

} // namespace ir
