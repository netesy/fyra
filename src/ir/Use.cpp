#include "ir/Use.h"
#include "ir/Value.h"

namespace ir {

Use::Use(User* u, Value* v) : user(u), v(nullptr) {
    set(v);
}

Use::~Use() {
    set(nullptr);
}

void Use::set(Value* new_v) {
    if (v == new_v) return;
    if (v) {
        v->removeUse(*this);
    }
    v = new_v;
    if (v) {
        v->addUse(*this);
    }
}

} // namespace ir
