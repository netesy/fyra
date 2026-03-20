#include "ir/Constant.h"

namespace ir {

ConstantInt::ConstantInt(IntegerType* ty, uint64_t val)
    : Constant(ty), value(val) {}

ConstantInt* ConstantInt::get(IntegerType* ty, uint64_t value) {
    // In a real compiler, these would be uniqued/memoized in a context object.
    // For now, we'll just allocate a new one each time.
    return new ConstantInt(ty, value);
}

ConstantFP::ConstantFP(Type* ty, double val)
    : Constant(ty), value(val) {}

ConstantFP* ConstantFP::get(Type* ty, double value) {
    return new ConstantFP(ty, value);
}

ConstantArray::ConstantArray(ArrayType* ty, const std::vector<Constant*>& values) : Constant(ty), values(values) {}

ConstantArray* ConstantArray::get(ArrayType* ty, const std::vector<Constant*>& values) {
    return new ConstantArray(ty, values);
}

ConstantString::ConstantString(const std::string& value) : Constant(nullptr), value(value) {}

void ConstantString::setType(Type* ty) {
    type = ty;
}

ConstantString* ConstantString::get(const std::string& value) {
    return new ConstantString(value);
}

} // namespace ir
