#pragma once
#include "Value.h"
#include <string>
#include <vector>
namespace ir {
class Type; class IntegerType; class ArrayType;
class Constant : public Value { public: Constant(Type* ty) : Value(ty) {} };
class ConstantInt : public Constant { public: static ConstantInt* get(IntegerType* ty, uint64_t value); uint64_t getValue() const { return value; } ConstantInt(IntegerType* ty, uint64_t value); public: uint64_t value; };
class ConstantFP : public Constant { public: static ConstantFP* get(Type* ty, double value); double getValue() const { return value; } ConstantFP(Type* ty, double value); public: double value; };
class ConstantArray : public Constant { public: static ConstantArray* get(ArrayType* ty, const std::vector<Constant*>& values); const std::vector<Constant*>& getValues() const { return values; } public: ConstantArray(ArrayType* ty, const std::vector<Constant*>& values); public: std::vector<Constant*> values; };
class ConstantString : public Constant { public: static ConstantString* get(const std::string& value); const std::string& getValue() const { return value; } ConstantString(const std::string& value); void setType(Type* ty); public: std::string value; };
}
