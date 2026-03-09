#include "codegen/target/TargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include <iostream>

namespace codegen {
namespace target {

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    
    // Determine element suffix based on type
    if (type->getElementType()->isFloatTy()) {
        ctx.elementSuffix = "ps";  // Packed single-precision
    } else if (type->getElementType()->isDoubleTy()) {
        ctx.elementSuffix = "pd";  // Packed double-precision
    } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type->getElementType())) {
        switch (intTy->getBitwidth()) {
            case 8:  ctx.elementSuffix = "epi8"; break;
            case 16: ctx.elementSuffix = "epi16"; break;
            case 32: ctx.elementSuffix = "epi32"; break;
            case 64: ctx.elementSuffix = "epi64"; break;
            default: ctx.elementSuffix = "epi32";
        }
    }
    
    // Determine width suffix for x86
    if (ctx.vectorWidth <= 128) {
        ctx.widthSuffix = "x";  // XMM registers
    } else if (ctx.vectorWidth <= 256) {
        ctx.widthSuffix = "y";  // YMM registers
    } else {
        ctx.widthSuffix = "z";  // ZMM registers
    }
    
    return ctx;
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned width) const {
    // Default implementation for x86-style naming
    if (width <= 128) {
        return "%xmm" + baseReg.substr(baseReg.find_last_of("0123456789"));
    } else if (width <= 256) {
        return "%ymm" + baseReg.substr(baseReg.find_last_of("0123456789"));
    } else {
        return "%zmm" + baseReg.substr(baseReg.find_last_of("0123456789"));
    }
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const {
    // Default implementation: append element suffix
    return baseInstr + ctx.elementSuffix;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

int32_t TargetInfo::getStackOffset(const CodeGen& cg, ir::Value* val) const {
    auto it = cg.getStackOffsets().find(val);
    if (it != cg.getStackOffsets().end()) return it->second;
    return 0;
}

} // namespace target
} // namespace codegen
