#pragma once

#include "ir/Function.h"

namespace transforms {

class CFGBuilder {
public:
    static void run(ir::Function& func);
    // Refresh one changed block, preserving every unaffected edge. The caller
    // supplies its next block in layout order for implicit fallthrough.
    static void updateOutgoingEdges(ir::BasicBlock& block, ir::BasicBlock* nextBlock);

private:
    static void appendOutgoingEdges(ir::BasicBlock& block, ir::BasicBlock* nextBlock);
};

} // namespace transforms
