#pragma once

#include "ir/Type.h"
#include <vector>

namespace ir {

class FunctionType : public Type {
public:
    FunctionType(Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic)
        : Type(Type::FunctionTyID), returnType(returnType), paramTypes(paramTypes), isVariadic_(isVariadic) {}

    static FunctionType* get(Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic = false);

    Type* getReturnType() const { return returnType; }
    const std::vector<Type*>& getParamTypes() const { return paramTypes; }
    bool isVariadic() const { return isVariadic_; }

    size_t getSize() const override { return 0; }
    size_t getAlignment() const override { return 0; }
    std::string toString() const override;

private:
    Type* returnType;
    std::vector<Type*> paramTypes;
    bool isVariadic_;
};

} // namespace ir
