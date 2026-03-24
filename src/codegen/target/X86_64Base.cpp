#include "codegen/target/X86_64Base.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Parameter.h"
#include "ir/Type.h"
#include <algorithm>
#include <set>

namespace codegen {
namespace target {

X86_64Base::X86_64Base() {}

TypeInfo X86_64Base::getTypeInfo(const ir::Type* type) const {
    TypeInfo info;
    info.size = type->getSize() * 8;
    info.align = 8;
    info.regClass = type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer;
    info.isFloatingPoint = type->isFloatingPoint();
    info.isSigned = true;
    return info;
}

const std::vector<std::string>& X86_64Base::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Float) return floatRegs;
    return integerRegs;
}

const std::string& X86_64Base::getReturnRegister(const ir::Type* type) const {
    return (type && type->isFloatingPoint()) ? floatReturnReg : intReturnReg;
}

void X86_64Base::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        std::string p = (getName() == "win64") ? "" : "%";
        
        bool isLeaf = true;
        std::set<std::string> usedCalleeSaved;
        
        if (cg.currentFunction) {
            for (auto& bb : cg.currentFunction->getBasicBlocks()) {
                for (auto& instr : bb->getInstructions()) {
                    if (instr->getOpcode() == ir::Instruction::Call) isLeaf = false;
                    if (instr->hasPhysicalRegister()) {
                        std::string reg = integerRegs[instr->getPhysicalRegister()];
                        if (isCalleeSaved(reg)) usedCalleeSaved.insert(reg);
                    }
                }
            }
        }

        bool needsFrame = stack_size > 0 || !isLeaf || !usedCalleeSaved.empty();

        if (needsFrame) {
            *os << "  push " << p << framePtrReg << "\n  mov " << p << framePtrReg << ", " << p << stackPtrReg << "\n";
            
            for (const auto& reg : usedCalleeSaved) {
                *os << "  push " << reg << "\n";
            }
            
            if (stack_size > 0) {
                int adjusted_stack = stack_size;
                int pushed = 1 + usedCalleeSaved.size(); // rbp + callee-saved
                if ((pushed * 8 + adjusted_stack) % 16 != 0) adjusted_stack += 8;
                if (p == "%") {
                    *os << "  subq $" << adjusted_stack << ", %" << stackPtrReg << "\n";
                } else {
                    *os << "  sub " << p << stackPtrReg << ", " << adjusted_stack << "\n";
                }
            }
        }
    }
}

void X86_64Base::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        std::string p = (getName() == "win64") ? "" : "%";
        
        bool isLeaf = true;
        std::vector<std::string> usedCalleeSaved;
        bool usesStack = false;
        
        if (cg.currentFunction) {
            if (cg.currentFunction->getStackFrameSize() > 0) usesStack = true;
            if (!cg.getStackOffsets().empty()) usesStack = true;

            for (auto& bb : cg.currentFunction->getBasicBlocks()) {
                for (auto& instr : bb->getInstructions()) {
                    if (instr->getOpcode() == ir::Instruction::Call) isLeaf = false;
                    if (instr->hasPhysicalRegister()) {
                        std::string reg = integerRegs[instr->getPhysicalRegister()];
                        if (isCalleeSaved(reg) && std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), reg) == usedCalleeSaved.end()) {
                            usedCalleeSaved.push_back(reg);
                        }
                    }
                }
            }
        }
        
        std::reverse(usedCalleeSaved.begin(), usedCalleeSaved.end());
        bool needsFrame = usesStack || !isLeaf || !usedCalleeSaved.empty();

        if (needsFrame) {
            if (!usedCalleeSaved.empty()) {
                if (p == "%") {
                    *os << "  leaq -" << (usedCalleeSaved.size() * 8) << "(%rbp), %rsp\n";
                } else {
                    *os << "  lea rsp, [rbp - " << (usedCalleeSaved.size() * 8) << "]\n";
                }
                for (const auto& reg : usedCalleeSaved) {
                    *os << "  pop " << reg << "\n";
                }
                *os << "  pop " << p << framePtrReg << "\n";
            } else {
                if (p == "%") {
                    *os << "  leave\n";
                } else {
                    *os << "  mov " << p << stackPtrReg << ", " << p << framePtrReg << "\n";
                    *os << "  pop " << p << framePtrReg << "\n";
                }
            }
        }
        *os << "  ret\n";
    }
}

bool X86_64Base::isCallerSaved(const std::string& reg) const {
    return callerSaved.count(reg) && callerSaved.at(reg);
}

bool X86_64Base::isCalleeSaved(const std::string& reg) const {
    return calleeSaved.count(reg) && calleeSaved.at(reg);
}

std::string X86_64Base::formatStackOperand(int offset) const {
    std::string p = (getName() == "win64") ? "" : "%";
    if (getName() == "win64") return "[rbp + " + std::to_string(offset) + "]";
    return std::to_string(offset) + "(%" + framePtrReg + ")";
}

std::string X86_64Base::formatGlobalOperand(const std::string& name) const {
    if (getName() == "win64") return "[" + name + "]";
    return name + "(%rip)";
}

std::string X86_64Base::getRegisterName(const std::string& baseReg, const ir::Type* type) const {
    if (!type || type->getSize() >= 8) return baseReg;
    if (type->isFloatingPoint()) return baseReg;
    
    bool hasPrefix = (baseReg[0] == '%');
    std::string name = hasPrefix ? baseReg.substr(1) : baseReg;
    std::string result;

    if (type->getSize() == 4) {
        if (name == "rax") result = "eax";
        else if (name == "rbx") result = "ebx";
        else if (name == "rcx") result = "ecx";
        else if (name == "rdx") result = "edx";
        else if (name == "rsi") result = "esi";
        else if (name == "rdi") result = "edi";
        else if (name == "rbp") result = "ebp";
        else if (name == "rsp") result = "esp";
        else if (name.size() > 1 && name[0] == 'r' && isdigit(name[1])) result = name + "d";
        else result = name;
    } else {
        result = name;
    }
    
    return hasPrefix ? "%" + result : result;
}

}
}
