#include "ir/Module.h"
#include "ir/Type.h"

namespace ir {

Module::Module(const std::string& name, std::shared_ptr<IRContext> ctx) : context(ctx), name(name) {}

Module::~Module() {
    // Explicitly clear functions to trigger destruction in a controlled order
    // if needed, but unique_ptr handles it.
}

void Module::addFunction(std::unique_ptr<Function> func) {
    functions.push_back(std::move(func));
}

void Module::addGlobalVariable(std::unique_ptr<GlobalVariable> gv) {
    globalVariables.push_back(std::move(gv));
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
