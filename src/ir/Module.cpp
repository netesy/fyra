#include "ir/Module.h"

namespace ir {

Module::Module(const std::string& name) : name(name) {}

void Module::addFunction(std::unique_ptr<Function> func) {
    functions.push_back(std::move(func));
}

void Module::addGlobalVariable(GlobalVariable* gv) {
    globalVariables.emplace_back(gv);
}

Function* Module::getFunction(const std::string& name) {
    for (auto& func : functions) {
        if (func->getName() == name) {
            return func.get();
        }
    }
    return nullptr;
}

void Module::addType(const std::string& name, Type* type) {
    namedTypes[name] = type;
}

Type* Module::getType(const std::string& name) const {
    auto it = namedTypes.find(name);
    if (it != namedTypes.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace ir
