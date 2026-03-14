#include "codegen/debug/DWARFGenerator.h"
#include "codegen/CodeGen.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"

namespace codegen {
namespace debug {

DebugInfoManager::DebugInfoManager() : dwarfGenerator(std::make_unique<DWARFGenerator>()), debugEnabled(false), currentLine(0) {
}

void DebugInfoManager::generateDebugInfo(CodeGen& cg, std::ostream& os, ir::Module& module) {
    if (debugEnabled && dwarfGenerator) {
        // Generate debug info sections
        dwarfGenerator->generateDebugInfoSection(os);
        dwarfGenerator->generateDebugAbbrevSection(os);
        dwarfGenerator->generateDebugStringSection(os);
    }
}

void DebugInfoManager::beforeFunctionEmission(CodeGen& cg, std::ostream& os, const ir::Function& func) {
    if (debugEnabled && dwarfGenerator) {
        // Emit debug directives before function
        dwarfGenerator->emitDebugDirectives(os);
    }
}

void DebugInfoManager::afterFunctionEmission(CodeGen& cg, std::ostream& os, const ir::Function& func, uint64_t address) {
    if (debugEnabled && dwarfGenerator) {
        // Emit debug frame information after function
        dwarfGenerator->generateDebugFrameSection(os);
    }
}

void DebugInfoManager::beforeInstructionEmission(CodeGen& cg, std::ostream& os, const ir::Instruction& instr, uint64_t address) {
    if (debugEnabled && dwarfGenerator) {
        // Track instruction location for debugging
        // This is a no-op for now, but can be extended
    }
}

void DebugInfoManager::emitFunctionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Function& func, uint64_t startAddr) {
    if (debugEnabled && dwarfGenerator) {
        dwarfGenerator->beginFunction(func, startAddr);
    }
}

void DebugInfoManager::emitInstructionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Instruction& instr, uint64_t address) {
    if (debugEnabled && dwarfGenerator) {
        // Emit instruction-level debug info
    }
}

} // namespace debug
} // namespace codegen