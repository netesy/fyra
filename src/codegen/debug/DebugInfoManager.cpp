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
        dwarfGenerator->beginCompileUnit(module.getSourceFilename(), "fyra compiler");

        for (auto& func : module.getFunctions()) {
            dwarfGenerator->beginFunction(*func, 0); // Start address will be relative
            dwarfGenerator->endFunction(0); // End address placeholder
        }

        // Generate debug info sections
        dwarfGenerator->generateDebugInfoSection(os);
        dwarfGenerator->generateDebugAbbrevSection(os);
        dwarfGenerator->generateDebugStringSection(os);
        dwarfGenerator->generateLineTable(os);
        dwarfGenerator->generateDebugFrameSection(os);
    }
}

void DebugInfoManager::beforeFunctionEmission(CodeGen& cg, std::ostream& os, const ir::Function& func) {
    if (debugEnabled && dwarfGenerator) {
        dwarfGenerator->emitDebugDirectives(os);
        currentLine = func.getSourceLine();
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
        unsigned line = instr.getSourceLine();
        if (line != 0 && line != currentLine) {
            currentLine = line;
            dwarfGenerator->addLineInfo(line, 0, cg.module.getSourceFilename(), address);
        }
    }
}

void DebugInfoManager::emitFunctionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Function& func, uint64_t startAddr) {
    if (debugEnabled && dwarfGenerator) {
        dwarfGenerator->beginFunction(func, startAddr);
    }
}

void DebugInfoManager::emitInstructionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Instruction& instr, uint64_t address) {
    if (debugEnabled && dwarfGenerator) {
        unsigned line = instr.getSourceLine() ? instr.getSourceLine() : currentLine;
        os << "  .loc 1 " << line << " 0\n";
    }
}

} // namespace debug
} // namespace codegen