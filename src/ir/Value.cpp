#include "ir/Value.h"
#include "ir/Use.h"
#include <iostream>

namespace ir {

Value::~Value() {
    auto uses = use_list;
    for (Use* u : uses) {
        u->set(nullptr);
    }
}

void Value::addUse(Use& u) {
    use_list.push_back(&u);
}

void Value::removeUse(Use& u) {
    use_list.remove(&u);
}

void Value::replaceAllUsesWith(Value* newVal) {
    auto uses = use_list; // Make a copy to avoid iterator invalidation
    for (Use* u : uses) {
        u->set(newVal);
    }
}

void Value::print(std::ostream& os) const {
    if (!name.empty()) {
        os << "%" << name;
    } else {
        // In a real compiler, we might print the pointer address
        // or a unique ID for unnamed values.
        os << "<unnamed>";
    }
}

} // namespace ir
