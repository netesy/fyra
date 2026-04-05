#include "codegen/target/TargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Constant.h"

namespace codegen {
namespace target {

void TargetInfo::emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) {
    std::string label = getBBLabel(&bb);
    if (auto* os = cg.getTextStream()) {
        *os << label << ":\n";
    } else {
        CodeGen::SymbolInfo s;
        s.name = label;
        s.sectionName = ".text";
        s.value = cg.getAssembler().getCodeSize();
        s.type = 0; // STT_NOTYPE
        s.binding = 1; // STB_GLOBAL
        cg.addSymbol(s);
    }
}

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    (void)type;
    return SIMDContext();
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned width) const {
    (void)width;
    return baseReg;
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const {
    (void)ctx;
    return baseInstr;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return std::to_string(C->getValue());
}

int32_t TargetInfo::getStackOffset(const CodeGen& cg, ir::Value* val) const {
    if (cg.getStackOffsets().count(val)) {
        return cg.getStackOffsets().at(val);
    }
    return 0;
}

std::string TargetInfo::getFunctionEpilogueLabel(const ir::Function& func) const {
    return func.getName() + "_epilogue";
}

std::string TargetInfo::getBBLabel(const ir::BasicBlock* bb) const {
    if (!bb) return "label";
    return bb->getParent()->getName() + "_" + bb->getName();
}

}
}
