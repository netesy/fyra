#pragma once

#include "ir/Module.h"
#include <string>
#include <vector>

namespace ir {

class Validator {
public:
    static bool validateModule(const Module& module, std::vector<std::string>& errors);
};

} // namespace ir
