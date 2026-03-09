#pragma once

#include "User.h"

namespace ir {

class Value; // Forward declaration

class Use {
public:
    Use(User* u, Value* v);
    ~Use();

    Value* get() const { return v; }
    void set(Value* new_v);
    User* getUser() const { return user; }

private:
    User* user;
    Value* v;
};

} // namespace ir
