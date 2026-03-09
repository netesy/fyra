#include "ir/Type.h"
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
    // Use a static map to memoize types
    static std::unordered_map<unsigned, IntegerType*> intTypes;
    
    auto it = intTypes.find(bitwidth);
    if (it != intTypes.end()) {
        return it->second;
    }
    
    IntegerType* type = new IntegerType(bitwidth);
    intTypes[bitwidth] = type;
    return type;
}

FloatType* FloatType::get() {
    static FloatType S;
    return &S;
}

DoubleType* DoubleType::get() {
    static DoubleType D;
    return &D;
}

VoidType* VoidType::get() {
    static VoidType V;
    return &V;
}

LabelType* LabelType::get() {
    static LabelType L;
    return &L;
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
    // Use a static map to memoize pointer types
    static std::unordered_map<std::pair<Type*, unsigned>, PointerType*> ptrTypes;
    
    auto key = std::make_pair(elementType, addressSpace);
    auto it = ptrTypes.find(key);
    if (it != ptrTypes.end()) {
        return it->second;
    }
    
    PointerType* type = new PointerType(elementType, addressSpace);
    ptrTypes[key] = type;
    return type;
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
