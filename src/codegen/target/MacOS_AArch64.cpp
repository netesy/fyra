#include "codegen/target/MacOS_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include <ostream>

namespace codegen {
namespace target {

MacOS_AArch64::MacOS_AArch64() : AArch64() {}

void MacOS_AArch64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // macOS standard: prefix symbols with underscore
        *os << "_" << func.getName() << ":\n";
    }

    // Use base AArch64 class for stack frame setup
    AArch64::emitFunctionPrologue(cg, func);
}

void MacOS_AArch64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n.globl _main\n";
        *os << "_main:\n";
        *os << "  bl _main_user\n";
        *os << "  ret\n";
    }
}

} // namespace target
} // namespace codegen
