#include "ir/Use.h"
#include "ir/Value.h"
#include <iostream>

namespace ir {

Use::Use(User* u, Value* v) : user(u), v(nullptr) {
    set(v);
}

Use::~Use() {
    if (v) v->removeUse(*this);
}

#include <iostream>

void Use::set(Value* new_v) {
    if (v) {
        v->removeUse(*this);
    }
    v = new_v;
    if (v) {
        v->addUse(*this);
    }
}

} // namespace ir
