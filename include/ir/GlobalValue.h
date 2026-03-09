#pragma once

#include "Value.h"

namespace ir {

class GlobalValue : public Value {
public:
    GlobalValue(Type* ty, const std::string& name) : Value(ty) {
        setName(name);
    }
};

} // namespace ir
