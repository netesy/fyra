#pragma once

#include "Value.h"
#include "Type.h"
#include <cstdint>
#include <vector>

namespace ir {

class Constant : public Value {
protected:
    Constant(Type* ty) : Value(ty) {}
};

class ConstantInt : public Constant {
public:
    static ConstantInt* get(IntegerType* ty, uint64_t value);
    uint64_t getValue() const { return value; }

private:
    ConstantInt(IntegerType* ty, uint64_t value);
    uint64_t value;
};

class ConstantFP : public Constant {
public:
    static ConstantFP* get(Type* ty, double value);
    double getValue() const { return value; }

private:
    ConstantFP(Type* ty, double value);
    double value;
};

class ConstantArray : public Constant {
public:
    static ConstantArray* get(ArrayType* ty, const std::vector<Constant*>& values);
    const std::vector<Constant*>& getValues() const { return values; }

private:
    ConstantArray(ArrayType* ty, const std::vector<Constant*>& values);
    std::vector<Constant*> values;
};

class ConstantString : public Constant {
public:
    static ConstantString* get(const std::string& value);
    const std::string& getValue() const { return value; }

private:
    ConstantString(const std::string& value);
    std::string value;
};

} // namespace ir
