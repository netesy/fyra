#pragma once

#include "Function.h"
#include "GlobalVariable.h"
#include <list>
#include <string>
#include <memory>

namespace ir {

class Module {
public:
    Module(const std::string& name);

    const std::string& getName() const { return name; }

    std::list<std::unique_ptr<Function>>& getFunctions() { return functions; }
    const std::list<std::unique_ptr<Function>>& getFunctions() const { return functions; }

    void addFunction(std::unique_ptr<Function> func);
    Function* getFunction(const std::string& name);

    void addGlobalVariable(GlobalVariable* gv);
    const std::list<std::unique_ptr<GlobalVariable>>& getGlobalVariables() const { return globalVariables; }

    void addType(const std::string& name, Type* type);
    Type* getType(const std::string& name) const;

private:
    std::string name;
    std::list<std::unique_ptr<Function>> functions;
    std::list<std::unique_ptr<GlobalVariable>> globalVariables;
    std::map<std::string, Type*> namedTypes;
};

} // namespace ir
