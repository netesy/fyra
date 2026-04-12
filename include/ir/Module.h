#pragma once

#include "Function.h"
#include "GlobalVariable.h"
#include "IRContext.h"
#include <list>
#include <string>
#include <memory>
#include <map>

namespace ir {

class Module {
public:
    Module(const std::string& name, std::shared_ptr<IRContext> ctx = nullptr);
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) noexcept = default;
    Module& operator=(Module&&) noexcept = default;

    const std::string& getName() const { return name; }
    IRContext* getContext() const { return context.get(); }
    std::shared_ptr<IRContext> getContextShared() const { return context; }

    std::list<std::unique_ptr<Function>>& getFunctions() { return functions; }
    const std::list<std::unique_ptr<Function>>& getFunctions() const { return functions; }

    void addFunction(std::unique_ptr<Function> func);
    Function* getFunction(const std::string& name);

    void addGlobalVariable(std::unique_ptr<GlobalVariable> gv);
    const std::list<std::unique_ptr<GlobalVariable>>& getGlobalVariables() const { return globalVariables; }

    void addType(const std::string& name, Type* type);
    Type* getType(const std::string& name) const;

    struct ExternDecl {
        std::string capability;
        std::vector<Type*> paramTypes;
        Type* returnType;
    };
    void addExternDecl(const std::string& name, ExternDecl decl) { externDecls[name] = decl; }
    const std::map<std::string, ExternDecl>& getExternDecls() const { return externDecls; }

private:
    std::shared_ptr<IRContext> context;
    std::string name;
    std::list<std::unique_ptr<Function>> functions;
    std::list<std::unique_ptr<GlobalVariable>> globalVariables;
    std::map<std::string, Type*> namedTypes;
    std::map<std::string, ExternDecl> externDecls;
};

} // namespace ir
