#include "codegen/target/MacOS_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include <ostream>

namespace codegen {
namespace target {

void MacOS_x64::initRegisters() {
    SystemV_x64::initRegisters();
}

MacOS_x64::MacOS_x64() : SystemV_x64() {
    initRegisters();
}

const std::vector<std::string>& MacOS_x64::getIntegerArgumentRegisters() const {
    return SystemV_x64::getIntegerArgumentRegisters();
}

const std::vector<std::string>& MacOS_x64::getFloatArgumentRegisters() const {
    return SystemV_x64::getFloatArgumentRegisters();
}

const std::string& MacOS_x64::getIntegerReturnRegister() const {
    return SystemV_x64::getIntegerReturnRegister();
}

const std::string& MacOS_x64::getFloatReturnRegister() const {
    return SystemV_x64::getFloatReturnRegister();
}

void MacOS_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // macOS standard: prefix symbols with underscore
        *os << "_" << func.getName() << ":\n";
    }

    // Use SystemV class for stack frame setup
    SystemV_x64::emitFunctionPrologue(cg, func);
}

void MacOS_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n.globl _main\n";
        *os << "_main:\n";
        *os << "  call _main_user\n";
        *os << "  ret\n";
    }
}

} // namespace target
} // namespace codegen
