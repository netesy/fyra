#pragma once

#include "Type.h"
#include <string>
#include <list>

namespace ir {

class Use; // Forward declaration

class Value {
public:
    Value(Type *ty) : type(ty) {}
    virtual ~Value();

    Type* getType() const { return type; }

    const std::string& getName() const { return name; }
    void setName(const std::string& newName) { name = newName; }

    void addUse(Use& u);
    void removeUse(Use& u);
    bool use_empty() const { return use_list.empty(); }
    const std::list<Use*>& getUseList() const { return use_list; }
    void replaceAllUsesWith(Value* newVal);

    // For debugging and printing the IR
    virtual void print(std::ostream& os) const;

protected:
    Type* type;
    std::string name;
    std::list<Use*> use_list;
};

} // namespace ir
