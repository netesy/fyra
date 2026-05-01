#include "ir/Constant.h"
#include "ir/IRContext.h"
#include <memory>
#include <stdexcept>

namespace ir {

ConstantInt::ConstantInt(IntegerType* ty, uint64_t val)
    : Constant(ty), value(val) {}

static IRContext& getGlobalCtx() {
    static std::unique_ptr<IRContext> globalCtx;
    if (!globalCtx) globalCtx = std::make_unique<IRContext>();
    return *globalCtx;
}

ConstantInt* ConstantInt::get(IntegerType* ty, uint64_t value) {
    return getGlobalCtx().getConstantInt(ty, value);
}

ConstantFP::ConstantFP(Type* ty, double val)
    : Constant(ty), value(val) {}

ConstantFP* ConstantFP::get(Type* ty, double value) {
    return getGlobalCtx().getConstantFP(ty, value);
}

ConstantArray::ConstantArray(ArrayType* ty, const std::vector<Constant*>& values) : Constant(ty), values(values) {}

ConstantArray* ConstantArray::get(ArrayType* ty, const std::vector<Constant*>& values) {
    return dynamic_cast<ConstantArray*>(getGlobalCtx().getConstantArray(ty, values));
}

ConstantString::ConstantString(const std::string& value) : Constant(nullptr), value(value) {}

void ConstantString::setType(Type* ty) {
    type = ty;
}

ConstantString* ConstantString::get(const std::string& value) {
    return getGlobalCtx().getConstantString(value);
}

} // namespace ir
