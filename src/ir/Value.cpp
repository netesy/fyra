#include "ir/Value.h"
#include "ir/Use.h"
#include <iostream>
#include <algorithm>

namespace ir {

Value::~Value() {
    // Break connections to all users before this value is destroyed.
    while (!use_list.empty()) {
        use_list.front()->set(nullptr);
    }
}

void Value::addUse(Use& u) {
    u.position = use_list.insert(use_list.end(), &u);
}

void Value::removeUse(Use& u) {
    use_list.erase(u.position);
}

void Value::replaceAllUsesWith(Value* newVal) {
    auto copy = use_list;
    for (Use* u : copy) {
        u->set(newVal);
    }
}

void Value::print(std::ostream& os) const {
    if (!name.empty()) {
        os << "%" << name;
    } else {
        os << "<unnamed:" << (void*)this << ">";
    }
}

} // namespace ir
