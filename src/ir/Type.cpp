#include "ir/Type.h"
#include "ir/IRContext.h"
#include "codegen/target/TargetInfo.h"
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
size_t Type::getTargetSize(const codegen::target::TargetInfo* target) const {
    if (target) {
        auto info = target->getTypeInfo(this);
        return info.size / 8; // Convert bits to bytes
    }
    return getSize(); // Fall back to default
}

size_t Type::getTargetAlignment(const codegen::target::TargetInfo* target) const {
    if (target) {
        auto info = target->getTypeInfo(this);
        return info.align / 8; // Convert bits to bytes
    }
    return getAlignment(); // Fall back to default
}

// For simplicity, we'll leak these singletons. In a real compiler,
// you would have a context object that owns them.
IntegerType* IntegerType::get(unsigned bitwidth) {
    throw std::runtime_error("IntegerType::get(bitwidth) is deprecated.");
}

IntegerType* IntegerType::get(IRContext& ctx, unsigned bitwidth) {
    return ctx.getIntegerType(bitwidth);
}

FloatType* FloatType::get() {
    throw std::runtime_error("FloatType::get() is deprecated.");
}

FloatType* FloatType::get(IRContext& ctx) {
    return ctx.getFloatType();
}

DoubleType* DoubleType::get() {
    throw std::runtime_error("DoubleType::get() is deprecated.");
}

DoubleType* DoubleType::get(IRContext& ctx) {
    return ctx.getDoubleType();
}

VoidType* VoidType::get() {
    throw std::runtime_error("VoidType::get() is deprecated.");
}

VoidType* VoidType::get(IRContext& ctx) {
    return ctx.getVoidType();
}

LabelType* LabelType::get() {
    throw std::runtime_error("LabelType::get() is deprecated.");
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
