#pragma once

#include "ir/BasicBlock.h"
#include <set>
#include <vector>

namespace transforms {

struct Loop {
    ir::BasicBlock* header;           ///< Loop header block
    std::set<ir::BasicBlock*> blocks; ///< All blocks in the loop
    std::set<ir::BasicBlock*> exits;  ///< Loop exit blocks
    ir::BasicBlock* preheader;        ///< Preheader block (may be null initially)
    Loop* parent;                     ///< Parent loop (for nested loops)
    std::vector<Loop*> children;      ///< Nested loops

    Loop(ir::BasicBlock* h) : header(h), preheader(nullptr), parent(nullptr) {}
};

} // namespace transforms
