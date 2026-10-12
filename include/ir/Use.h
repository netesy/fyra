#pragma once

#include "User.h"
#include <list>

namespace ir {

class Value; // Forward declaration

class Use {
public:
    Use(User* u, Value* v);
    ~Use();

    Value* get() const { return v; }
    void set(Value* new_v);
    User* getUser() const { return user; }

    Value* getOriginalValue() const { return originalValue ? originalValue : v; }
    void setOriginalValue(Value* orig) { originalValue = orig; }

private:
    friend class Value;
    std::list<Use*>::iterator position;
    User* user;
    Value* v;
    Value* originalValue = nullptr;
};

} // namespace ir
