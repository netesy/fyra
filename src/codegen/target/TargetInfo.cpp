#include "codegen/target/TargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/Type.h"
#include <iostream>

namespace codegen {
namespace target {

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    return ctx;
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned) const {
    return baseReg;
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext&) const {
    return baseInstr;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

int32_t TargetInfo::getStackOffset(const CodeGen& cg, ir::Value* val) const {
    auto it = cg.getStackOffsets().find(val);
    if (it != cg.getStackOffsets().end()) return it->second;
    return 0;
}

}
}
