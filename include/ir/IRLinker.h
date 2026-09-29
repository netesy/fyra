#pragma once

#include "ir/Module.h"
#include <memory>
#include <string>

namespace ir {

class IRLinker {
public:
    IRLinker() = default;
    ~IRLinker() = default;

    /// Merges the source module into the destination module.
    /// Moves functions, global variables, named types, and extern declarations from src into dest.
    /// Handles symbol renaming if internal linkage conflicts arise.
    static bool linkModules(Module& dest, std::unique_ptr<Module> src, std::string& errorMsg);
};

} // namespace ir
