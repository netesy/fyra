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
    return nullptr; // Legacy
}

IntegerType* IntegerType::get(IRContext& ctx, unsigned bitwidth) {
    return ctx.getIntegerType(bitwidth);
}

FloatType* FloatType::get() {
    return nullptr; // Legacy
}

FloatType* FloatType::get(IRContext& ctx) {
    return ctx.getFloatType();
}

DoubleType* DoubleType::get() {
    return nullptr; // Legacy
}

DoubleType* DoubleType::get(IRContext& ctx) {
    return ctx.getDoubleType();
}

VoidType* VoidType::get() {
    return nullptr; // Legacy
}

VoidType* VoidType::get(IRContext& ctx) {
    return ctx.getVoidType();
}

LabelType* LabelType::get() {
    return nullptr; // Legacy
}

LabelType* LabelType::get(IRContext& ctx) {
    return ctx.getLabelType();
}

StructType* StructType::create(const std::string& name) {
    return new StructType(name, {}, true);
}

void StructType::setBody(std::vector<Type*> elements, bool isOpaque) {
    this->elements = elements;
    this->opaque = isOpaque;
}

UnionType* UnionType::create(const std::string& name, std::vector<Type*> elements) {
    return new UnionType(name, elements);
}

PointerType* PointerType::get(Type* elementType, unsigned addressSpace) {
    return nullptr; // Legacy
}

PointerType* PointerType::get(IRContext& ctx, Type* elementType, unsigned addressSpace) {
    return ctx.getPointerType(elementType, addressSpace);
}

ArrayType* ArrayType::get(Type* elementType, uint64_t numElements) {
    return new ArrayType(elementType, numElements);
}

VectorType* VectorType::get(Type* elementType, unsigned numElements) {
    // Use a static map to memoize vector types
    static std::unordered_map<std::pair<Type*, unsigned>, VectorType*> vectorTypes;
    
    auto key = std::make_pair(elementType, numElements);
    auto it = vectorTypes.find(key);
    if (it != vectorTypes.end()) {
        return it->second;
    }
    
    VectorType* type = new VectorType(elementType, numElements);
    vectorTypes[key] = type;
    return type;
}

} // namespace ir
