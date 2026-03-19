#pragma once

#include <memory>
#include <vector>
#include <map>
#include <string>
#include <unordered_map>

namespace ir {

class Type;
class IntegerType;
class FloatType;
class DoubleType;
class VoidType;
class LabelType;
class PointerType;
class StructType;
class ArrayType;
class VectorType;
class FunctionType;
class Constant;
class ConstantInt;
class ConstantFP;
class ConstantString;
class Module;

class IRContext : public std::enable_shared_from_this<IRContext> {
public:
    IRContext();
    ~IRContext();

    // Type management
    IntegerType* getIntegerType(unsigned bitwidth);
    FloatType* getFloatType();
    DoubleType* getDoubleType();
    VoidType* getVoidType();
    LabelType* getLabelType();
    PointerType* getPointerType(Type* elementType, unsigned addressSpace = 0);
    StructType* createStructType(const std::string& name);
    ArrayType* getArrayType(Type* elementType, uint64_t numElements);
    VectorType* getVectorType(Type* elementType, unsigned numElements);
    FunctionType* getFunctionType(Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic = false);

    // Constant management
    ConstantInt* getConstantInt(IntegerType* ty, uint64_t value);
    ConstantFP* getConstantFP(Type* ty, double value);
    ConstantString* getConstantString(const std::string& value);

    // Module management
    std::unique_ptr<Module> createModule(const std::string& name);

private:
    // Storage for all owned objects
    std::vector<std::unique_ptr<Type>> ownedTypes;
    std::vector<std::unique_ptr<Constant>> ownedConstants;

    // Memoization maps
    std::unordered_map<unsigned, IntegerType*> intTypes;
    FloatType* floatTy = nullptr;
    DoubleType* doubleTy = nullptr;
    VoidType* voidTy = nullptr;
    LabelType* labelTy = nullptr;
    
    struct PointerKey {
        Type* element;
        unsigned addrSpace;
        bool operator==(const PointerKey& o) const { return element == o.element && addrSpace == o.addrSpace; }
    };
    struct PointerHash {
        size_t operator()(const PointerKey& k) const { return std::hash<void*>{}(k.element) ^ k.addrSpace; }
    };
    std::unordered_map<PointerKey, PointerType*, PointerHash> ptrTypes;
    
    std::unordered_map<std::string, FunctionType*> functionTypes;
    
    struct ArrayKey {
        Type* element;
        uint64_t size;
        bool operator==(const ArrayKey& o) const { return element == o.element && size == o.size; }
    };
    struct ArrayHash {
        size_t operator()(const ArrayKey& k) const { return std::hash<void*>{}(k.element) ^ k.size; }
    };
    std::unordered_map<ArrayKey, ArrayType*, ArrayHash> arrayTypes;
};

} // namespace ir
