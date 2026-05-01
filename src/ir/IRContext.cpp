#include "ir/IRContext.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "ir/Module.h"
#include "ir/FunctionType.h"
#include <sstream>

namespace ir {

IRContext::IRContext() {}
IRContext::~IRContext() = default;

IntegerType* IRContext::getIntegerType(unsigned bitwidth) {
    if (intTypes.count(bitwidth)) return intTypes[bitwidth];
    auto ty = std::make_unique<IntegerType>(bitwidth);
    IntegerType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    intTypes[bitwidth] = ptr;
    return ptr;
}

FloatType* IRContext::getFloatType() {
    if (floatTy) return floatTy;
    auto ty = std::make_unique<FloatType>();
    floatTy = ty.get();
    ownedTypes.push_back(std::move(ty));
    return floatTy;
}

DoubleType* IRContext::getDoubleType() {
    if (doubleTy) return doubleTy;
    auto ty = std::make_unique<DoubleType>();
    doubleTy = ty.get();
    ownedTypes.push_back(std::move(ty));
    return doubleTy;
}

VoidType* IRContext::getVoidType() {
    if (voidTy) return voidTy;
    auto ty = std::make_unique<VoidType>();
    voidTy = ty.get();
    ownedTypes.push_back(std::move(ty));
    return voidTy;
}

LabelType* IRContext::getLabelType() {
    if (labelTy) return labelTy;
    auto ty = std::make_unique<LabelType>();
    labelTy = ty.get();
    ownedTypes.push_back(std::move(ty));
    return labelTy;
}

PointerType* IRContext::getPointerType(Type* elementType, unsigned addressSpace) {
    PointerKey key{elementType, addressSpace};
    if (ptrTypes.count(key)) return ptrTypes[key];
    auto ty = std::make_unique<PointerType>(elementType, addressSpace);
    PointerType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    ptrTypes[key] = ptr;
    return ptr;
}

StructType* IRContext::createStructType(const std::string& name) {
    auto ty = std::make_unique<StructType>(name, std::vector<Type*>{}, true);
    StructType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    return ptr;
}

UnionType* IRContext::createUnionType(const std::string& name) {
    auto ty = std::make_unique<UnionType>(name, std::vector<Type*>{});
    UnionType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    return ptr;
}

ArrayType* IRContext::getArrayType(Type* elementType, uint64_t numElements) {
    ArrayKey key{elementType, numElements};
    if (arrayTypes.count(key)) return arrayTypes[key];
    auto ty = std::make_unique<ArrayType>(elementType, numElements);
    ArrayType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    arrayTypes[key] = ptr;
    return ptr;
}

VectorType* IRContext::getVectorType(Type* elementType, unsigned numElements) {
    VectorKey key{elementType, numElements};
    if (vectorTypes.count(key)) return vectorTypes[key];
    auto ty = std::make_unique<VectorType>(elementType, numElements);
    VectorType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    vectorTypes[key] = ptr;
    return ptr;
}

FunctionType* IRContext::getFunctionType(Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic) {
    std::stringstream ss;
    ss << returnType->toString() << "(";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        ss << paramTypes[i]->toString() << (i == paramTypes.size() - 1 ? "" : ",");
    }
    if (isVariadic) ss << ",...";
    ss << ")";
    std::string key = ss.str();
    
    if (functionTypes.count(key)) return functionTypes[key];
    auto ty = std::make_unique<FunctionType>(returnType, paramTypes, isVariadic);
    FunctionType* ptr = ty.get();
    ownedTypes.push_back(std::move(ty));
    functionTypes[key] = ptr;
    return ptr;
}

ConstantInt* IRContext::getConstantInt(IntegerType* ty, uint64_t value) {
    ConstantIntKey key{ty, value};
    if (constantInts.count(key)) return constantInts[key];
    auto c = std::unique_ptr<ConstantInt>(new ConstantInt(ty, value));
    ConstantInt* ptr = c.get();
    ownedConstants.push_back(std::move(c));
    constantInts[key] = ptr;
    return ptr;
}

ConstantFP* IRContext::getConstantFP(Type* ty, double value) {
    ConstantFPKey key{ty, value};
    if (constantFPs.count(key)) return constantFPs[key];
    auto c = std::unique_ptr<ConstantFP>(new ConstantFP(ty, value));
    ConstantFP* ptr = c.get();
    ownedConstants.push_back(std::move(c));
    constantFPs[key] = ptr;
    return ptr;
}

ConstantString* IRContext::getConstantString(const std::string& value) {
    if (constantStrings.count(value)) return constantStrings[value];
    auto c = std::unique_ptr<ConstantString>(new ConstantString(value));
    ConstantString* ptr = c.get();
    ptr->setType(getArrayType(getIntegerType(8), value.length() + 1));
    ownedConstants.push_back(std::move(c));
    constantStrings[value] = ptr;
    return ptr;
}

Constant* IRContext::getConstantArray(ArrayType* ty, const std::vector<Constant*>& elements) {
    ConstantArrayKey key{ty, elements};
    if (constantArrays.count(key)) return constantArrays[key];
    auto c = std::unique_ptr<ConstantArray>(new ConstantArray(ty, elements));
    Constant* ptr = c.get();
    ownedConstants.push_back(std::move(c));
    constantArrays[key] = ptr;
    return ptr;
}

std::unique_ptr<Module> IRContext::createModule(const std::string& name) {
    return std::make_unique<Module>(name, shared_from_this());
}

} // namespace ir
namespace ir {
IRContext& IRContext::getContext() { static IRContext instance; return instance; }
}
