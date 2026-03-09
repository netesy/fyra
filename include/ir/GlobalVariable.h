#pragma once

#include "GlobalValue.h"
#include "Constant.h"

namespace ir {

class GlobalVariable : public GlobalValue {
public:
    GlobalVariable(Type* ty, const std::string& name, Constant* initializer, bool isThreadLocal, const std::string& section)
        : GlobalValue(ty, name), initializer(initializer), threadLocal(isThreadLocal), section(section) {}

    Constant* getInitializer() const { return initializer; }
    bool isThreadLocal() const { return threadLocal; }
    const std::string& getSection() const { return section; }

private:
    Constant* initializer;
    bool threadLocal;
    std::string section;
};

} // namespace ir
