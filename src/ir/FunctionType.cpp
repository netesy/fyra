#include "ir/FunctionType.h"
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace ir {

FunctionType* FunctionType::get(Type* returnType, const std::vector<Type*>& paramTypes, bool isVariadic) {
    // A simple cache for function types
    static std::map<std::string, std::unique_ptr<FunctionType>> functionTypeCache;
    std::string key;
    key += returnType->toString();
    key += "(";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        key += paramTypes[i]->toString();
        if (i < paramTypes.size() - 1) {
            key += ",";
        }
    }
    if (isVariadic) {
        if (!paramTypes.empty()) {
            key += ",";
        }
        key += "...";
    }
    key += ")";

    auto it = functionTypeCache.find(key);
    if (it != functionTypeCache.end()) {
        return it->second.get();
    }

    auto newType = new FunctionType(returnType, paramTypes, isVariadic);
    functionTypeCache[key] = std::unique_ptr<FunctionType>(newType);
    return newType;
}

std::string FunctionType::toString() const {
    std::string str = returnType->toString() + " (";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        str += paramTypes[i]->toString();
        if (i < paramTypes.size() - 1) {
            str += ", ";
        }
    }
    if (isVariadic()) {
        str += ", ...";
    }
    str += ")";
    return str;
}

} // namespace ir