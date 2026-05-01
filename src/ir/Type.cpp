#include "ir/Type.h"
#include "ir/IRContext.h"
#include "target/core/TargetInfo.h"
#include "ir/FunctionType.h"
#include <unordered_map>
#include <functional>
#include <utility>

namespace std {
    template<>
    struct hash<pair<ir::Type*, unsigned>> {
        size_t operator()(const pair<ir::Type*, unsigned>& p) const {
            return hash<ir::Type*>()(p.first) ^ (hash<unsigned>()(p.second) << 1);
        }
    };
}

namespace ir {

// Target-aware methods implementation
size_t Type::getTargetSize(const target::TargetInfo* target) const {
    if (target) {
        auto info = target->getTypeInfo(this);
        return info.size / 8; // Convert bits to bytes
    }
    return getSize(); // Fall back to default
}

size_t Type::getTargetAlignment(const target::TargetInfo* target) const {
    if (target) {
        auto info = target->getTypeInfo(this);
        return info.align / 8; // Convert bits to bytes
    }
    return getAlignment(); // Fall back to default
}

// For simplicity, we'll leak these singletons. In a real compiler,
// you would have a context object that owns them.
static std::unique_ptr<IRContext> globalCtx;
static IRContext& getGlobalCtx() {
    if (!globalCtx) globalCtx = std::make_unique<IRContext>();
    return *globalCtx;
}

IntegerType* IntegerType::get(unsigned bitwidth) {
    return get(getGlobalCtx(), bitwidth);
}

IntegerType* IntegerType::get(IRContext& ctx, unsigned bitwidth) {
    return ctx.getIntegerType(bitwidth);
}

FloatType* FloatType::get() {
    return get(getGlobalCtx());
}

FloatType* FloatType::get(IRContext& ctx) {
    return ctx.getFloatType();
}

DoubleType* DoubleType::get() {
    return get(getGlobalCtx());
}

DoubleType* DoubleType::get(IRContext& ctx) {
    return ctx.getDoubleType();
}

VoidType* VoidType::get() {
    return get(getGlobalCtx());
}

VoidType* VoidType::get(IRContext& ctx) {
    return ctx.getVoidType();
}

LabelType* LabelType::get() {
    return get(getGlobalCtx());
}

LabelType* LabelType::get(IRContext& ctx) {
    return ctx.getLabelType();
}

StructType* StructType::create(const std::string& name) {
    throw std::runtime_error("StructType::create(name) is deprecated. Use IRContext::createStructType(name).");
}

void StructType::setBody(std::vector<Type*> elements, bool isOpaque) {
    this->elements = elements;
    this->opaque = isOpaque;
}

UnionType* UnionType::create(const std::string& name, std::vector<Type*> elements) {
    throw std::runtime_error("UnionType::create is deprecated.");
}

PointerType* PointerType::get(Type* elementType, unsigned addressSpace) {
    throw std::runtime_error("PointerType::get is deprecated. Use IRContext::getPointerType.");
}

PointerType* PointerType::get(IRContext& ctx, Type* elementType, unsigned addressSpace) {
    return ctx.getPointerType(elementType, addressSpace);
}

ArrayType* ArrayType::get(Type* elementType, uint64_t numElements) {
    throw std::runtime_error("ArrayType::get is deprecated. Use IRContext::getArrayType.");
}

VectorType* VectorType::get(Type* elementType, unsigned numElements) {
    throw std::runtime_error("VectorType::get is deprecated. Use IRContext::getVectorType.");
}

} // namespace ir
namespace ir {
Type* Type::getUnionTy(const std::vector<Type*>& members) {
    return IRContext::getContext().createUnionType("union");
}
}
