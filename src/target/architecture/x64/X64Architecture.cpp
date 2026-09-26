#include "target/architecture/x64/X64Architecture.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include "codegen/asm/Assembler.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/PhiNode.h"
#include <iostream>
#include <ostream>
#include <cstring>

namespace target {

static std::string to8BitReg(const std::string& reg) {
    if (reg == "%rax" || reg == "rax" || reg == "%eax" || reg == "eax") return "%al";
    if (reg == "%rcx" || reg == "rcx" || reg == "%ecx" || reg == "ecx") return "%cl";
    if (reg == "%rdx" || reg == "rdx" || reg == "%edx" || reg == "edx") return "%dl";
    if (reg == "%rbx" || reg == "rbx" || reg == "%ebx" || reg == "ebx") return "%bl";
    if (reg == "%rsi" || reg == "rsi" || reg == "%esi" || reg == "esi") return "%sil";
    if (reg == "%rdi" || reg == "rdi" || reg == "%edi" || reg == "edi") return "%dil";
    if (reg == "%r8" || reg == "r8" || reg == "%r8d" || reg == "r8d") return "%r8b";
    if (reg == "%r9" || reg == "r9" || reg == "%r9d" || reg == "r9d") return "%r9b";
    if (reg == "%r10" || reg == "r10" || reg == "%r10d" || reg == "r10d") return "%r10b";
    if (reg == "%r11" || reg == "r11" || reg == "%r11d" || reg == "r11d") return "%r11b";
    if (reg == "%r12" || reg == "r12" || reg == "%r12d" || reg == "r12d") return "%r12b";
    if (reg == "%r13" || reg == "r13" || reg == "%r13d" || reg == "r13d") return "%r13b";
    if (reg == "%r14" || reg == "r14" || reg == "%r14d" || reg == "r14d") return "%r14b";
    if (reg == "%r15" || reg == "r15" || reg == "%r15d" || reg == "r15d") return "%r15b";
    return reg;
}

static std::string to16BitReg(const std::string& reg) {
    if (reg == "%rax" || reg == "rax" || reg == "%eax" || reg == "eax") return "%ax";
    if (reg == "%rcx" || reg == "rcx" || reg == "%ecx" || reg == "ecx") return "%cx";
    if (reg == "%rdx" || reg == "rdx" || reg == "%edx" || reg == "edx") return "%dx";
    if (reg == "%rbx" || reg == "rbx" || reg == "%ebx" || reg == "ebx") return "%bx";
    if (reg == "%rsi" || reg == "rsi" || reg == "%esi" || reg == "esi") return "%si";
    if (reg == "%rdi" || reg == "rdi" || reg == "%edi" || reg == "edi") return "%di";
    if (reg == "%r8" || reg == "r8" || reg == "%r8d" || reg == "r8d") return "%r8w";
    if (reg == "%r9" || reg == "r9" || reg == "%r9d" || reg == "r9d") return "%r9w";
    if (reg == "%r10" || reg == "r10" || reg == "%r10d" || reg == "r10d") return "%r10w";
    if (reg == "%r11" || reg == "r11" || reg == "%r11d" || reg == "r11d") return "%r11w";
    if (reg == "%r12" || reg == "r12" || reg == "%r12d" || reg == "r12d") return "%r12w";
    if (reg == "%r13" || reg == "r13" || reg == "%r13d" || reg == "r13d") return "%r13w";
    if (reg == "%r14" || reg == "r14" || reg == "%r14d" || reg == "r14d") return "%r14w";
    if (reg == "%r15" || reg == "r15" || reg == "%r15d" || reg == "r15d") return "%r15w";
    return reg;
}

static std::string to32BitReg(const std::string& reg) {
    if (reg == "%rax" || reg == "rax" || reg == "%eax" || reg == "eax") return "%eax";
    if (reg == "%rcx" || reg == "rcx" || reg == "%ecx" || reg == "ecx") return "%ecx";
    if (reg == "%rdx" || reg == "rdx" || reg == "%edx" || reg == "edx") return "%edx";
    if (reg == "%rbx" || reg == "rbx" || reg == "%ebx" || reg == "ebx") return "%ebx";
    if (reg == "%rsi" || reg == "rsi" || reg == "%esi" || reg == "esi") return "%esi";
    if (reg == "%rdi" || reg == "rdi" || reg == "%edi" || reg == "edi") return "%edi";
    if (reg == "%r8" || reg == "r8" || reg == "%r8d" || reg == "r8d") return "%r8d";
    if (reg == "%r9" || reg == "r9" || reg == "%r9d" || reg == "r9d") return "%r9d";
    if (reg == "%r10" || reg == "r10" || reg == "%r10d" || reg == "r10d") return "%r10d";
    if (reg == "%r11" || reg == "r11" || reg == "%r11d" || reg == "r11d") return "%r11d";
    if (reg == "%r12" || reg == "r12" || reg == "%r12d" || reg == "r12d") return "%r12d";
    if (reg == "%r13" || reg == "r13" || reg == "%r13d" || reg == "r13d") return "%r13d";
    if (reg == "%r14" || reg == "r14" || reg == "%r14d" || reg == "r14d") return "%r14d";
    if (reg == "%r15" || reg == "r15" || reg == "%r15d" || reg == "r15d") return "%r15d";
    return reg;
}

static std::string to64BitReg(const std::string& reg) {
    if (reg == "%rax" || reg == "rax" || reg == "%eax" || reg == "eax") return "%rax";
    if (reg == "%rcx" || reg == "rcx" || reg == "%ecx" || reg == "ecx") return "%rcx";
    if (reg == "%rdx" || reg == "rdx" || reg == "%edx" || reg == "edx") return "%rdx";
    if (reg == "%rbx" || reg == "rbx" || reg == "%ebx" || reg == "ebx") return "%rbx";
    if (reg == "%rsi" || reg == "rsi" || reg == "%esi" || reg == "esi") return "%rsi";
    if (reg == "%rdi" || reg == "rdi" || reg == "%edi" || reg == "edi") return "%rdi";
    if (reg == "%r8" || reg == "r8" || reg == "%r8d" || reg == "r8d") return "%r8";
    if (reg == "%r9" || reg == "r9" || reg == "%r9d" || reg == "r9d") return "%r9";
    if (reg == "%r10" || reg == "r10" || reg == "%r10d" || reg == "r10d") return "%r10";
    if (reg == "%r11" || reg == "r11" || reg == "%r11d" || reg == "r11d") return "%r11";
    if (reg == "%r12" || reg == "r12" || reg == "%r12d" || reg == "r12d") return "%r12";
    if (reg == "%r13" || reg == "r13" || reg == "%r13d" || reg == "r13d") return "%r13";
    if (reg == "%r14" || reg == "r14" || reg == "%r14d" || reg == "r14d") return "%r14";
    if (reg == "%r15" || reg == "r15" || reg == "%r15d" || reg == "r15d") return "%r15";
    return reg;
}

static bool is32BitType(const ir::Type* type) {
    if (!type) return false;
    if (type->isFloatTy()) return true;
    if (type->isInteger()) return type->getSize() <= 4;
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        return it->getBitwidth() <= 32;
    }
    return false;
}

static bool is32BitRegisterName(const std::string& reg) {
    if (reg.size() >= 3 && reg[0] == '%' && reg[1] == 'e') return true;
    if (reg.size() >= 4 && reg[0] == '%' && reg.back() == 'd') return true;
    return false;
}

static bool isXmmRegisterName(const std::string& reg) {
    return reg.find("xmm") != std::string::npos || reg.find("ymm") != std::string::npos || reg.find("zmm") != std::string::npos;
}

static std::string toYmmReg(const std::string& reg) {
    std::string r = reg;
    size_t pos = r.find("xmm");
    if (pos != std::string::npos) {
        r.replace(pos, 3, "ymm");
    }
    return r;
}

static std::string toZmmReg(const std::string& reg) {
    std::string r = reg;
    size_t pos = r.find("xmm");
    if (pos != std::string::npos) {
        r.replace(pos, 3, "zmm");
    }
    return r;
}

static bool isDirectGprRegister(const std::string& op) {
    if (op.empty()) return false;
    if (isXmmRegisterName(op)) return false;
    if (op[0] == '%' || op[0] == 'r' || op[0] == 'e') {
        if (op[0] == '-' || op[0] == '[' || op[0] == '$') return false;
        if (op.find('(') != std::string::npos || op.find(')') != std::string::npos) return false;
        return true;
    }
    return false;
}

static bool isYmmRegisterName(const std::string& reg) {
    return reg.find("ymm") != std::string::npos;
}

static void emitMov(CodeGen& cg, std::ostream* os, const std::string& src, const std::string& dst, bool is32, unsigned totalBits = 128) {
    if (!os) return;
    if (isXmmRegisterName(src) || isXmmRegisterName(dst)) {
        if (src == dst) return;
        if (isXmmRegisterName(src) && isXmmRegisterName(dst)) {
            if (totalBits == 256 || isYmmRegisterName(src) || isYmmRegisterName(dst)) {
                *os << "  vmovdqu " << toYmmReg(src) << ", " << toYmmReg(dst) << "\n";
            } else {
                *os << "  movdqu " << src << ", " << dst << "\n";
            }
        } else if (isXmmRegisterName(src)) {
            if (is32BitRegisterName(dst) || is32) {
                *os << "  movd " << src << ", " << to32BitReg(dst) << "\n";
            } else {
                *os << "  movq " << src << ", " << to64BitReg(dst) << "\n";
            }
        } else {
            if (is32BitRegisterName(src) || is32) {
                *os << "  movd " << to32BitReg(src) << ", " << dst << "\n";
            } else {
                *os << "  movq " << to64BitReg(src) << ", " << dst << "\n";
            }
        }
        cg.lastStoreOp = "";
        return;
    }

    if (is32BitRegisterName(src) || is32BitRegisterName(dst)) is32 = true;
    std::string regRax = is32 ? "%eax" : "%rax";
    std::string s = src.empty() ? regRax : src;
    std::string d = dst.empty() ? regRax : dst;

    if (!s.empty() && s[0] == '%') s = is32 ? to32BitReg(s) : to64BitReg(s);
    if (!d.empty() && d[0] == '%') d = is32 ? to32BitReg(d) : to64BitReg(d);
    if (s == d) return;


    if (!cg.lastStoreOp.empty() && s == cg.lastStoreOp && (d == regRax || d == "%rax" || d == "%eax")) {
        return;
    }

    bool srcIsAddr = (!s.empty() && s[0] == '(' && s.find(',') != std::string::npos);
    if (srcIsAddr) {
        std::string reg = is32 ? "%eax" : "%rax";
        *os << "  leaq " << s << ", " << reg << "\n";
        if (d != reg) {
            *os << "  movq " << reg << ", " << d << "\n";
        }
        cg.lastStoreOp = d;
        return;
    }

    bool srcIsMem = (!s.empty() && (s[0] == '-' || s[0] == '[' || s.find("(%rbp)") != std::string::npos));
    bool dstIsMem = (!d.empty() && (d[0] == '-' || d[0] == '[' || d.find("(%rbp)") != std::string::npos));

    if (srcIsMem && dstIsMem) {
        bool isWin = cg.getTargetInfo() && (cg.getTargetInfo()->getName().find("windows") != std::string::npos || cg.getTargetInfo()->getName().find("win64") != std::string::npos);
        if (isWin) {
            std::string reg = is32 ? "eax" : "rax";
            *os << "  mov " << reg << ", " << s << "\n";
            *os << "  mov " << d << ", " << reg << "\n";
        } else {
            std::string reg = is32 ? "%eax" : "%rax";
            std::string op = is32 ? "movl" : "movq";
            *os << "  " << op << " " << s << ", " << reg << "\n";
            *os << "  " << op << " " << reg << ", " << d << "\n";
        }
        cg.lastStoreOp = d;
        return;
    }

    std::string op = is32 ? "movl" : "movq";
    *os << "  " << op << " " << s << ", " << d << "\n";
    if (!d.empty() && d[0] == '-') {
        cg.lastStoreOp = d;
    } else if (d == regRax || d == "%rax" || d == "%eax") {
        if (s != cg.lastStoreOp) cg.lastStoreOp = "";
    } else {
        cg.lastStoreOp = "";
    }
}

X64Architecture::X64Architecture(X64ABI abi) : abi(abi) {
    initRegisters();
}

void X64Architecture::initRegisters() {
    if (abi == X64ABI::SystemV) {
        integerRegs = {"r10", "r11", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "rbx", "r12", "r13", "r14", "r15"};
        integerArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    } else {
        integerRegs = {"rbx", "rsi", "rdi", "r12", "r13", "r14", "r15"};
        integerArgRegs = {"rcx", "rdx", "r8", "r9"};
        floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3"};
    }
    floatRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    intReturnReg = "rax"; floatReturnReg = "xmm0"; framePtrReg = "rbp"; stackPtrReg = "rsp";
    for (const auto& r : integerRegs) { callerSaved[r] = false; calleeSaved[r] = true; }
}

TypeInfo X64Architecture::getTypeInfo(const ir::Type* type) const {
    if (!type || type->isVoidTy()) return {0, 0, RegisterClass::Integer, false, false};
    if (type->isFloatTy()) return {4, 4, RegisterClass::Float, false, false};
    if (type->isDoubleTy()) return {8, 8, RegisterClass::Float, false, false};
    if (type->isPointerTy()) return {8, 8, RegisterClass::Integer, false, false};
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        int w = it->getBitwidth();
        if (w <= 8) return {1, 1, RegisterClass::Integer, false, false};
        if (w <= 16) return {2, 2, RegisterClass::Integer, false, false};
        if (w <= 32) return {4, 4, RegisterClass::Integer, false, false};
        return {8, 8, RegisterClass::Integer, false, false};
    }
    return {8, 8, RegisterClass::Integer, false, false};
}

const std::vector<std::string>& X64Architecture::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Integer) return integerRegs;
    if (regClass == RegisterClass::Float) return floatRegs;
    return vectorRegs;
}

const std::string& X64Architecture::getReturnRegister(const ir::Type* type) const {
    if (type && (type->isFloatTy() || type->isDoubleTy())) return floatReturnReg;
    return intReturnReg;
}

void X64Architecture::emitHeader(CodeGen& cg) {
}

X64FrameLayout X64Architecture::computeFrameLayout(CodeGen& cg, ir::Function& func) const {
    X64FrameLayout layout;
    if (abi == X64ABI::SystemV) {
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                auto opc = instr->getOpcode();
                if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                    layout.makesCalls = true;
                    break;
                }
            }
            if (layout.makesCalls) break;
        }

        static const std::vector<std::string> calleeList = {"rbx", "r12", "r13", "r14", "r15"};
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->hasPhysicalRegister()) {
                    size_t regIdx = instr->getPhysicalRegister();
                    if (regIdx < integerRegs.size()) {
                        const std::string& regName = integerRegs[regIdx];
                        if (std::find(calleeList.begin(), calleeList.end(), regName) != calleeList.end()) {
                            if (std::find(layout.usedCalleeRegs.begin(), layout.usedCalleeRegs.end(), regName) == layout.usedCalleeRegs.end()) {
                                layout.usedCalleeRegs.push_back(regName);
                            }
                        }
                    }
                }
            }
        }

        size_t pIdx0 = 0;
        for (auto& param : func.getParameters()) {
            if (pIdx0 >= 6) {
                int stackArgOff = 16 + (pIdx0 - 6) * 8;
                cg.getStackOffsets()[param.get()] = stackArgOff;
            }
            pIdx0++;
        }
        for (const auto& [vreg, slotBytes] : func.getStackSlots()) {
            if (vreg) {
                cg.getStackOffsets()[const_cast<ir::Value*>(vreg)] = -slotBytes;
            }
        }

        int maxAlign = 16;
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType()) {
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(instr->getType())) {
                        unsigned bits = vt->getSize() * 8;
                        if (bits >= 512) maxAlign = std::max(maxAlign, 64);
                        else if (bits >= 256) maxAlign = std::max(maxAlign, 32);
                        else if (bits >= 128) maxAlign = std::max(maxAlign, 16);
                    }
                }
            }
        }

        int current_offset = -8 * (int)layout.usedCalleeRegs.size();

        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    if (func.hasStackSlot(instr.get())) {
                        cg.getStackOffsets()[instr.get()] = -func.getStackSlotForVreg(instr.get());
                    }
                }
            }
        }

        int min_offset = current_offset;
        for (auto& [val, off] : cg.getStackOffsets()) {
            int slotSize = 8;
            if (val->getType()) {
                if (auto* vt = dynamic_cast<const ir::VectorType*>(val->getType())) {
                    slotSize = vt->getSize();
                }
            }
            if (off - slotSize < min_offset) {
                min_offset = off - slotSize;
            }
        }

        int total_frame = std::abs(min_offset);
        if (total_frame % maxAlign != 0) {
            total_frame += (maxAlign - (total_frame % maxAlign));
        }
        layout.stackAlloc = total_frame - 8 * (1 + (int)layout.usedCalleeRegs.size());
        bool hasStackParameters = func.getParameters().size() > integerArgRegs.size();
        layout.isZeroFrame = (!layout.makesCalls && total_frame == 0 &&
                              layout.usedCalleeRegs.empty() && !hasStackParameters && func.getParameters().empty());
    } else {
        layout.makesCalls = true;
        layout.usedCalleeRegs = {"rbx", "rsi", "rdi", "r12", "r13", "r14", "r15"};
        int current_offset = -64;
        for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } }
        layout.stackAlloc = std::abs(current_offset + 56) + 32; // Shadow space
        if ((layout.stackAlloc + 64 + 8) % 16 != 0) layout.stackAlloc += 16 - ((layout.stackAlloc + 64 + 8) % 16);
        layout.isZeroFrame = false;
    }
    return layout;
}

void X64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    X64FrameLayout layout = computeFrameLayout(cg, func);
    if (abi == X64ABI::SystemV) {
        if (auto* os = cg.getTextStream()) {
            *os << "  .cfi_startproc\n";
            if (!layout.isZeroFrame) {
                *os << "  pushq %rbp\n";
                *os << "  .cfi_def_cfa_offset 16\n";
                *os << "  .cfi_offset 6, -16\n";
                *os << "  movq %rsp, %rbp\n";
                *os << "  .cfi_def_cfa_register 6\n";
                for (const auto& reg : layout.usedCalleeRegs) {
                    *os << "  pushq %" << reg << "\n";
                }
            }
        } else {
            auto& as = cg.getAssembler();
            if (!layout.isZeroFrame) {
                as.emitByte(0x55);
                as.emitBytes({0x48, 0x89, 0xE5});
                for (const auto& reg : layout.usedCalleeRegs) {
                    uint8_t r = getArchRegIndex(reg);
                    if (r >= 8) as.emitByte(0x41);
                    as.emitByte(0x50 + (r & 7));
                }
            }
        }

        if (!layout.isZeroFrame) {
            if (auto* os = cg.getTextStream()) {
                if (layout.stackAlloc > 0) *os << "  subq $" << layout.stackAlloc << ", %rsp\n";
                static const std::vector<std::string> sysvArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                size_t pIdx = 0;
                for (auto& param : func.getParameters()) {
                    if (pIdx < sysvArgRegs.size() && func.hasStackSlot(param.get())) {
                        bool is32 = is32BitType(param->getType());
                        std::string srcReg = is32 ? to32BitReg("%" + sysvArgRegs[pIdx]) : "%" + sysvArgRegs[pIdx];
                        std::string stackOp = formatStackOperand(cg.getStackOffsets()[param.get()]);
                        emitMov(cg, os, srcReg, stackOp, is32);
                    }
                    pIdx++;
                }
            } else {
                auto& as = cg.getAssembler();
                if (layout.stackAlloc > 0) {
                    if (layout.stackAlloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)layout.stackAlloc});
                    else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(layout.stackAlloc); }
                }
            }
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << "  push rbp\n  mov rbp, rsp\n";
            *os << "  push rbx\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n";
        } else {
            auto& as = cg.getAssembler();
            as.emitByte(0x55); as.emitBytes({0x48, 0x89, 0xE5});
            as.emitByte(0x53); as.emitByte(0x56); as.emitByte(0x57);
            as.emitBytes({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
        }
        if (auto* os = cg.getTextStream()) {
            if (layout.stackAlloc > 0) *os << "  sub rsp, " << layout.stackAlloc << "\n";
            int j = 0;
            for (auto& param : func.getParameters()) {
                if (j < 4) {
                    *os << "  mov " << formatStackOperand(cg.getStackOffsets()[param.get()]) << ", " << integerArgRegs[j] << "\n";
                } else {
                    int paramStackOff = 16 + j * 8;
                    *os << "  mov rax, [rbp + " << paramStackOff << "]\n";
                    *os << "  mov " << formatStackOperand(cg.getStackOffsets()[param.get()]) << ", rax\n";
                }
                j++;
            }
        } else {
            auto& as = cg.getAssembler();
            if (layout.stackAlloc > 0) { if (layout.stackAlloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)layout.stackAlloc}); else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(layout.stackAlloc); } }
            int j = 0;
            for (auto& param : func.getParameters()) {
                if (j < 4) {
                    uint8_t r = getArchRegIndex(integerArgRegs[j]);
                    emitRegMem(as, (r >= 8 ? 0x4C : 0x48), 0x89, r & 7, cg.getStackOffsets()[param.get()]);
                } else {
                    uint8_t paramStackOff = (uint8_t)(16 + j * 8);
                    emitRegMem(as, 0x48, 0x8B, 0, paramStackOff);
                    emitRegMem(as, 0x48, 0x89, 0, cg.getStackOffsets()[param.get()]);
                }
                j++;
            }
        }
    }
}

void X64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    X64FrameLayout layout = computeFrameLayout(cg, func);
    if (abi == X64ABI::SystemV) {
        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            if (!layout.usedCalleeRegs.empty()) {
                size_t bytes = layout.usedCalleeRegs.size() * 8;
                *os << "  leaq -" << bytes << "(%rbp), %rsp\n";
                for (auto it = layout.usedCalleeRegs.rbegin(); it != layout.usedCalleeRegs.rend(); ++it) {
                    *os << "  popq %" << *it << "\n";
                }
                *os << "  popq %rbp\n";
            } else if (!layout.isZeroFrame) {
                *os << "  leave\n";
            }
            *os << "  .cfi_def_cfa 7, 8\n";
            *os << "  ret\n";
            *os << "  .cfi_endproc\n";
        } else {
            auto& as = cg.getAssembler();
            CodeGen::SymbolInfo epilogue_sym;
            epilogue_sym.name = func.getName() + "_epilogue";
            epilogue_sym.sectionName = ".text";
            epilogue_sym.value = as.getCodeSize();
            epilogue_sym.type = 0; // STT_NOTYPE
            epilogue_sym.binding = 0; // STB_LOCAL
            cg.addSymbol(epilogue_sym);

            if (!layout.usedCalleeRegs.empty()) {
                size_t bytes = layout.usedCalleeRegs.size() * 8;
                emitRegMem(as, 0x48, 0x8D, 4, -(int32_t)bytes); // leaq -N(%rbp), %rsp
                for (auto it = layout.usedCalleeRegs.rbegin(); it != layout.usedCalleeRegs.rend(); ++it) {
                    uint8_t r = getArchRegIndex(*it);
                    if (r >= 8) as.emitByte(0x41);
                    as.emitByte(0x58 + (r & 7));
                }
                as.emitByte(0x5D); // pop rbp
            } else if (!layout.isZeroFrame) {
                as.emitByte(0xC9); // leave
            }
            as.emitByte(0xC3); // ret
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            *os << "  lea rsp, [rbp - 56]\n";
            *os << "  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbx\n";
            *os << "  leave\n  ret\n";
        } else {
            auto& as = cg.getAssembler();
            CodeGen::SymbolInfo epilogue_sym;
            epilogue_sym.name = func.getName() + "_epilogue";
            epilogue_sym.sectionName = ".text";
            epilogue_sym.value = as.getCodeSize();
            epilogue_sym.type = 0; // STT_NOTYPE
            epilogue_sym.binding = 0; // STB_LOCAL
            cg.addSymbol(epilogue_sym);

            as.emitBytes({0x48, 0x8D, 0x65, 0xC8});
            as.emitBytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C});
            as.emitByte(0x5F); as.emitByte(0x5E); as.emitByte(0x5B);
            as.emitByte(0xC9); as.emitByte(0xC3);
        }
    }
}

void X64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty() && i.getOperands()[0]->get() != nullptr) {
            ir::Value* retVal = i.getOperands()[0]->get();
            bool is32 = is32BitType(retVal->getType());
            std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
            std::string src = cg.getValueAsOperand(retVal);
            if (abi == X64ABI::Windows) {
                *os << "  mov " << rax << ", " << src << "\n";
            } else {
                emitMov(cg, os, src, rax, is32);
            }
        }
        ir::Function* func = i.getParent()->getParent();
        X64FrameLayout layout = computeFrameLayout(cg, *func);
        if (layout.permitsBareReturn()) {
            *os << "  ret\n";
        } else {
            *os << "  jmp " << func->getName() << "_epilogue\n";
        }
    } else {
        if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize();
        cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getParent()->getParent()->getName() + "_epilogue", ".text"});
    }
}

static bool canUseInPlace(CodeGen& cg, ir::Instruction& i, ir::Value* val0) {
    if (!i.hasPhysicalRegister() || !val0 || !val0->hasPhysicalRegister()) {
        return false;
    }
    if (i.getPhysicalRegister() != val0->getPhysicalRegister()) {
        return false;
    }
    if (i.getOperands().empty() || !i.getOperands()[0]) {
        return false;
    }
    return cg.liveness.isLastUseOfOperand(&i, i.getOperands()[0].get());
}

void X64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string rax = is32 ? "%eax" : "%rax";
    std::string addOp = is32 ? "addl" : "addq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(val0) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val0) != nullptr && !dynamic_cast<ir::Function*>(val0));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[1]->get()) != nullptr || 
                         (dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[1]->get()));


        if (abi != X64ABI::Windows && !isGlobal0 && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal1) {
                *os << "  leaq " << op1 << ", %rdx\n  " << addOp << " %rdx, " << d << "\n";
            } else {
                std::string s1 = op1;
                if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
                if (!s1.empty() && s1[0] == '$') {
                    try {
                        uint64_t uv = std::stoull(s1.substr(1));
                        int64_t v = static_cast<int64_t>(uv);
                        if (v > 2147483647LL || v < -2147483648LL || uv > 2147483647ULL) {
                            std::string scratch = (d == "%rax" || d == "%eax") ? "%rdx" : "%rax";
                            *os << "  movabsq " << s1 << ", " << scratch << "\n";
                            s1 = is32 ? to32BitReg(scratch) : scratch;
                        }
                    } catch (...) {}
                }
                *os << "  " << addOp << " " << s1 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        auto* val1 = i.getOperands()[1]->get();
        if (abi != X64ABI::Windows && !isGlobal1 && canUseInPlace(cg, i, val1)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal0) {
                *os << "  leaq " << op0 << ", %rdx\n  " << addOp << " %rdx, " << d << "\n";
            } else {
                std::string s0 = op0;
                if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
                if (!s0.empty() && s0[0] == '$') {
                    try {
                        int64_t v = std::stoll(s0.substr(1));
                        if (v > 2147483647LL || v < -2147483648LL) {
                            std::string scratch = (d == "%rax" || d == "%eax") ? "%rdx" : "%rax";
                            *os << "  movabsq " << s0 << ", " << scratch << "\n";
                            s0 = is32 ? to32BitReg(scratch) : scratch;
                        }
                    } catch (...) {}
                }
                *os << "  " << addOp << " " << s0 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        bool isStackDst = !isDirectGprRegister(dst);
        std::string rax = (abi == X64ABI::Windows) ? (is32 ? "eax" : "rax") : (is32 ? "%eax" : "%rax");
        std::string d = isStackDst ? rax : (is32 ? to32BitReg(dst) : to64BitReg(dst));
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        // 3-operand LEA optimization for non-destructive constant additions (dst = val + C where dst != val)
        if (abi != X64ABI::Windows && !isGlobal0 && !isGlobal1) {
            auto tryEmitLeaConstAdd = [&](const std::string& regOp, const std::string& immOp) -> bool {
                if (!immOp.empty() && immOp[0] == '$' && isDirectGprRegister(regOp)) {
                    try {
                        int64_t v = std::stoll(immOp.substr(1));
                        if (v >= -2147483648LL && v <= 2147483647LL) {
                            std::string baseReg = to64BitReg(regOp);
                            std::string targetReg = isStackDst ? rax : d;
                            if (is32) targetReg = to32BitReg(targetReg);
                            std::string leaInst = is32 ? "leal" : "leaq";
                            std::string disp = (v != 0) ? std::to_string(v) : "";
                            *os << "  " << leaInst << " " << disp << "(" << baseReg << "), " << targetReg << "\n";
                            if (isStackDst) {
                                emitMov(cg, os, rax, dst, is32);
                            }
                            cg.lastStoreOp = "";
                            return true;
                        }
                    } catch (...) {}
                }
                return false;
            };

            std::string d64 = to64BitReg(d);
            std::string s0_64 = to64BitReg(s0);
            std::string s1_64 = to64BitReg(s1);

            if (d64 != s0_64 && tryEmitLeaConstAdd(s0, s1)) return;
            if (d64 != s1_64 && tryEmitLeaConstAdd(s1, s0)) return;
        }

        if (abi == X64ABI::Windows) {
            if (d == s1 && d != s0) {
                // Commute: dst = src2 + src1
                if (isGlobal1) *os << "  lea " << d << ", " << op1 << "\n";
                else *os << "  mov " << d << ", " << op1 << "\n";

                if (isGlobal0) *os << "  lea rdx, " << op0 << "\n  add " << d << ", rdx\n";
                else *os << "  add " << d << ", " << op0 << "\n";
            } else {
                // Direct: dst = src1 + src2
                if (isGlobal0) *os << "  lea " << d << ", " << op0 << "\n";
                else *os << "  mov " << d << ", " << op0 << "\n";

                if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  add " << d << ", rdx\n";
                else *os << "  add " << d << ", " << op1 << "\n";
            }
            if (isStackDst) {
                *os << "  mov " << dst << ", " << rax << "\n";
            }
        } else {
            if (d == s1 && d != s0) {
                // Commute: dst = src2 + src1
                if (isGlobal1) *os << "  leaq " << op1 << ", " << (isStackDst ? rax : d) << "\n";
                else emitMov(cg, os, op1, d, is32);

                if (isGlobal0) *os << "  leaq " << op0 << ", %rdx\n  " << addOp << " %rdx, " << d << "\n";
                else {
                    if (!s0.empty() && s0[0] == '$') {
                        try {
                            int64_t v = std::stoll(s0.substr(1));
                            if (v > 2147483647LL || v < -2147483648LL) {
                                std::string scratch = (d == "%rax" || d == "%eax") ? "%rdx" : "%rax";
                                *os << "  movabsq " << s0 << ", " << scratch << "\n";
                                s0 = is32 ? to32BitReg(scratch) : scratch;
                            }
                        } catch (...) {}
                    }
                    *os << "  " << addOp << " " << s0 << ", " << d << "\n";
                }
            } else {
                // Direct: dst = src1 + src2
                if (isGlobal0) *os << "  leaq " << op0 << ", " << (isStackDst ? rax : d) << "\n";
                else emitMov(cg, os, op0, d, is32);

                if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  " << addOp << " %rdx, " << d << "\n";
                else {
                    if (!s1.empty() && s1[0] == '$') {
                        try {
                            int64_t v = std::stoll(s1.substr(1));
                            if (v > 2147483647LL || v < -2147483648LL) {
                                *os << "  movabsq " << s1 << ", " << d << "\n";
                                *os << "  " << addOp << " " << s0 << ", " << d << "\n";
                                if (isStackDst) emitMov(cg, os, rax, dst, is32);
                                cg.lastStoreOp = "";
                                return;
                            }
                        } catch (...) {}
                    }
                    *os << "  " << addOp << " " << s1 << ", " << d << "\n";
                }
            }
            if (isStackDst) {
                emitMov(cg, os, rax, dst, is32);
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

static void emitSignedMinMaxText(X64Architecture&, CodeGen& cg,
                                 ir::Instruction& i, bool isMin) {
    auto* os = cg.getTextStream();
    if (!os) return;
    bool is32 = i.getType()->getSize() == 4;
    std::string lhs = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string rhs = cg.getValueAsOperand(i.getOperands()[1]->get());
    std::string dst = cg.getValueAsOperand(&i);
    if (dst == rhs && dst != lhs) {
        // Keep the coalesced RHS in place.  Comparing dst against lhs and
        // reversing the candidate avoids borrowing an untracked scratch reg.
        *os << "  " << (is32 ? "cmpl" : "cmpq") << " " << lhs << ", " << dst << "\n";
        *os << "  " << (isMin ? "cmovg" : "cmovl") << " " << lhs << ", " << dst << "\n";
        return;
    }
    if (dst != lhs) *os << "  " << (is32 ? "movl" : "movq") << " " << lhs << ", " << dst << "\n";
    *os << "  " << (is32 ? "cmpl" : "cmpq") << " " << rhs << ", " << dst << "\n";
    *os << "  " << (isMin ? "cmovg" : "cmovl") << " " << rhs << ", " << dst << "\n";
}

void X64Architecture::emitSMin(CodeGen& cg, ir::Instruction& i) { emitSignedMinMaxText(*this, cg, i, true); }
void X64Architecture::emitSMax(CodeGen& cg, ir::Instruction& i) { emitSignedMinMaxText(*this, cg, i, false); }

void X64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string rax = is32 ? "%eax" : "%rax";
    std::string subOp = is32 ? "subl" : "subq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        if (abi != X64ABI::Windows && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            std::string s1 = op1;
            if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
            *os << "  " << subOp << " " << s1 << ", " << d << "\n";
            cg.lastStoreOp = "";
            return;
        }

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        if (d != s1) {
            // Safe direct lowering: dst = src1 - src2
            if (abi == X64ABI::Windows) {
                *os << "  mov " << d << ", " << op0 << "\n";
                *os << "  sub " << d << ", " << op1 << "\n";
            } else {
                emitMov(cg, os, op0, d, is32);
                std::string realS1 = s1;
                if (!isDirectGprRegister(d) && !isDirectGprRegister(s1)) {
                    std::string scratch = is32 ? "%r11d" : "%r11";
                    emitMov(cg, os, s1, scratch, is32);
                    realS1 = scratch;
                }
                *os << "  " << subOp << " " << realS1 << ", " << d << "\n";
            }
        } else {
            // Fallback for SUB when dst == src2 (non-commutative)
            if (abi == X64ABI::Windows) {
                *os << "  mov " << rax << ", " << op0 << "\n";
                *os << "  sub " << rax << ", " << op1 << "\n";
                *os << "  mov " << dst << ", " << rax << "\n";
            } else {
                emitMov(cg, os, op0, rax, is32);
                *os << "  " << subOp << " " << s1 << ", " << rax << "\n";
                emitMov(cg, os, rax, dst, is32);
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x29, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string rax = is32 ? "%eax" : "%rax";
    std::string mulOp = is32 ? "imull" : "imulq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        if (abi != X64ABI::Windows && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            std::string s1 = op1;
            if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
            *os << "  " << mulOp << " " << s1 << ", " << d << "\n";
            cg.lastStoreOp = "";
            return;
        }

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        if (abi == X64ABI::Windows) {
            if (d == s1 && d != s0) {
                // Commute: dst = src2 * src1
                *os << "  mov " << d << ", " << op1 << "\n";
                *os << "  imul " << d << ", " << op0 << "\n";
            } else {
                // Direct: dst = src1 * src2
                *os << "  mov " << d << ", " << op0 << "\n";
                *os << "  imul " << d << ", " << op1 << "\n";
            }
        } else {
            if (!isDirectGprRegister(d)) {
                std::string reg = is32 ? "%eax" : "%rax";
                emitMov(cg, os, op0, reg, is32);
                std::string realS1 = s1;
                if (!isDirectGprRegister(s1)) {
                    std::string scratch = is32 ? "%r11d" : "%r11";
                    emitMov(cg, os, s1, scratch, is32);
                    realS1 = scratch;
                }
                *os << "  " << mulOp << " " << realS1 << ", " << (is32 ? "%eax" : "%rax") << "\n";
                emitMov(cg, os, (is32 ? "%eax" : "%rax"), d, is32);
            } else if (d == s1 && d != s0) {
                // Commute: dst = src2 * src1
                emitMov(cg, os, op1, d, is32);
                *os << "  " << mulOp << " " << s0 << ", " << d << "\n";
            } else {
                // Direct: dst = src1 * src2
                emitMov(cg, os, op0, d, is32);
                *os << "  " << mulOp << " " << s1 << ", " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string idivOp = is32 ? "idivl" : "idivq";

    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[0]->get()) != nullptr || 
                         (dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[0]->get()));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[1]->get()) != nullptr || 
                         (dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[1]->get()));
        if (abi == X64ABI::Windows) {
            *os << "  push rcx\n  push rdx\n  push rax\n";
            if (isGlobal0) *os << "  lea rax, " << op0 << "\n";
            else *os << "  mov rax, " << op0 << "\n";
            if (is32) *os << "  cdq\n"; else *os << "  cqo\n";
            if (isGlobal1) *os << "  lea rcx, " << op1 << "\n";
            else *os << "  mov rcx, " << op1 << "\n";
            *os << "  idiv rcx\n";
            *os << "  mov [rsp], rax\n"; // Store quotient into pushed rax slot on stack
            *os << "  pop rax\n  pop rdx\n  pop rcx\n";
            *os << "  mov " << dst << ", rax\n";
        } else {
            std::string raxReg = is32 ? "%eax" : "%rax";
            std::string rcxReg = is32 ? "%ecx" : "%rcx";
            *os << "  pushq %rax\n  pushq %rcx\n  pushq %rdx\n";
            if (isGlobal0) *os << "  leaq " << op0 << ", " << raxReg << "\n";
            else emitMov(cg, os, op0, raxReg, is32);
            if (is32) *os << "  cltd\n"; else *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcxReg << "\n";
            else emitMov(cg, os, op1, rcxReg, is32);
            *os << "  " << idivOp << " " << rcxReg << "\n";
            *os << "  movq %rax, 16(%rsp)\n"; // Store quotient into pushed rax slot on stack
            *os << "  popq %rdx\n  popq %rcx\n  popq %rax\n";
            emitMov(cg, os, raxReg, dst, is32);
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string idivOp = is32 ? "idivl" : "idivq";

    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[0]->get()) != nullptr || 
                         (dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[0]->get()));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[1]->get()) != nullptr || 
                         (dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[1]->get()));
        if (abi == X64ABI::Windows) {
            *os << "  push rcx\n  push rdx\n  push rax\n";
            if (isGlobal0) *os << "  lea rax, " << op0 << "\n";
            else *os << "  mov rax, " << op0 << "\n";
            if (is32) *os << "  cdq\n"; else *os << "  cqo\n";
            if (isGlobal1) *os << "  lea rcx, " << op1 << "\n";
            else *os << "  mov rcx, " << op1 << "\n";
            *os << "  idiv rcx\n";
            *os << "  mov [rsp], rdx\n"; // Store remainder into pushed rax slot on stack
            *os << "  pop rax\n  pop rdx\n  pop rcx\n";
            *os << "  mov " << dst << ", rax\n";
        } else {
            std::string raxReg = is32 ? "%eax" : "%rax";
            std::string rcxReg = is32 ? "%ecx" : "%rcx";
            *os << "  pushq %rax\n  pushq %rcx\n  pushq %rdx\n";
            if (isGlobal0) *os << "  leaq " << op0 << ", " << raxReg << "\n";
            else emitMov(cg, os, op0, raxReg, is32);
            if (is32) *os << "  cltd\n"; else *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcxReg << "\n";
            else emitMov(cg, os, op1, rcxReg, is32);
            *os << "  " << idivOp << " " << rcxReg << "\n";
            *os << "  movq %rdx, 16(%rsp)\n"; // Store remainder into pushed rax slot on stack
            *os << "  popq %rdx\n  popq %rcx\n  popq %rax\n";
            emitMov(cg, os, raxReg, dst, is32);
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 2);
    }
}

void X64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string andOp = is32 ? "andl" : "andq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto* val1 = i.getOperands()[1]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(val1);
        auto dst = cg.getValueAsOperand(&i);

        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(val0) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val0) != nullptr && !dynamic_cast<ir::Function*>(val0));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(val1) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val1) != nullptr && !dynamic_cast<ir::Function*>(val1));

        if (!isGlobal0 && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal1) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op1 << (abi == X64ABI::Windows ? "\n  and " : ", %rdx\n  andl %rdx, ") << d << "\n";
            } else {
                std::string s1 = op1;
                if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
                if (abi == X64ABI::Windows) *os << "  and " << d << ", " << op1 << "\n";
                else *os << "  " << andOp << " " << s1 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        if (!isGlobal1 && canUseInPlace(cg, i, val1)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal0) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op0 << (abi == X64ABI::Windows ? "\n  and " : ", %rdx\n  andl %rdx, ") << d << "\n";
            } else {
                std::string s0 = op0;
                if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
                if (abi == X64ABI::Windows) *os << "  and " << d << ", " << op0 << "\n";
                else *os << "  " << andOp << " " << s0 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        if (abi == X64ABI::Windows) {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  lea " << d << ", " << op1 << "\n";
                else *os << "  mov " << d << ", " << op1 << "\n";
                if (isGlobal0) *os << "  lea rdx, " << op0 << "\n  and " << d << ", rdx\n";
                else *os << "  and " << d << ", " << op0 << "\n";
            } else {
                if (isGlobal0) *os << "  lea " << d << ", " << op0 << "\n";
                else *os << "  mov " << d << ", " << op0 << "\n";
                if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  and " << d << ", rdx\n";
                else *os << "  and " << d << ", " << op1 << "\n";
            }
        } else {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  leaq " << op1 << ", " << d << "\n";
                else emitMov(cg, os, op1, d, is32);
                if (isGlobal0) *os << "  leaq " << op0 << ", %rdx\n  " << andOp << " %rdx, " << d << "\n";
                else *os << "  " << andOp << " " << s0 << ", " << d << "\n";
            } else {
                if (isGlobal0) *os << "  leaq " << op0 << ", " << d << "\n";
                else emitMov(cg, os, op0, d, is32);
                if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  " << andOp << " %rdx, " << d << "\n";
                else *os << "  " << andOp << " " << s1 << ", " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x21, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string orOp = is32 ? "orl" : "orq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto* val1 = i.getOperands()[1]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(val1);
        auto dst = cg.getValueAsOperand(&i);

        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(val0) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val0) != nullptr && !dynamic_cast<ir::Function*>(val0));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(val1) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val1) != nullptr && !dynamic_cast<ir::Function*>(val1));

        if (!isGlobal0 && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal1) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op1 << (abi == X64ABI::Windows ? "\n  or " : ", %rdx\n  orl %rdx, ") << d << "\n";
            } else {
                std::string s1 = op1;
                if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
                if (abi == X64ABI::Windows) *os << "  or " << d << ", " << op1 << "\n";
                else *os << "  " << orOp << " " << s1 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        if (!isGlobal1 && canUseInPlace(cg, i, val1)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal0) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op0 << (abi == X64ABI::Windows ? "\n  or " : ", %rdx\n  orl %rdx, ") << d << "\n";
            } else {
                std::string s0 = op0;
                if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
                if (abi == X64ABI::Windows) *os << "  or " << d << ", " << op0 << "\n";
                else *os << "  " << orOp << " " << s0 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        if (abi == X64ABI::Windows) {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  lea " << d << ", " << op1 << "\n";
                else *os << "  mov " << d << ", " << op1 << "\n";
                if (isGlobal0) *os << "  lea rdx, " << op0 << "\n  or " << d << ", rdx\n";
                else *os << "  or " << d << ", " << op0 << "\n";
            } else {
                if (isGlobal0) *os << "  lea " << d << ", " << op0 << "\n";
                else *os << "  mov " << d << ", " << op0 << "\n";
                if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  or " << d << ", rdx\n";
                else *os << "  or " << d << ", " << op1 << "\n";
            }
        } else {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  leaq " << op1 << ", " << d << "\n";
                else emitMov(cg, os, op1, d, is32);
                if (isGlobal0) *os << "  leaq " << op0 << ", %rdx\n  " << orOp << " %rdx, " << d << "\n";
                else *os << "  " << orOp << " " << s0 << ", " << d << "\n";
            } else {
                if (isGlobal0) *os << "  leaq " << op0 << ", " << d << "\n";
                else emitMov(cg, os, op0, d, is32);
                if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  " << orOp << " %rdx, " << d << "\n";
                else *os << "  " << orOp << " " << s1 << ", " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x09, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string xorOp = is32 ? "xorl" : "xorq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto* val1 = i.getOperands()[1]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(val1);
        auto dst = cg.getValueAsOperand(&i);

        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(val0) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val0) != nullptr && !dynamic_cast<ir::Function*>(val0));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(val1) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(val1) != nullptr && !dynamic_cast<ir::Function*>(val1));

        if (!isGlobal0 && canUseInPlace(cg, i, val0)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal1) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op1 << (abi == X64ABI::Windows ? "\n  xor " : ", %rdx\n  xorl %rdx, ") << d << "\n";
            } else {
                std::string s1 = op1;
                if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);
                if (abi == X64ABI::Windows) *os << "  xor " << d << ", " << op1 << "\n";
                else *os << "  " << xorOp << " " << s1 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        if (!isGlobal1 && canUseInPlace(cg, i, val1)) {
            std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
            if (isGlobal0) {
                *os << (abi == X64ABI::Windows ? "  lea rdx, " : "  leaq ") << op0 << (abi == X64ABI::Windows ? "\n  xor " : ", %rdx\n  xorl %rdx, ") << d << "\n";
            } else {
                std::string s0 = op0;
                if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
                if (abi == X64ABI::Windows) *os << "  xor " << d << ", " << op0 << "\n";
                else *os << "  " << xorOp << " " << s0 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        std::string s1 = op1;
        if (!s1.empty() && s1[0] == '%') s1 = is32 ? to32BitReg(s1) : to64BitReg(s1);

        if (abi == X64ABI::Windows) {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  lea " << d << ", " << op1 << "\n";
                else *os << "  mov " << d << ", " << op1 << "\n";
                if (isGlobal0) *os << "  lea rdx, " << op0 << "\n  xor " << d << ", rdx\n";
                else *os << "  xor " << d << ", " << op0 << "\n";
            } else {
                if (isGlobal0) *os << "  lea " << d << ", " << op0 << "\n";
                else *os << "  mov " << d << ", " << op0 << "\n";
                if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  xor " << d << ", rdx\n";
                else *os << "  xor " << d << ", " << op1 << "\n";
            }
        } else {
            if (d == s1 && d != s0) {
                if (isGlobal1) *os << "  leaq " << op1 << ", " << d << "\n";
                else emitMov(cg, os, op1, d, is32);
                if (isGlobal0) *os << "  leaq " << op0 << ", %rdx\n  " << xorOp << " %rdx, " << d << "\n";
                else *os << "  " << xorOp << " " << s0 << ", " << d << "\n";
            } else {
                if (isGlobal0) *os << "  leaq " << op0 << ", " << d << "\n";
                else emitMov(cg, os, op0, d, is32);
                if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  " << xorOp << " %rdx, " << d << "\n";
                else *os << "  " << xorOp << " " << s1 << ", " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x31, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getOperands()[0]->get()->getType());
    std::string shiftOp = is32 ? "shll" : "shlq";
    std::string rcx = (abi == X64ABI::SystemV) ? (is32 ? "%ecx" : "%rcx") : (is32 ? "ecx" : "rcx");
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get());

        if (abi == X64ABI::Windows) {
            if (constantCount) {
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  shl " << d << ", " << count << "\n";
            } else {
                *os << "  mov " << rcx << ", " << op1 << "\n";
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                *os << "  shl " << d << ", cl\n";
            }
        } else {
            if (constantCount) {
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  " << shiftOp << " $" << count << ", " << d << "\n";
            } else {
                emitMov(cg, os, op1, rcx, is32);
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                *os << "  " << shiftOp << " %cl, " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        if (auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
            uint8_t count = static_cast<uint8_t>(constantCount->getValue() & (is32 ? 31ULL : 63ULL));
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xC1, 0xE0, count});
        } else {
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xD3, 0xE0});
        }
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getOperands()[0]->get()->getType());
    std::string shiftOp = is32 ? "shrl" : "shrq";
    std::string rcx = (abi == X64ABI::SystemV) ? (is32 ? "%ecx" : "%rcx") : (is32 ? "ecx" : "rcx");
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get());

        if (abi == X64ABI::Windows) {
            if (constantCount) {
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  shr " << d << ", " << count << "\n";
            } else {
                *os << "  mov " << rcx << ", " << op1 << "\n";
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                *os << "  shr " << d << ", cl\n";
            }
        } else {
            if (constantCount) {
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  " << shiftOp << " $" << count << ", " << d << "\n";
            } else {
                emitMov(cg, os, op1, rcx, is32);
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                *os << "  " << shiftOp << " %cl, " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        if (auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
            uint8_t count = static_cast<uint8_t>(constantCount->getValue() & (is32 ? 31ULL : 63ULL));
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xC1, 0xE8, count});
        } else {
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xD3, 0xE8});
        }
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getOperands()[0]->get()->getType());
    std::string shiftOp = is32 ? "sarl" : "sarq";
    std::string rcx = (abi == X64ABI::SystemV) ? (is32 ? "%ecx" : "%rcx") : (is32 ? "ecx" : "rcx");
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);
        auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get());

        if (abi == X64ABI::Windows) {
            if (constantCount) {
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  sar " << d << ", " << count << "\n";
            } else {
                *os << "  mov " << rcx << ", " << op1 << "\n";
                if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
                *os << "  sar " << d << ", cl\n";
            }
        } else {
            if (constantCount) {
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                uint64_t count = constantCount->getValue() & (is32 ? 31ULL : 63ULL);
                *os << "  " << shiftOp << " $" << count << ", " << d << "\n";
            } else {
                emitMov(cg, os, op1, rcx, is32);
                if (s0 != d) emitMov(cg, os, s0, d, is32);
                *os << "  " << shiftOp << " %cl, " << d << "\n";
            }
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        if (auto* constantCount = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
            uint8_t count = static_cast<uint8_t>(constantCount->getValue() & (is32 ? 31ULL : 63ULL));
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xC1, 0xF8, count});
        } else {
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
            if (!is32) cg.getAssembler().emitByte(0x48);
            cg.getAssembler().emitBytes({0xD3, 0xF8});
        }
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string negOp = is32 ? "negl" : "negq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto dst = cg.getValueAsOperand(&i);

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);

        if (abi == X64ABI::Windows) {
            if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
            *os << "  neg " << d << "\n";
        } else {
            if (s0 != d) emitMov(cg, os, s0, d, is32);
            *os << "  " << negOp << " " << d << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xD8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getType());
    std::string notOp = is32 ? "notl" : "notq";
    if (auto* os = cg.getTextStream()) {
        auto* val0 = i.getOperands()[0]->get();
        auto op0 = cg.getValueAsOperand(val0);
        auto dst = cg.getValueAsOperand(&i);

        std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
        std::string s0 = op0;
        if (!s0.empty() && s0[0] == '%') s0 = is32 ? to32BitReg(s0) : to64BitReg(s0);

        if (abi == X64ABI::Windows) {
            if (s0 != d) *os << "  mov " << d << ", " << s0 << "\n";
            *os << "  not " << d << "\n";
        } else {
            if (s0 != d) emitMov(cg, os, s0, d, is32);
            *os << "  " << notOp << " " << d << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xD0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    std::string srcOp = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string destOp = cg.getValueAsOperand(&i);
    if (srcOp == destOp) return; // Move elimination (self-move)

    bool isVector = i.getType() && (i.getType()->isVectorTy() || i.getType()->isSIMDType() || dynamic_cast<const ir::VectorType*>(i.getType()) != nullptr);
    if (isVector) {
        auto* vecType = dynamic_cast<const ir::VectorType*>(i.getType());
        unsigned totalBits = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;
        std::string movInst = (totalBits == 256) ? "vmovdqu" : "movdqu";
        std::string src = (totalBits == 256) ? toYmmReg(srcOp) : srcOp;
        std::string dest = (totalBits == 256) ? toYmmReg(destOp) : destOp;
        if (auto* os = cg.getTextStream()) {
            *os << "  " << movInst << " " << src << ", " << dest << "\n";
        }
        return;
    }

    bool is32 = is32BitType(i.getType());
    std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
    std::string movOp = is32 ? "movl" : "movq";

    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << srcOp << "\n";
            *os << "  mov " << destOp << ", " << rax << "\n";
        } else {
            emitMov(cg, os, srcOp, rax, is32);
            emitMov(cg, os, rax, destOp, is32);
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    ir::Value* calleeVal = i.getOperands()[0]->get();
    bool isDirectCall = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));

    if (auto* os = cg.getTextStream()) {
        size_t systemVStackBytes = 0;
        if (abi == X64ABI::Windows) {
            for (size_t j = 1; j < i.getOperands().size(); ++j) {
                ir::Value* argVal = i.getOperands()[j]->get();
                size_t paramIdx = j - 1;
                bool isFloat = argVal->getType() && (argVal->getType()->isFloatTy() || argVal->getType()->isDoubleTy());
                bool argIsGlobal = dynamic_cast<ir::GlobalVariable*>(argVal) != nullptr ||
                                   (dynamic_cast<ir::GlobalValue*>(argVal) != nullptr && !dynamic_cast<ir::Function*>(argVal));
                std::string argOp = cg.getValueAsOperand(argVal);

                if (paramIdx < 4) {
                    if (isFloat) {
                        std::string reg = floatArgRegs[paramIdx];
                        *os << "  movsd " << reg << ", " << argOp << "\n";
                    } else {
                        std::string reg = getRegisterName(integerArgRegs[paramIdx], argVal->getType());
                        if (argIsGlobal) {
                            *os << "  lea " << reg << ", " << argOp << "\n";
                        } else {
                            *os << "  mov " << reg << ", " << argOp << "\n";
                        }
                    }
                } else {
                    size_t stackOff = paramIdx * 8;
                    if (argIsGlobal) {
                        *os << "  lea rax, " << argOp << "\n";
                        *os << "  mov [rsp + " << stackOff << "], rax\n";
                    } else {
                        *os << "  mov rax, " << argOp << "\n";
                        *os << "  mov [rsp + " << stackOff << "], rax\n";
                    }
                }
            }
        } else {
            size_t int_idx = 0, float_idx = 0;
            struct RegisterArgument {
                std::string destination;
                std::string source;
                bool isGlobal;
            };
            std::vector<RegisterArgument> integerArgs;
            std::vector<std::pair<std::string, bool>> stackArgs;
            auto pushArgument = [&](const std::pair<std::string, bool>& arg) {
                const auto& [source, isGlobal] = arg;
                if (isGlobal) {
                    *os << "  leaq " << source << ", %rax\n  pushq %rax\n";
                } else if (!source.empty() && source[0] == '%') {
                    *os << "  pushq " << to64BitReg(source) << "\n";
                } else if (!source.empty() && source[0] == '$') {
                    // pushq only accepts a sign-extended 32-bit immediate.
                    *os << "  movabsq " << source << ", %rax\n  pushq %rax\n";
                } else {
                    *os << "  pushq " << source << "\n";
                }
            };
            for (size_t j = 1; j < i.getOperands().size(); ++j) {
                ir::Value* argVal = i.getOperands()[j]->get();
                bool isFloat = argVal->getType() && (argVal->getType()->isFloatTy() || argVal->getType()->isDoubleTy());
                bool argIsGlobal = dynamic_cast<ir::GlobalVariable*>(argVal) != nullptr ||
                                   (dynamic_cast<ir::GlobalValue*>(argVal) != nullptr && !dynamic_cast<ir::Function*>(argVal));
                if (isFloat) {
                    if (float_idx < floatArgRegs.size()) {
                        std::string reg = floatArgRegs[float_idx++];
                        *os << "  movsd " << cg.getValueAsOperand(argVal) << ", %" << reg << "\n";
                    }
                } else {
                    if (int_idx < 6) {
                        std::string reg = getRegisterName(integerArgRegs[int_idx++], argVal->getType());
                        integerArgs.push_back({reg, cg.getValueAsOperand(argVal), argIsGlobal});
                    } else {
                        stackArgs.push_back({cg.getValueAsOperand(argVal), argIsGlobal});
                    }
                }
            }
            // System V passes integer arguments after the sixth on the stack,
            // right-to-left. Keep the call-site stack 16-byte aligned; padding
            // precedes the arguments so the first stack argument remains at
            // 8(%rsp) on callee entry.
            if (stackArgs.size() % 2 != 0) {
                *os << "  subq $8, %rsp\n";
                systemVStackBytes += 8;
            }
            for (auto it = stackArgs.rbegin(); it != stackArgs.rend(); ++it) {
                pushArgument(*it);
                systemVStackBytes += 8;
            }

            // Calls require a parallel copy into ABI argument registers. Save
            // every source only after the stack arguments are in place, then
            // pop in reverse destination order. This also preserves stack-arg
            // sources which happen to reside in an ABI argument register.
            for (const auto& arg : integerArgs)
                pushArgument({arg.source, arg.isGlobal});
            for (auto it = integerArgs.rbegin(); it != integerArgs.rend(); ++it)
                *os << "  popq " << to64BitReg(it->destination) << "\n";
        }
        if (isDirectCall) {
            *os << "  call " << calleeVal->getName() << "\n";
        } else {
            if (abi == X64ABI::Windows) {
                *os << "  mov rax, " << cg.getValueAsOperand(calleeVal) << "\n";
                *os << "  call rax\n";
            } else {
                *os << "  movq " << cg.getValueAsOperand(calleeVal) << ", %rax\n";
                *os << "  call *%rax\n";
            }
        }
        if (systemVStackBytes != 0)
            *os << "  addq $" << systemVStackBytes << ", %rsp\n";
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) {
            bool resultIs32 = is32BitType(i.getType());
            if (abi == X64ABI::Windows) {
                std::string destination = resultIs32 ? to32BitReg(cg.getValueAsOperand(&i)) : to64BitReg(cg.getValueAsOperand(&i));
                *os << "  mov " << destination << ", " << (resultIs32 ? "eax" : "rax") << "\n";
            } else {
                std::string destination = resultIs32 ? to32BitReg(cg.getValueAsOperand(&i)) : to64BitReg(cg.getValueAsOperand(&i));
                *os << "  " << (resultIs32 ? "movl %eax, " : "movq %rax, ") << destination << "\n";
            }
        }
    } else {
        size_t maxArgs = (abi == X64ABI::SystemV) ? 6 : 4;
        for (size_t j = 1; j < i.getOperands().size(); ++j) {
            if (j <= maxArgs) {
                uint8_t r = getArchRegIndex(integerArgRegs[j-1]);
                emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), r);
            } else if (abi == X64ABI::Windows) {
                emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), 0); // Load to RAX
                uint8_t offset = (uint8_t)((j - 1) * 8);
                cg.getAssembler().emitBytes({0x48, 0x89, 0x44, 0x24, offset}); // mov [rsp + offset], rax
            }
        }
        if (isDirectCall) {
            cg.getAssembler().emitByte(0xE8);
            uint64_t off = cg.getAssembler().getCodeSize();
            cg.getAssembler().emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, calleeVal->getName(), ".text"});
        } else {
            emitLoadValue(cg, cg.getAssembler(), calleeVal, 0); // RAX
            cg.getAssembler().emitBytes({0xFF, 0xD0});         // call rax
        }
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) emitStoreResult(cg, i, 0);
    }
}

bool X64Architecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) {
    ir::Value* calleeVal = callInst.getOperands()[0]->get();
    bool isDirectCall = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
    if (!isDirectCall) return false;

    // Safety Predicate: Check return value consumption
    if (callInst.getType()->getTypeID() != ir::Type::VoidTyID) {
        if (retInst.getOperands().empty() || retInst.getOperands()[0]->get() != &callInst) {
            return false;
        }
    } else {
        if (!retInst.getOperands().empty()) return false;
    }

    // Safety Predicate: No stack-passed arguments
    size_t numArgs = callInst.getOperands().size() - 1;
    size_t maxArgs = (abi == X64ABI::SystemV) ? 6 : 4;
    if (numArgs > maxArgs) return false;

    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            for (size_t j = 1; j < callInst.getOperands().size(); ++j) {
                ir::Value* argVal = callInst.getOperands()[j]->get();
                size_t paramIdx = j - 1;
                bool isFloat = argVal->getType() && (argVal->getType()->isFloatTy() || argVal->getType()->isDoubleTy());
                bool argIsGlobal = dynamic_cast<ir::GlobalVariable*>(argVal) != nullptr ||
                                   (dynamic_cast<ir::GlobalValue*>(argVal) != nullptr && !dynamic_cast<ir::Function*>(argVal));
                std::string argOp = cg.getValueAsOperand(argVal);

                if (paramIdx < 4) {
                    if (isFloat) {
                        std::string reg = floatArgRegs[paramIdx];
                        *os << "  movsd " << reg << ", " << argOp << "\n";
                    } else {
                        std::string reg = getRegisterName(integerArgRegs[paramIdx], argVal->getType());
                        if (argIsGlobal) {
                            *os << "  lea " << reg << ", " << argOp << "\n";
                        } else {
                            *os << "  mov " << reg << ", " << argOp << "\n";
                        }
                    }
                }
            }
        } else {
            size_t int_idx = 0, float_idx = 0;
            for (size_t j = 1; j < callInst.getOperands().size(); ++j) {
                ir::Value* argVal = callInst.getOperands()[j]->get();
                bool isFloat = argVal->getType() && (argVal->getType()->isFloatTy() || argVal->getType()->isDoubleTy());
                bool argIsGlobal = dynamic_cast<ir::GlobalVariable*>(argVal) != nullptr ||
                                   (dynamic_cast<ir::GlobalValue*>(argVal) != nullptr && !dynamic_cast<ir::Function*>(argVal));
                if (isFloat) {
                    if (float_idx < floatArgRegs.size()) {
                        std::string reg = floatArgRegs[float_idx++];
                        *os << "  movsd " << cg.getValueAsOperand(argVal) << ", %" << reg << "\n";
                    }
                } else {
                    if (int_idx < 6) {
                        bool is32 = is32BitType(argVal->getType());
                        std::string reg = getRegisterName(integerArgRegs[int_idx++], argVal->getType());
                        if (argIsGlobal) {
                            *os << "  leaq " << cg.getValueAsOperand(argVal) << ", " << reg << "\n";
                        } else {
                            emitMov(cg, os, cg.getValueAsOperand(argVal), reg, is32);
                        }
                    }
                }
            }
        }

        // Frame Teardown
        ir::Function* func = callInst.getParent()->getParent();
        X64FrameLayout layout = computeFrameLayout(cg, *func);
        if (abi == X64ABI::SystemV) {
            if (!layout.usedCalleeRegs.empty()) {
                *os << "  jmp " << func->getName() << "_epilogue\n";
                return true;
            }
            if (!layout.isZeroFrame) {
                *os << "  leave\n";
            }
            *os << "  jmp " << calleeVal->getName() << "\n";
        } else {
            *os << "  jmp " << func->getName() << "_epilogue\n";
        }
        return true;
    }
    return false;
}

void X64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        const bool single = i.getType() && i.getType()->isFloatTy();
        const char* moveFP = single ? "movss" : "movsd";
        const char* binaryFP = single ? "addss" : "addsd";
        const std::string fpScratch = getReservedScratchVectorReg();
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  " << moveFP << " " << fpScratch << ", " << op0 << "\n";
            *os << "  " << binaryFP << " " << fpScratch << ", " << op1 << "\n";
            *os << "  " << moveFP << " " << dst << ", " << fpScratch << "\n";
        } else {
            *os << "  " << moveFP << " " << op0 << ", " << fpScratch << "\n";
            *os << "  " << binaryFP << " " << op1 << ", " << fpScratch << "\n";
            *os << "  " << moveFP << " " << fpScratch << ", " << dst << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0); // RAX
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 1); // RCX
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); // movq xmm0, rax
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); // movq xmm1, rcx
        as.emitBytes({0xF2, 0x0F, 0x58, 0xC1});       // addsd xmm0, xmm1
        as.emitBytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); // movq rax, xmm0
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        const bool single = i.getType() && i.getType()->isFloatTy();
        const char* moveFP = single ? "movss" : "movsd";
        const char* binaryFP = single ? "subss" : "subsd";
        const std::string fpScratch = getReservedScratchVectorReg();
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  " << moveFP << " " << fpScratch << ", " << op0 << "\n";
            *os << "  " << binaryFP << " " << fpScratch << ", " << op1 << "\n";
            *os << "  " << moveFP << " " << dst << ", " << fpScratch << "\n";
        } else {
            *os << "  " << moveFP << " " << op0 << ", " << fpScratch << "\n";
            *os << "  " << binaryFP << " " << op1 << ", " << fpScratch << "\n";
            *os << "  " << moveFP << " " << fpScratch << ", " << dst << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 1);
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); // movq xmm0, rax
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); // movq xmm1, rcx
        as.emitBytes({0xF2, 0x0F, 0x5C, 0xC1});       // subsd xmm0, xmm1
        as.emitBytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); // movq rax, xmm0
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        const bool single = i.getType() && i.getType()->isFloatTy();
        const char* moveFP = single ? "movss" : "movsd";
        const char* binaryFP = single ? "mulss" : "mulsd";
        const std::string fpScratch = getReservedScratchVectorReg();
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  " << moveFP << " " << fpScratch << ", " << op0 << "\n";
            *os << "  " << binaryFP << " " << fpScratch << ", " << op1 << "\n";
            *os << "  " << moveFP << " " << dst << ", " << fpScratch << "\n";
        } else {
            *os << "  " << moveFP << " " << op0 << ", " << fpScratch << "\n";
            *os << "  " << binaryFP << " " << op1 << ", " << fpScratch << "\n";
            *os << "  " << moveFP << " " << fpScratch << ", " << dst << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 1);
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); // movq xmm0, rax
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); // movq xmm1, rcx
        as.emitBytes({0xF2, 0x0F, 0x59, 0xC1});       // mulsd xmm0, xmm1
        as.emitBytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); // movq rax, xmm0
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        const bool single = i.getType() && i.getType()->isFloatTy();
        const char* moveFP = single ? "movss" : "movsd";
        const char* binaryFP = single ? "divss" : "divsd";
        const std::string fpScratch = getReservedScratchVectorReg();
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  " << moveFP << " " << fpScratch << ", " << op0 << "\n";
            *os << "  " << binaryFP << " " << fpScratch << ", " << op1 << "\n";
            *os << "  " << moveFP << " " << dst << ", " << fpScratch << "\n";
        } else {
            *os << "  " << moveFP << " " << op0 << ", " << fpScratch << "\n";
            *os << "  " << binaryFP << " " << op1 << ", " << fpScratch << "\n";
            *os << "  " << moveFP << " " << fpScratch << ", " << dst << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 1);
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); // movq xmm0, rax
        as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); // movq xmm1, rcx
        as.emitBytes({0xF2, 0x0F, 0x5E, 0xC1});       // divsd xmm0, xmm1
        as.emitBytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); // movq rax, xmm0
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string al = (abi == X64ABI::SystemV) ? "%al" : "al";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    bool isFloatCmp = (i.getOpcode() == ir::Instruction::Ceqf || i.getOpcode() == ir::Instruction::Cnef ||
                       i.getOpcode() == ir::Instruction::Clt  || i.getOpcode() == ir::Instruction::Cle ||
                       i.getOpcode() == ir::Instruction::Cgt  || i.getOpcode() == ir::Instruction::Cge);
    if (auto* os = cg.getTextStream()) {
        std::string set;
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq:  case ir::Instruction::Ceqf: set = "sete"; break;
            case ir::Instruction::Cne:  case ir::Instruction::Cnef: set = "setne"; break;
            case ir::Instruction::Cslt: set = "setl"; break;
            case ir::Instruction::Cult: case ir::Instruction::Clt: set = "setb"; break;
            case ir::Instruction::Csle: set = "setle"; break;
            case ir::Instruction::Cule: case ir::Instruction::Cle: set = "setbe"; break;
            case ir::Instruction::Csgt: set = "setg"; break;
            case ir::Instruction::Cugt: case ir::Instruction::Cgt: set = "seta"; break;
            case ir::Instruction::Csge: set = "setge"; break;
            case ir::Instruction::Cuge: case ir::Instruction::Cge: set = "setae"; break;
            default:                    set = "sete"; break;
        }
        if (isFloatCmp) {
            const bool single = i.getOperands()[0]->get()->getType()->isFloatTy();
            const char* move = single ? "movss" : "movsd";
            const char* compare = single ? "ucomiss" : "ucomisd";
            if (abi == X64ABI::Windows) {
                *os << "  movsd xmm0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
                *os << "  ucomisd xmm0, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzx " << eax << ", " << al << "\n";
                *os << "  mov " << cg.getValueAsOperand(&i) << ", " << rax << "\n";
            } else {
                *os << "  " << move << " " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %xmm0\n";
                *os << "  " << compare << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %xmm0\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzbq " << al << ", " << rax << "\n";
                *os << "  movl " << eax << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        } else {
            bool is32 = is32BitType(i.getOperands()[0]->get()->getType());
            std::string cmpOp = is32 ? "cmpl" : "cmpq";
            std::string rax = is32 ? "%eax" : "%rax";
            std::string rdx = is32 ? "%edx" : "%rdx";

            bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[0]->get()) != nullptr || 
                             (dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[0]->get()));
            bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[1]->get()) != nullptr || 
                             (dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[1]->get()));
            if (abi == X64ABI::Windows) {
                if (isGlobal0) *os << "  lea " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
                else *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
                if (isGlobal1) *os << "  lea rdx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  cmp " << rax << ", rdx\n";
                else *os << "  cmp " << rax << ", " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzx " << eax << ", " << al << "\n";
                *os << "  mov " << cg.getValueAsOperand(&i) << ", " << rax << "\n";
            } else {
                if (isGlobal0) *os << "  leaq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
                else emitMov(cg, os, cg.getValueAsOperand(i.getOperands()[0]->get()), rax, is32);
                if (isGlobal1) *os << "  leaq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rdx << "\n  " << cmpOp << " " << rdx << ", " << rax << "\n";
                else {
                    std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
                    if (op1[0] == '%') op1 = is32 ? to32BitReg(op1) : to64BitReg(op1);
                    *os << "  " << cmpOp << " " << op1 << ", " << rax << "\n";
                }
                *os << "  " << set << " " << al << "\n";
                *os << "  movzbq " << al << ", %rax\n";
                emitMov(cg, os, "%rax", cg.getValueAsOperand(&i), is32BitType(i.getType()));
            }
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 1);
        if (isFloatCmp) {
            as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC0}); // movq xmm0, rax
            as.emitBytes({0x66, 0x48, 0x0F, 0x6E, 0xC9}); // movq xmm1, rcx
            as.emitBytes({0x66, 0x0F, 0x2E, 0xC1});       // ucomisd xmm0, xmm1
        } else {
            as.emitBytes({0x48, 0x39, 0xC8});            // cmp rax, rcx
        }
        uint8_t s = 0x94;
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq:  case ir::Instruction::Ceqf: s = 0x94; break;
            case ir::Instruction::Cne:  case ir::Instruction::Cnef: s = 0x95; break;
            case ir::Instruction::Cslt: case ir::Instruction::Clt:  s = (isFloatCmp ? 0x92 : 0x9C); break;
            case ir::Instruction::Csle: case ir::Instruction::Cle:  s = (isFloatCmp ? 0x96 : 0x9E); break;
            case ir::Instruction::Csgt: case ir::Instruction::Cgt:  s = (isFloatCmp ? 0x97 : 0x9F); break;
            case ir::Instruction::Csge: case ir::Instruction::Cge:  s = (isFloatCmp ? 0x93 : 0x9D); break;
            case ir::Instruction::Cult: s = 0x92; break;
            case ir::Instruction::Cule: s = 0x96; break;
            case ir::Instruction::Cugt: s = 0x97; break;
            case ir::Instruction::Cuge: s = 0x93; break;
            default:                    s = 0x94; break;
        }
        cg.getAssembler().emitBytes({0x0F, s, 0xC0, 0x48, 0x0F, 0xB6, 0xC0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    std::string srcOp = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string destOp = cg.getValueAsOperand(&i);

    if (auto* os = cg.getTextStream()) {
        ir::Instruction::Opcode op = i.getOpcode();
        if (op == ir::Instruction::Sltof || op == ir::Instruction::SWtoF) {
            if (abi == X64ABI::Windows) {
                *os << "  cvtsi2sd xmm0, " << srcOp << "\n";
                *os << "  movsd " << destOp << ", xmm0\n";
            } else {
                *os << "  cvtsi2sd " << srcOp << ", %xmm0\n";
                *os << "  movsd %xmm0, " << destOp << "\n";
            }
        } else if (op == ir::Instruction::UWtoF) {
            if (abi == X64ABI::Windows) {
                *os << "  mov eax, " << srcOp << "\n";
                *os << "  cvtsi2sd xmm0, rax\n";
                *os << "  movsd " << destOp << ", xmm0\n";
            } else {
                std::string s32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
                *os << "  movl " << s32 << ", %eax\n";
                *os << "  cvtsi2sd %rax, %xmm0\n";
                *os << "  movsd %xmm0, " << destOp << "\n";
            }
        } else if (op == ir::Instruction::Ultof) {
            std::string labelHigh = ".L_ultof_high_" + std::to_string((uintptr_t)&i);
            std::string labelDone = ".L_ultof_done_" + std::to_string((uintptr_t)&i);
            if (abi == X64ABI::Windows) {
                *os << "  mov rax, " << srcOp << "\n";
                *os << "  test rax, rax\n";
                *os << "  js " << labelHigh << "\n";
                *os << "  cvtsi2sd xmm0, rax\n";
                *os << "  jmp " << labelDone << "\n";
                *os << labelHigh << ":\n";
                *os << "  mov rdx, rax\n";
                *os << "  shr rax, 1\n";
                *os << "  and rdx, 1\n";
                *os << "  or rax, rdx\n";
                *os << "  cvtsi2sd xmm0, rax\n";
                *os << "  addsd xmm0, xmm0\n";
                *os << labelDone << ":\n";
                *os << "  movsd " << destOp << ", xmm0\n";
            } else {
                *os << "  movq " << srcOp << ", %rax\n";
                *os << "  testq %rax, %rax\n";
                *os << "  js " << labelHigh << "\n";
                *os << "  cvtsi2sd %rax, %xmm0\n";
                *os << "  jmp " << labelDone << "\n";
                *os << labelHigh << ":\n";
                *os << "  movq %rax, %rdx\n";
                *os << "  shrq $1, %rax\n";
                *os << "  andq $1, %rdx\n";
                *os << "  orq %rdx, %rax\n";
                *os << "  cvtsi2sd %rax, %xmm0\n";
                *os << "  addsd %xmm0, %xmm0\n";
                *os << labelDone << ":\n";
                *os << "  movsd %xmm0, " << destOp << "\n";
            }
        } else if (op == ir::Instruction::DToUI || op == ir::Instruction::SToUI) {
            std::string labelHigh = ".L_dtoui_high_" + std::to_string((uintptr_t)&i);
            std::string labelDone = ".L_dtoui_done_" + std::to_string((uintptr_t)&i);
            if (abi == X64ABI::Windows) {
                *os << "  movsd xmm0, " << srcOp << "\n";
                *os << "  mov rax, 0x43E0000000000000\n";
                *os << "  movq xmm1, rax\n";
                *os << "  comisd xmm0, xmm1\n";
                *os << "  jae " << labelHigh << "\n";
                *os << "  cvttsd2si rax, xmm0\n";
                *os << "  jmp " << labelDone << "\n";
                *os << labelHigh << ":\n";
                *os << "  subsd xmm0, xmm1\n";
                *os << "  cvttsd2si rax, xmm0\n";
                *os << "  mov rdx, 0x8000000000000000\n";
                *os << "  add rax, rdx\n";
                *os << labelDone << ":\n";
                *os << "  mov " << destOp << ", rax\n";
            } else {
                *os << "  movsd " << srcOp << ", %xmm0\n";
                *os << "  movabsq $0x43E0000000000000, %rax\n";
                *os << "  movq %rax, %xmm1\n";
                *os << "  comisd %xmm1, %xmm0\n";
                *os << "  jae " << labelHigh << "\n";
                *os << "  cvttsd2si %xmm0, %rax\n";
                *os << "  jmp " << labelDone << "\n";
                *os << labelHigh << ":\n";
                *os << "  subsd %xmm1, %xmm0\n";
                *os << "  cvttsd2si %xmm0, %rax\n";
                *os << "  movabsq $0x8000000000000000, %rdx\n";
                *os << "  addq %rdx, %rax\n";
                *os << labelDone << ":\n";
                *os << "  movq %rax, " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtUB) {
            std::string r8 = (srcOp[0] == '%') ? to8BitReg(srcOp) : srcOp;
            std::string d32 = (destOp[0] == '%') ? to32BitReg(destOp) : destOp;
            if (destOp[0] == '%') {
                *os << "  movzbl " << r8 << ", " << d32 << "\n";
            } else {
                *os << "  movzbl " << r8 << ", " << eax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtUH) {
            std::string r16 = (srcOp[0] == '%') ? to16BitReg(srcOp) : srcOp;
            std::string d32 = (destOp[0] == '%') ? to32BitReg(destOp) : destOp;
            if (destOp[0] == '%') {
                *os << "  movzwl " << r16 << ", " << d32 << "\n";
            } else {
                *os << "  movzwl " << r16 << ", " << eax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtUW) {
            std::string r32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
            std::string d32 = (destOp[0] == '%') ? to32BitReg(destOp) : destOp;
            if (destOp[0] == '%') {
                *os << "  movl " << r32 << ", " << d32 << "\n";
            } else {
                *os << "  movl " << r32 << ", " << eax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtSB) {
            std::string r8 = (srcOp[0] == '%') ? to8BitReg(srcOp) : srcOp;
            std::string d64 = (destOp[0] == '%') ? to64BitReg(destOp) : destOp;
            if (destOp[0] == '%') {
                *os << "  movsbq " << r8 << ", " << d64 << "\n";
            } else {
                *os << "  movsbq " << r8 << ", " << rax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtSH) {
            std::string r16 = (srcOp[0] == '%') ? to16BitReg(srcOp) : srcOp;
            std::string d64 = (destOp[0] == '%') ? to64BitReg(destOp) : destOp;
            if (destOp[0] == '%') {
                *os << "  movswq " << r16 << ", " << d64 << "\n";
            } else {
                *os << "  movswq " << r16 << ", " << rax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtSW) {
            std::string s32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
            std::string d64 = (destOp[0] == '%') ? to64BitReg(destOp) : destOp;
            if (d64[0] == '%') {
                *os << "  movslq " << s32 << ", " << d64 << "\n";
            } else {
                *os << "  movslq " << s32 << ", " << rax << "\n";
                *os << "  movq " << rax << ", " << destOp << "\n";
            }
        } else if (op == ir::Instruction::TruncD) {
            std::string d32 = (destOp[0] == '%') ? to32BitReg(destOp) : destOp;
            std::string s32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
            if (destOp[0] == '%') {
                *os << "  movl " << s32 << ", " << d32 << "\n";
            } else {
                *os << "  movl " << s32 << ", " << eax << "\n";
                *os << "  movl " << eax << ", " << destOp << "\n";
            }
        } else {
            bool is32 = is32BitType(to);
            std::string movOp = is32 ? "movl" : "movq";
            std::string regRax = is32 ? eax : rax;
            *os << "  movq " << srcOp << ", " << rax << "\n";
            *os << "  " << movOp << " " << regRax << ", " << destOp << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        ir::Instruction::Opcode op = i.getOpcode();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        if (op == ir::Instruction::Sltof || op == ir::Instruction::SWtoF || op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            as.emitBytes({0xF2, 0x48, 0x0F, 0x2A, 0xC0}); // cvtsi2sd xmm0, rax
            as.emitBytes({0x66, 0x48, 0x0F, 0x7E, 0xC0}); // movq rax, xmm0
        } else if (op == ir::Instruction::ExtUB) {
            as.emitBytes({0x0F, 0xB6, 0xC0}); // movzbl eax, al
        } else if (op == ir::Instruction::ExtUH) {
            as.emitBytes({0x0F, 0xB7, 0xC0}); // movzwl eax, ax
        } else if (op == ir::Instruction::ExtUW) {
            as.emitBytes({0x89, 0xC0});       // mov eax, eax
        } else if (op == ir::Instruction::ExtSB) {
            as.emitBytes({0x48, 0x0F, 0xBE, 0xC0}); // movsbq rax, al
        } else if (op == ir::Instruction::ExtSH) {
            as.emitBytes({0x48, 0x0F, 0xBF, 0xC0}); // movswq rax, ax
        } else if (op == ir::Instruction::ExtSW) {
            as.emitBytes({0x48, 0x63, 0xC0});       // movslq rax, eax
        }
        emitStoreResult(cg, i, 0);
    }
}
void X64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void X64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}

void X64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8; bool isSigned = true;
    switch(i.getOpcode()) {
        case ir::Instruction::Loadub: size = 1; isSigned = false; break;
        case ir::Instruction::Loadsb: size = 1; isSigned = true; break;
        case ir::Instruction::Loaduh: size = 2; isSigned = false; break;
        case ir::Instruction::Loadsh: size = 2; isSigned = true; break;
        case ir::Instruction::Loaduw: size = 4; isSigned = false; break;
        case ir::Instruction::Loadl:  size = 8; isSigned = true; break;
        default: {
            if (i.getType()) {
                TypeInfo info = getTypeInfo(i.getType());
                size = info.size;
                isSigned = info.isSigned;
            } else {
                size = 4;
                isSigned = true;
            }
            break;
        }
    }
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    std::string fpScratch = getReservedScratchVectorReg();
    if (auto* os = cg.getTextStream()) {
        ir::Value* ptrVal = i.getOperands()[0]->get();
        if (auto* ciSlot = dynamic_cast<ir::ConstantInt*>(ptrVal)) {
            std::string stackOp = formatStackOperand(-ciSlot->getValue());
            bool is32 = is32BitType(i.getType());
            std::string dest = (abi == X64ABI::Windows) ? (is32 ? "eax" : "rax") : (is32 ? "%eax" : "%rax");
            if (i.getType() && i.getType()->isFloatingPoint()) {
                dest = fpScratch;
            }
            if (i.hasPhysicalRegister()) {
                dest = cg.getValueAsOperand(&i);
            }
            emitMov(cg, os, stackOp, dest, is32);
            return;
        }
        ComplexAddress complexAddr = matchComplexAddress(cg, ptrVal);
        if (complexAddr.isValid) {
            std::string sibStr = complexAddr.format(abi);
            std::string destOp = cg.getValueAsOperand(&i);
            if (i.getType() && (i.getType()->isFloatTy() || i.getType()->isDoubleTy())) {
                const char* moveFP = i.getType()->isFloatTy() ? "movss" : "movsd";
                if (abi == X64ABI::Windows)
                    *os << "  " << moveFP << " " << destOp << ", " << sibStr << "\n";
                else
                    *os << "  " << moveFP << " " << sibStr << ", " << destOp << "\n";
                return;
            }
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << (isSigned ? "  movsbq " : "  movzbq ") << sibStr << ", " << rax << "\n";
                else if (size == 2) *os << (isSigned ? "  movswq " : "  movzwq ") << sibStr << ", " << rax << "\n";
                else if (size == 4) *os << (isSigned ? "  movslq " : "  movl ") << sibStr << ", " << eax << "\n";
                else *os << "  movq " << sibStr << ", " << rax << "\n";
            } else {
                if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr " : "  movzx rax, byte ptr ") << sibStr << "\n";
                else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr " : "  movzx rax, word ptr ") << sibStr << "\n";
                else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr " : "  mov eax, dword ptr ") << sibStr << "\n";
                else *os << "  mov rax, " << sibStr << "\n";
            }
            bool is32 = is32BitType(i.getType());
            emitMov(cg, os, is32 ? eax : rax, destOp, is32);
            return;
        }

        std::string op = cg.getValueAsOperand(ptrVal);
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(ptrVal) != nullptr;
        if (isGlobal) {
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << (isSigned ? "  movsbq " : "  movzbq ") << op << ", " << rax << "\n";
                else if (size == 2) *os << (isSigned ? "  movswq " : "  movzwq ") << op << ", " << rax << "\n";
                else if (size == 4) *os << (isSigned ? "  movslq " : "  movl ") << op << ", " << eax << "\n";
                else *os << "  movq " << op << ", " << rax << "\n";
            } else {
                if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr " : "  movzx rax, byte ptr ") << op << "\n";
                else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr " : "  movzx rax, word ptr ") << op << "\n";
                else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr " : "  mov eax, dword ptr ") << op << "\n";
                else *os << "  mov rax, " << op << "\n";
            }
        } else {
            // Load address of pointer slot into rax, then dereference
            if (abi == X64ABI::Windows) {
                *os << "  mov " << rax << ", " << op << "\n";
            } else {
                *os << "  movq " << op << ", " << rax << "\n";
            }
            if (abi == X64ABI::SystemV) {
                if (i.getType() && i.getType()->isFloatTy()) *os << "  movss (" << rax << "), " << fpScratch << "\n";
                else if (i.getType() && i.getType()->isDoubleTy()) *os << "  movsd (" << rax << "), " << fpScratch << "\n";
                else if (size == 1) *os << (isSigned ? "  movsbq (%rax), %rax\n" : "  movzbq (%rax), %rax\n");
                else if (size == 2) *os << (isSigned ? "  movswq (%rax), %rax\n" : "  movzwq (%rax), %rax\n");
                else if (size == 4) *os << (isSigned ? "  movslq (%rax), %rax\n" : "  movl (%rax), %eax\n");
                else *os << "  movq (%rax), %rax\n";
            } else {
                if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr [rax]\n" : "  movzx rax, byte ptr [rax]\n");
                else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr [rax]\n" : "  movzx rax, word ptr [rax]\n");
                else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr [rax]\n" : "  mov eax, dword ptr [rax]\n");
                else *os << "  mov rax, [rax]\n";
            }
        }
        // Store result
        if (i.getType() && (i.getType()->isFloatTy() || i.getType()->isDoubleTy())) {
            const char* moveFP = i.getType()->isFloatTy() ? "movss" : "movsd";
            if (abi == X64ABI::Windows)
                *os << "  " << moveFP << " " << cg.getValueAsOperand(&i) << ", " << fpScratch << "\n";
            else
                *os << "  " << moveFP << " " << fpScratch << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (abi == X64ABI::Windows)
            *os << "  mov " << cg.getValueAsOperand(&i) << ", " << rax << "\n";
        else {
            bool is32 = is32BitType(i.getType());
            emitMov(cg, os, is32 ? eax : rax, cg.getValueAsOperand(&i), is32);
        }
    } else {
        auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        if (size == 1) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBE : 0xB6), 0x00});
        else if (size == 2) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBF : 0xB7), 0x00});
        else if (size == 4) as.emitBytes(isSigned ? std::vector<uint8_t>{0x48, 0x63, 0x00} : std::vector<uint8_t>{0x8B, 0x00});
        else as.emitBytes({0x48, 0x8B, 0x00});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8;
    switch(i.getOpcode()) {
        case ir::Instruction::Storeb: size = 1; break;
        case ir::Instruction::Storeh: size = 2; break;
        case ir::Instruction::Storel: size = 8; break;
        default: {
            if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get()) {
                TypeInfo info = getTypeInfo(i.getOperands()[0]->get()->getType());
                size = info.size;
            } else {
                size = 4;
            }
            break;
        }
    }
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rdx = (abi == X64ABI::SystemV) ? "%rdx" : "rdx";
    std::string al = (abi == X64ABI::SystemV) ? "%al" : "al";
    std::string ax = (abi == X64ABI::SystemV) ? "%ax" : "ax";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    if (auto* os = cg.getTextStream()) {
        ir::Value* ptrVal = i.getOperands()[1]->get();
        ir::Type* storedType = i.getOperands()[0]->get()->getType();
        if (auto* ciSlot = dynamic_cast<ir::ConstantInt*>(ptrVal)) {
            std::string stackOp = formatStackOperand(-ciSlot->getValue());
            bool is32Val = (size <= 4);
            emitMov(cg, os, cg.getValueAsOperand(i.getOperands()[0]->get()), stackOp, is32Val);
            return;
        }

        struct SIBAddress {
            std::string base;
            std::string index;
            int scale = 1;
            int64_t disp = 0;
            bool isValid = false;

            std::string format(X64ABI abi) const {
                if (!isValid) return "";
                std::string res;
                if (disp != 0) res += std::to_string(disp);
                if (abi == X64ABI::SystemV) {
                    res += "(" + base;
                    if (!index.empty()) res += ", " + index + ", " + std::to_string(scale);
                    res += ")";
                } else {
                    res = "[" + base;
                    if (!index.empty()) res += " + " + index + " * " + std::to_string(scale);
                    if (disp > 0) res += " + " + std::to_string(disp);
                    else if (disp < 0) res += " - " + std::to_string(-disp);
                    res += "]";
                }
                return res;
            }
        };

        auto tryMatchSIB = [&](ir::Value* val) -> std::optional<SIBAddress> {
            if (!val) return std::nullopt;
            auto* inst = dynamic_cast<ir::Instruction*>(val);
            if (!inst) return std::nullopt;

            int64_t disp = 0;
            ir::Instruction* addrInst = inst;

            if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() == 2) {
                if (auto* c = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[1]->get())) {
                    disp = c->getValue();
                    if (auto* inner = dynamic_cast<ir::Instruction*>(inst->getOperands()[0]->get())) addrInst = inner;
                } else if (auto* c = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[0]->get())) {
                    disp = c->getValue();
                    if (auto* inner = dynamic_cast<ir::Instruction*>(inst->getOperands()[1]->get())) addrInst = inner;
                }
            }

            if (addrInst->getOpcode() == ir::Instruction::Add && addrInst->getOperands().size() == 2) {
                ir::Value* baseVal = addrInst->getOperands()[0]->get();
                ir::Value* scaledIndexVal = addrInst->getOperands()[1]->get();

                auto* mulInst = dynamic_cast<ir::Instruction*>(scaledIndexVal);
                if (!mulInst) {
                    std::swap(baseVal, scaledIndexVal);
                    mulInst = dynamic_cast<ir::Instruction*>(scaledIndexVal);
                }

                if (mulInst && mulInst->getOpcode() == ir::Instruction::Mul && mulInst->getOperands().size() == 2) {
                    ir::Value* idxVal = mulInst->getOperands()[0]->get();
                    auto* scaleC = dynamic_cast<ir::ConstantInt*>(mulInst->getOperands()[1]->get());
                    if (!scaleC) {
                        idxVal = mulInst->getOperands()[1]->get();
                        scaleC = dynamic_cast<ir::ConstantInt*>(mulInst->getOperands()[0]->get());
                    }

                    if (scaleC && (scaleC->getValue() == 1 || scaleC->getValue() == 2 || scaleC->getValue() == 4 || scaleC->getValue() == 8)) {
                        if (auto* extInst = dynamic_cast<ir::Instruction*>(idxVal)) {
                            if (extInst->getOpcode() == ir::Instruction::ExtSW || extInst->getOpcode() == ir::Instruction::ExtUW) {
                                if (!extInst->getOperands().empty()) idxVal = extInst->getOperands()[0]->get();
                            }
                        }
                        SIBAddress sib;
                        sib.base = cg.getValueAsOperand(baseVal);
                        sib.index = cg.getValueAsOperand(idxVal);
                        sib.scale = static_cast<int>(scaleC->getValue());
                        sib.disp = disp;
                        sib.isValid = true;
                        if (!sib.base.empty() && !sib.index.empty() &&
                            (abi == X64ABI::Windows || (sib.base[0] == '%' && sib.index[0] == '%'))) {
                            if (sib.index[0] == '%') sib.index = to64BitReg(sib.index);
                            return sib;
                        }
                    }
                }
            }
            return std::nullopt;
        };

        const std::string fpScratch = getReservedScratchVectorReg();
        ComplexAddress complexAddr = matchComplexAddress(cg, ptrVal);
        if (complexAddr.isValid) {
            std::string sibStr = complexAddr.format(abi);
            std::string valOp = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (storedType && storedType->isFloatingPoint()) {
                std::string move = storedType->isFloatTy() ? "movss" : "movsd";
                if (valOp.empty() || valOp[0] != '%') {
                    *os << "  " << move << " " << valOp << ", " << fpScratch << "\n";
                    valOp = fpScratch;
                }
                if (abi == X64ABI::SystemV) *os << "  " << move << " " << valOp << ", " << sibStr << "\n";
                else *os << "  " << move << " " << sibStr << ", " << valOp << "\n";
            } else {
                bool is32Val = (size <= 4);
                std::string scratch = "%r11";
                if (abi == X64ABI::Windows) {
                    *os << "  mov " << rax << ", " << valOp << "\n";
                    if (size == 1) *os << "  mov byte ptr " << sibStr << ", al\n";
                    else if (size == 2) *os << "  mov word ptr " << sibStr << ", ax\n";
                    else if (size == 4) *os << "  mov dword ptr " << sibStr << ", eax\n";
                    else *os << "  mov " << sibStr << ", rax\n";
                } else {
                    if (complexAddr.base == "%rax" || complexAddr.index == "%rax") {
                        emitMov(cg, os, valOp, scratch, is32Val);
                        if (size == 1) *os << "  movb " << to8BitReg(scratch) << ", " << sibStr << "\n";
                        else if (size == 2) *os << "  movw " << to16BitReg(scratch) << ", " << sibStr << "\n";
                        else if (size == 4) *os << "  movl " << to32BitReg(scratch) << ", " << sibStr << "\n";
                        else *os << "  movq " << scratch << ", " << sibStr << "\n";
                    } else {
                        emitMov(cg, os, valOp, rax, is32Val);
                        if (size == 1) *os << "  movb " << al << ", " << sibStr << "\n";
                        else if (size == 2) *os << "  movw " << ax << ", " << sibStr << "\n";
                        else if (size == 4) *os << "  movl " << eax << ", " << sibStr << "\n";
                        else *os << "  movq " << rax << ", " << sibStr << "\n";
                    }
                }
            }
            return;
        }

        if (storedType && storedType->isFloatingPoint()) {
            std::string valueOp = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string ptrOp = cg.getValueAsOperand(ptrVal);
            std::string move = storedType->isFloatTy() ? "movss" : "movsd";
            if (abi == X64ABI::SystemV) {
                if (isDirectGprRegister(ptrOp))
                    *os << "  " << move << " " << valueOp << ", (" << ptrOp << ")\n";
                else {
                    *os << "  movq " << ptrOp << ", %r11\n";
                    *os << "  " << move << " " << valueOp << ", (%r11)\n";
                }
            } else {
                if (isDirectGprRegister(ptrOp))
                    *os << "  " << move << " [" << ptrOp << "], " << valueOp << "\n";
                else {
                    *os << "  mov r11, " << ptrOp << "\n";
                    *os << "  " << move << " [r11], " << valueOp << "\n";
                }
            }
            return;
        }

        // Load value-to-store into rax
        bool isGlobalVal = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[0]->get()) != nullptr || 
                           (dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[0]->get()));
        bool is32Val = (size <= 4);
        if (abi == X64ABI::Windows) {
            if (isGlobalVal) *os << "  lea " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            else *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        } else {
            if (isGlobalVal) *os << "  leaq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
            else emitMov(cg, os, cg.getValueAsOperand(i.getOperands()[0]->get()), rax, is32Val);
        }
        std::string op = cg.getValueAsOperand(ptrVal);
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr;
        if (isGlobal) {
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << "  movb " << al << ", " << op << "\n";
                else if (size == 2) *os << "  movw " << ax << ", " << op << "\n";
                else if (size == 4) *os << "  movl " << eax << ", " << op << "\n";
                else *os << "  movq " << rax << ", " << op << "\n";
            } else {
                if (size == 1) *os << "  mov byte ptr " << op << ", al\n";
                else if (size == 2) *os << "  mov word ptr " << op << ", ax\n";
                else if (size == 4) *os << "  mov dword ptr " << op << ", eax\n";
                else *os << "  mov " << op << ", rax\n";
            }
        } else {
            // op = stack operand holding the pointer address — load it into r11
            if (abi == X64ABI::Windows)
                *os << "  mov r11, " << op << "\n";
            else
                *os << "  movq " << op << ", %r11\n";
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << "  movb " << al << ", (%r11)\n";
                else if (size == 2) *os << "  movw " << ax << ", (%r11)\n";
                else if (size == 4) *os << "  movl " << eax << ", (%r11)\n";
                else *os << "  movq " << rax << ", (%r11)\n";
            } else {
                if (size == 1) *os << "  mov byte ptr [r11], al\n";
                else if (size == 2) *os << "  mov word ptr [r11], ax\n";
                else if (size == 4) *os << "  mov dword ptr [r11], eax\n";
                else *os << "  mov [r11], rax\n";
            }
        }
    } else {
        auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 2);
        if (size == 1) as.emitBytes({0x88, 0x02});
        else if (size == 2) as.emitBytes({0x66, 0x89, 0x02});
        else if (size == 4) as.emitBytes({0x89, 0x02});
        else as.emitBytes({0x48, 0x89, 0x02});
    }
}

void X64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    int32_t pointerOffset = cg.getStackOffset(&i);
    uint64_t size = 8;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) { if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())) size = sizeConst->getValue(); }
    uint64_t alignedSize = (size + 7) & ~7;
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string dstOp = cg.getValueAsOperand(&i);
    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        if (abi == X64ABI::SystemV) {
            *os << "  movq heap_ptr(%rip), " << rax << "\n";
            *os << "  movq " << rax << ", " << dstOp << "\n";
            *os << "  addq $" << alignedSize << ", " << rax << "\n";
            *os << "  movq " << rax << ", heap_ptr(%rip)\n";
        } else {
            *os << "  mov rax, [rip + heap_ptr]\n";
            *os << "  mov " << dstOp << ", rax\n";
            *os << "  add rax, " << alignedSize << "\n";
            *os << "  mov [rip + heap_ptr], rax\n";
        }
    } else {
        auto& as = cg.getAssembler(); ir::GlobalValue hp_val(cg.module.getContext()->getVoidType(), "heap_ptr"); emitLoadValue(cg, as, &hp_val, 0);
        emitRegMem(as, 0x48, 0x89, 0, pointerOffset); as.emitBytes({0x48, 0x05}); as.emitDWord(alignedSize);
        as.emitBytes({0x48, 0x89, 0x05}); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "heap_ptr", ".text"});
    }
}

void X64Architecture::emitPhiCopies(CodeGen& cg, ir::BasicBlock* source, ir::BasicBlock* target) {
    if (!target) return;
    cg.lastStoreOp = "";
    std::vector<std::pair<ir::Value*, ir::PhiNode*>> phiMoves;
    for (auto& instr : target->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
            ir::Value* incomingVal = phi->getIncomingValueForBlock(source);
            if (incomingVal) {
                phiMoves.push_back({incomingVal, phi});
            }
        }
    }
    if (phiMoves.empty()) return;

    // Direct move check: if single phi copy or no overlap, try direct moves without push/pop stack operations
    bool canDirectMove = true;
    for (const auto& move : phiMoves) {
        std::string srcOp = cg.getValueAsOperand(move.first);
        std::string destOp = cg.getValueAsOperand(move.second);
        if (srcOp.empty() || destOp.empty()) {
            canDirectMove = false;
            break;
        }
        // x86-64 cannot execute memory-to-memory mov (e.g. -8(%rbp) -> -16(%rbp)) directly
        bool srcIsMem = (srcOp[0] == '-' || srcOp[0] == '[' || srcOp.find("(%rbp)") != std::string::npos);
        bool destIsMem = (destOp[0] == '-' || destOp[0] == '[' || destOp.find("(%rbp)") != std::string::npos);
        if (srcIsMem && destIsMem) {
            canDirectMove = false;
            break;
        }
    }

    // Check for self cycles among multiple phi moves (e.g. swap %r10 <-> %r11)
    if (canDirectMove && phiMoves.size() > 1) {
        for (size_t i = 0; i < phiMoves.size(); ++i) {
            std::string dest_i = cg.getValueAsOperand(phiMoves[i].second);
            for (size_t j = i + 1; j < phiMoves.size(); ++j) {
                std::string src_j = cg.getValueAsOperand(phiMoves[j].first);
                if (dest_i == src_j) {
                    canDirectMove = false; // Overlap detected, fallback to push/pop to preserve semantics
                    break;
                }
            }
            if (!canDirectMove) break;
        }
    }

    if (canDirectMove) {
        for (const auto& move : phiMoves) {
            ir::Value* incomingVal = move.first;
            ir::PhiNode* phi = move.second;
            std::string srcOp = cg.getValueAsOperand(incomingVal);
            std::string destOp = cg.getValueAsOperand(phi);
            if (srcOp == destOp) continue;

            bool is32 = is32BitType(phi->getType());
            auto* vecType = dynamic_cast<const ir::VectorType*>(phi->getType());
            unsigned totalBits = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;
            if (auto* os = cg.getTextStream()) {
                if (abi == X64ABI::Windows) {
                    *os << "  mov " << destOp << ", " << srcOp << "\n";
                } else {
                    emitMov(cg, os, srcOp, destOp, is32, totalBits);
                }
            } else {
                auto& as = cg.getAssembler();
                emitLoadValue(cg, as, incomingVal, 0);
                emitStoreResult(cg, *phi, 0);
            }
        }
        return;
    }

    for (const auto& move : phiMoves) {
        ir::Value* incomingVal = move.first;
        if (auto* os = cg.getTextStream()) {
            std::string srcOp = cg.getValueAsOperand(incomingVal);
            bool isVector = incomingVal->getType() && (incomingVal->getType()->isVectorTy() || incomingVal->getType()->isSIMDType() || dynamic_cast<const ir::VectorType*>(incomingVal->getType()) != nullptr);
            if (isVector) {
                auto* vecType = dynamic_cast<const ir::VectorType*>(incomingVal->getType());
                unsigned totalBits = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;
                if (totalBits == 256) {
                    std::string ymmSrc = toYmmReg(srcOp);
                    *os << "  subq $32, %rsp\n";
                    *os << "  vmovdqu " << ymmSrc << ", (%rsp)\n";
                } else {
                    *os << "  subq $16, %rsp\n";
                    *os << "  movdqu " << srcOp << ", (%rsp)\n";
                }
            } else {
                bool is32 = is32BitType(incomingVal->getType());
                std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
                if (abi == X64ABI::Windows) {
                    *os << "  mov " << rax << ", " << srcOp << "\n";
                    *os << "  push " << rax << "\n";
                } else {
                    emitMov(cg, os, srcOp, rax, is32);
                    *os << "  pushq %rax\n";
                }
            }
        } else {
            auto& as = cg.getAssembler();
            emitLoadValue(cg, as, incomingVal, 0);
            as.emitByte(0x50);
        }
    }
    for (auto it = phiMoves.rbegin(); it != phiMoves.rend(); ++it) {
        ir::PhiNode* phi = it->second;
        if (auto* os = cg.getTextStream()) {
            std::string destOp = cg.getValueAsOperand(phi);
            bool isVector = phi->getType() && (phi->getType()->isVectorTy() || phi->getType()->isSIMDType() || dynamic_cast<const ir::VectorType*>(phi->getType()) != nullptr);
            if (isVector) {
                auto* vecType = dynamic_cast<const ir::VectorType*>(phi->getType());
                unsigned totalBits = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;
                if (totalBits == 256) {
                    std::string ymmDst = toYmmReg(destOp);
                    *os << "  vmovdqu (%rsp), " << ymmDst << "\n";
                    *os << "  addq $32, %rsp\n";
                } else {
                    *os << "  movdqu (%rsp), " << destOp << "\n";
                    *os << "  addq $16, %rsp\n";
                }
            } else {
                bool is32 = is32BitType(phi->getType());
                std::string movOp = is32 ? "movl" : "movq";
                std::string regRax = is32 ? "%eax" : "%rax";
                std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
                if (abi == X64ABI::Windows)
                    *os << "  pop " << rax << "\n";
                else
                    *os << "  popq " << rax << "\n";
                if (abi == X64ABI::Windows)
                    *os << "  mov " << destOp << ", " << rax << "\n";
                else
                    *os << "  " << movOp << " " << regRax << ", " << destOp << "\n";
            }
        } else {
            auto& as = cg.getAssembler();
            as.emitByte(0x58);
            emitStoreResult(cg, *phi, 0);
        }
    }
}

void X64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    bool is32 = is32BitType(i.getOperands()[0]->get()->getType());
    std::string movOp = is32 ? "movl" : "movq";
    std::string testOp = is32 ? "testl" : "testq";
    std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get());

    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            *os << "  test " << rax << ", " << rax << "\n";
        } else {
            *os << "  " << movOp << " " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
            *os << "  " << testOp << " " << rax << ", " << rax << "\n";
        }

        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        bool trueHasPhis = false;
        for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        bool falseHasPhis = false;
        for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;

        if (trueHasPhis || falseHasPhis) {
            std::string labelTrueCopies = ".L_true_copies_" + std::to_string((uintptr_t)&i);
            std::string labelFalseCopies = ".L_false_copies_" + std::to_string((uintptr_t)&i);

            *os << "  jne " << labelTrueCopies << "\n";
            *os << "  jmp " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetTrue);
            *os << "  jmp " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetFalse);
            *os << "  jmp " << falseLabel << "\n";
        } else {
            *os << "  jne " << trueLabel << "\n";
            *os << "  jmp " << falseLabel << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);

        bool trueHasPhis = false;
        for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        bool falseHasPhis = false;
        for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;

        if (trueHasPhis || falseHasPhis) {
            as.emitBytes({0x48, 0x85, 0xC0});
            as.emitBytes({0x0F, 0x85});
            uint64_t trueCopiesOff = as.getCodeSize();
            as.emitDWord(0);

            emitPhiCopies(cg, i.getParent(), targetFalse);
            as.emitByte(0xE9);
            uint64_t falseTargetOff = as.getCodeSize();
            as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{falseTargetOff, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(targetFalse), ".text"});

            uint32_t trueCopiesRel = as.getCodeSize() - (trueCopiesOff + 4);
            as.setByteAt(trueCopiesOff, trueCopiesRel & 0xFF);
            as.setByteAt(trueCopiesOff + 1, (trueCopiesRel >> 8) & 0xFF);
            as.setByteAt(trueCopiesOff + 2, (trueCopiesRel >> 16) & 0xFF);
            as.setByteAt(trueCopiesOff + 3, (trueCopiesRel >> 24) & 0xFF);

            emitPhiCopies(cg, i.getParent(), targetTrue);
            as.emitByte(0xE9);
            uint64_t trueTargetOff = as.getCodeSize();
            as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{trueTargetOff, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(targetTrue), ".text"});
        } else {
            as.emitBytes({0x48, 0x85, 0xC0, 0x0F, 0x85});
            uint64_t off1 = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(targetTrue), ".text"});
            as.emitByte(0xE9);
            uint64_t off2 = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(targetFalse), ".text"});
        }
    }
}

void X64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
    emitPhiCopies(cg, i.getParent(), targetBB);
    if (auto* os = cg.getTextStream()) {
        *os << "  jmp " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
    } else {
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(targetBB), ".text"});
    }
}

bool X64Architecture::emitMulAddFusion(CodeGen& cg, ir::Instruction& mul, ir::Instruction& add) {
    // Only fuse if mul's result is used ONLY by add (so eliminating mul is safe)
    if (mul.getUseList().size() != 1) return false;

    if (!mul.getType() || !mul.getType()->isInteger()) return false;
    if (!add.getType() || !add.getType()->isInteger()) return false;

    // Only support 32-bit and 64-bit integer arithmetic for LEA to avoid wrapping/truncation semantic mismatch
    size_t mulSize = mul.getType()->getSize();
    size_t addSize = add.getType()->getSize();
    if (mulSize != addSize || (mulSize != 4 && mulSize != 8)) return false;

    bool is32 = (addSize == 4);

    auto getConstInt = [](ir::Value* v, int64_t& outVal) -> bool {
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) {
            outVal = static_cast<int64_t>(ci->getValue());
            return true;
        }
        return false;
    };

    if (mul.getOperands().size() < 2 || add.getOperands().size() < 2) return false;
    ir::Value* m0 = mul.getOperands()[0]->get();
    ir::Value* m1 = mul.getOperands()[1]->get();
    int64_t mulConst = 0;
    ir::Value* x = nullptr;
    if (getConstInt(m1, mulConst)) {
        x = m0;
    } else if (getConstInt(m0, mulConst)) {
        x = m1;
    } else {
        return false;
    }

    ir::Value* a0 = add.getOperands()[0]->get();
    ir::Value* a1 = add.getOperands()[1]->get();
    ir::Value* addOther = (a0 == &mul) ? a1 : a0;

    int64_t addConst = 0;
    bool hasAddConst = getConstInt(addOther, addConst);

    // Enforce 32-bit signed displacement limits for x86-64 SIB encoding
    if (hasAddConst && (addConst < -2147483648LL || addConst > 2147483647LL)) {
        return false;
    }

    std::string baseReg = "";
    std::string indexReg = "";
    int scale = 1;

    if (hasAddConst) {
        if (mulConst == 1 || mulConst == 2 || mulConst == 4 || mulConst == 8) {
            indexReg = cg.getValueAsOperand(x);
            scale = static_cast<int>(mulConst);
        } else if (mulConst == 3) {
            baseReg = cg.getValueAsOperand(x);
            indexReg = cg.getValueAsOperand(x);
            scale = 2; // x + 2*x = 3*x
        } else if (mulConst == 5) {
            baseReg = cg.getValueAsOperand(x);
            indexReg = cg.getValueAsOperand(x);
            scale = 4; // x + 4*x = 5*x
        } else if (mulConst == 9) {
            baseReg = cg.getValueAsOperand(x);
            indexReg = cg.getValueAsOperand(x);
            scale = 8; // x + 8*x = 9*x
        } else {
            return false;
        }
    } else {
        // x * scale + y
        if (mulConst == 1 || mulConst == 2 || mulConst == 4 || mulConst == 8) {
            baseReg = cg.getValueAsOperand(addOther);
            indexReg = cg.getValueAsOperand(x);
            scale = static_cast<int>(mulConst);
        } else {
            return false;
        }
    }

    auto prepareReg = [&](const std::string& r) -> std::string {
        if (r.empty()) return "";
        if (r[0] == '%') return is32 ? to32BitReg(r) : to64BitReg(r);
        return r;
    };

    baseReg = prepareReg(baseReg);
    indexReg = prepareReg(indexReg);

    // Ensure operands are valid registers, not constants, memory/stack slots, or global symbols
    auto isRegOp = [](const std::string& r) {
        if (r.empty()) return true;
        return r[0] == '%';
    };

    auto isWinRegOp = [](const std::string& r) {
        if (r.empty()) return true;
        if (r[0] == '[' || r[0] == '$' || r[0] == '-' || r[0] == '+') return false;
        if (r.find("rbp") != std::string::npos || r.find("rsp") != std::string::npos) return false;
        static const std::set<std::string> validWinRegs = {
            "rax", "rcx", "rdx", "rbx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
            "eax", "ecx", "edx", "ebx", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
        };
        return validWinRegs.count(r) > 0;
    };

    if (abi == X64ABI::Windows) {
        if (!isWinRegOp(baseReg) || !isWinRegOp(indexReg)) return false;
    } else {
        if (!isRegOp(baseReg) || !isRegOp(indexReg)) return false;
    }

    std::string dst = cg.getValueAsOperand(&add);
    std::string d = is32 ? to32BitReg(dst) : to64BitReg(dst);
    std::string leaOp = is32 ? "leal" : "leaq";

    auto* os = cg.getTextStream();
    if (!os) return false;

    if (abi == X64ABI::Windows) {
        std::string addrStr = "";
        if (!baseReg.empty()) addrStr += baseReg;
        if (!indexReg.empty()) {
            if (!addrStr.empty()) addrStr += " + ";
            addrStr += indexReg;
            if (scale > 1) addrStr += " * " + std::to_string(scale);
        }
        if (addConst != 0 || addrStr.empty()) {
            if (!addrStr.empty()) {
                if (addConst > 0) addrStr += " + " + std::to_string(addConst);
                else addrStr += " - " + std::to_string(-addConst);
            } else {
                addrStr = std::to_string(addConst);
            }
        }
        bool isStackDst = !isDirectGprRegister(d);
        std::string targetReg = isStackDst ? (is32 ? "eax" : "rax") : d;
        *os << "  lea " << targetReg << ", [" << addrStr << "]\n";
        if (isStackDst) {
            *os << "  mov " << d << ", " << targetReg << "\n";
        }
    } else {
        bool isStackDst = !isDirectGprRegister(d);
        std::string targetReg = isStackDst ? (is32 ? "%eax" : "%rax") : d;
        std::string dispStr = (addConst != 0) ? std::to_string(addConst) : "";
        std::string indexStr = "";
        if (!indexReg.empty()) {
            indexStr = indexReg;
            if (scale > 1) indexStr += "," + std::to_string(scale);
        }
        *os << "  " << leaOp << " " << dispStr << "(" << baseReg;
        if (!indexStr.empty()) {
            *os << "," << indexStr;
        }
        *os << "), " << targetReg << "\n";
        if (isStackDst) {
            emitMov(cg, os, targetReg, d, is32);
        }
    }

    cg.lastStoreOp = "";
    return true;
}

bool X64Architecture::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) {
    if (br.getOperands().size() < 3) return false;
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(br.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(br.getOperands()[2]->get());
    if (!targetTrue || !targetFalse) return false;

    std::string jcc;
    switch (cmp.getOpcode()) {
        case ir::Instruction::Ceq: case ir::Instruction::Ceqf: jcc = "je"; break;
        case ir::Instruction::Cne: case ir::Instruction::Cnef: jcc = "jne"; break;
        case ir::Instruction::Cslt: jcc = "jl"; break;
        case ir::Instruction::Cult: case ir::Instruction::Clt: jcc = "jb"; break;
        case ir::Instruction::Csle: jcc = "jle"; break;
        case ir::Instruction::Cule: case ir::Instruction::Cle: jcc = "jbe"; break;
        case ir::Instruction::Csgt: jcc = "jg"; break;
        case ir::Instruction::Cugt: case ir::Instruction::Cgt: jcc = "ja"; break;
        case ir::Instruction::Csge: jcc = "jge"; break;
        case ir::Instruction::Cuge: case ir::Instruction::Cge: jcc = "jae"; break;
        default: jcc = "jne"; break;
    }

    std::string op0 = cg.getValueAsOperand(cmp.getOperands()[0]->get());
    std::string op1 = cg.getValueAsOperand(cmp.getOperands()[1]->get());
    bool is32 = is32BitType(cmp.getOperands()[0]->get()->getType());
    std::string cmpOp = is32 ? "cmpl" : "cmpq";
    std::string regRax = is32 ? "%eax" : "%rax";

    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            if (op0[0] == '[' && op1[0] == '[') {
                *os << "  mov rax, " << op0 << "\n";
                *os << "  cmp rax, " << op1 << "\n";
            } else {
                *os << "  cmp " << op0 << ", " << op1 << "\n";
            }
        } else {
            if (op0[0] == '%') op0 = is32 ? to32BitReg(op0) : to64BitReg(op0);
            if (op1[0] == '%') op1 = is32 ? to32BitReg(op1) : to64BitReg(op1);

            if (op0[0] == '$' || (op0[0] != '%' && op1[0] != '%')) {
                emitMov(cg, os, cg.getValueAsOperand(cmp.getOperands()[0]->get()), regRax, is32);
                if (op1 == "$0" || op1 == "$0x0") {
                    std::string testOp = is32 ? "testl" : "testq";
                    *os << "  " << testOp << " " << regRax << ", " << regRax << "\n";
                } else {
                    *os << "  " << cmpOp << " " << op1 << ", " << regRax << "\n";
                }
            } else {
                if (op1 == "$0" || op1 == "$0x0") {
                    std::string testOp = is32 ? "testl" : "testq";
                    *os << "  " << testOp << " " << op0 << ", " << op0 << "\n";
                } else {
                    *os << "  " << cmpOp << " " << op1 << ", " << op0 << "\n";
                }
            }
        }

        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        bool trueHasPhis = false;
        for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        bool falseHasPhis = false;
        for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;

        if (trueHasPhis || falseHasPhis) {
            std::string labelTrueCopies = ".L_true_copies_" + std::to_string((uintptr_t)&br);
            std::string labelFalseCopies = ".L_false_copies_" + std::to_string((uintptr_t)&br);

            *os << "  " << jcc << " " << labelTrueCopies << "\n";
            *os << "  jmp " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetTrue);
            *os << "  jmp " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetFalse);
            *os << "  jmp " << falseLabel << "\n";
        } else {
            *os << "  " << jcc << " " << trueLabel << "\n";
            *os << "  jmp " << falseLabel << "\n";
        }
        return true;
    }
    return false;
}

void X64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (abi == X64ABI::SystemV) {
        if (auto* os = cg.getTextStream()) {
            ir::SyscallId sid = ir::SyscallId::None;
            auto* si = dynamic_cast<ir::SyscallInstruction*>(&i);
            if (si) sid = si->getSyscallId();
            if (sid != ir::SyscallId::None) {
                *os << "  movq $" << static_cast<uint64_t>(sid) << ", %rax\n";
            } else if (!i.getOperands().empty()) {
                *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
            }
            size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
            for (size_t j = startArg; j < i.getOperands().size(); ++j) {
                size_t argIdx = (sid != ir::SyscallId::None) ? j + 1 : j; std::string dest;
                switch(argIdx) { case 1: dest = "%rdi"; break; case 2: dest = "%rsi"; break; case 3: dest = "%rdx"; break; case 4: dest = "%r10"; break; case 5: dest = "%r8"; break; case 6: dest = "%r9"; break; }
                if (!dest.empty()) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[j]->get()) << ", " << dest << "\n";
            }
            *os << "  syscall\n"; if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    } else {
        auto* si = dynamic_cast<ir::SyscallInstruction*>(&i);
        if (si && si->getSyscallId() == ir::SyscallId::Exit) {
            if (auto* os = cg.getTextStream()) {
                *os << "  mov rcx, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  call ExitProcess\n";
            } else {
                auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 1); // rcx
                as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
                cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "ExitProcess", ".text"});
            }
        }
    }
}

void X64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i);
    if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (!spec || !cg.getTargetInfo()->validateCapability(i, *spec)) {
        cg.getTargetInfo()->emitUnsupportedCapability(cg, i, spec);
        return;
    }
    cg.getTargetInfo()->emitDomainCapability(cg, i, *spec);

    if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
        if (auto* os = cg.getTextStream()) {
            if (abi == X64ABI::SystemV)
                *os << "  movq %rax, " << formatStackOperand(cg.getStackOffsets()[&i]) << "\n";
            else
                *os << "  mov " << formatStackOperand(cg.getStackOffsets()[&i]) << ", rax\n";
        } else {
            emitStoreResult(cg, i, 0);
        }
    }
}

void X64Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    std::string rax = getRegisterName("rax", nullptr);
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $" << syscallNum << ", " << rax << "\n";
        static const char* sysregs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            bool isGlobal = dynamic_cast<ir::GlobalVariable*>(args[i]) != nullptr;
            std::string reg = getRegisterName(sysregs[i], args[i]->getType());
            std::string op = cg.getValueAsOperand(args[i]);
            if (isGlobal) {
                if (abi == X64ABI::Windows)
                    *os << "  lea " << reg << ", " << op << "\n";
                else
                    *os << "  leaq " << op << ", " << reg << "\n";
            } else {
                *os << "  movq " << op << ", " << reg << "\n";
            }
        }
        *os << "  syscall\n";
    } else {
        auto& as = cg.getAssembler();
        as.emitBytes({0x48, 0xC7, 0xC0}); as.emitDWord(syscallNum);
        static const char* sysregs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            uint8_t r = getArchRegIndex(sysregs[i]);
            emitLoadValue(cg, as, args[i], r);
        }
        as.emitBytes({0x0F, 0x05});
    }
}

void X64Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        auto emitArg = [&](ir::Value* arg, const std::string& reg) {
            bool isGlobal = dynamic_cast<ir::GlobalVariable*>(arg) != nullptr;
            std::string op = cg.getValueAsOperand(arg);
            if (isGlobal) {
                if (abi == X64ABI::Windows)
                    *os << "  lea " << reg << ", " << op << "\n";
                else
                    *os << "  leaq " << op << ", " << reg << "\n";
            } else {
                *os << "  movq " << op << ", " << reg << "\n";
            }
        };
        if (abi == X64ABI::Windows) {
            *os << "  sub rsp, 32\n";
            static const char* winRegs[] = {"rcx", "rdx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)4); ++i)
                emitArg(args[i], getRegisterName(winRegs[i], args[i]->getType()));
            *os << "  call " << name << "\n";
            *os << "  add rsp, 32\n";
        } else {
            static const char* sysvRegs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i)
                emitArg(args[i], getRegisterName(sysvRegs[i], args[i]->getType()));
            *os << "  call " << name << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        if (abi == X64ABI::Windows) {
            as.emitBytes({0x48, 0x83, 0xEC, 0x20});
            static const char* winRegs[] = {"rcx", "rdx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)4); ++i) {
                emitLoadValue(cg, as, args[i], getArchRegIndex(winRegs[i]));
            }
            as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, name, ".text"});
            as.emitBytes({0x48, 0x83, 0xC4, 0x20});
        } else {
            static const char* sysvRegs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
                emitLoadValue(cg, as, args[i], getArchRegIndex(sysvRegs[i]));
            }
            as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, name, ".text"});
        }
    }
}

std::string X64Architecture::formatStackOperand(int offset) const {
    if (abi == X64ABI::SystemV) return std::to_string(offset) + "(%rbp)";
    return "[rbp + " + std::to_string(offset) + "]";
}

std::string X64Architecture::formatGlobalOperand(const std::string& name) const {
    if (abi == X64ABI::SystemV) return name + "(%rip)";
    return "[rip + " + name + "]";
}

std::string X64Architecture::formatConstant(const ir::ConstantInt* C) const {
    if (abi == X64ABI::Windows) return std::to_string(C->getValue());
    return "$" + std::to_string(C->getValue());
}

std::string X64Architecture::formatConstant(const ir::ConstantFP* C) const {
    uint64_t bits = 0;
    double val = C->getValue();
    std::memcpy(&bits, &val, sizeof(double));
    if (abi == X64ABI::Windows) return std::to_string(bits);
    return "$" + std::to_string(bits);
}

bool X64Architecture::isCallerSaved(const std::string& reg) const { return callerSaved.count(reg) && callerSaved.at(reg); }
bool X64Architecture::isCalleeSaved(const std::string& reg) const { return calleeSaved.count(reg) && calleeSaved.at(reg); }
bool X64Architecture::isReserved(const std::string& reg) const {
    return reg == "rsp" || reg == "rbp" || reg == "%rsp" || reg == "%rbp";
}

std::string X64Architecture::getReservedScratchVectorReg() const {
    if (abi == X64ABI::Windows) return "xmm5";
    return "%xmm15";
}

unsigned X64Architecture::getReservedScratchVectorRegIndex() const {
    if (abi == X64ABI::Windows) return 105;
    return 115;
}

VectorCapabilities X64Architecture::getVectorCapabilities() const {
    VectorCapabilities caps;
    caps.supportsSSE = true;
    caps.supportsSSSE3 = true;
    caps.supportsAVX = true;
    caps.supportsAVX2 = true;
    caps.supportsAVX512 = false;
    caps.maxVectorWidth = 256;
    caps.supportedWidths = {128, 256};
    caps.supportsFloatVectors = true;
    caps.supportsIntegerVectors = true;
    caps.supportsDoubleVectors = true;
    caps.supportsMaskedOps = true;
    caps.simdExtension = "SSE2/SSSE3/SSE4.1/AVX2";
    return caps;
}

bool X64Architecture::supportsVectorWidth(unsigned width) const {
    return width == 128 || width == 256;
}

bool X64Architecture::supportsVectorType(const ir::VectorType* type) const {
    if (!type) return false;
    auto* elemTy = type->getElementType();
    if (!elemTy) return false;

    unsigned numElem = type->getNumElements();

    if (elemTy->isIntegerTy()) {
        auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
        if (!intTy) return false;
        unsigned bw = intTy->getBitwidth();
        if (bw == 8 && (numElem == 16 || numElem == 32)) return true;
        if (bw == 16 && (numElem == 8 || numElem == 16)) return true;
        if (bw == 32 && (numElem == 4 || numElem == 8)) return true;
        if (bw == 64 && (numElem == 2 || numElem == 4)) return true;
    } else if (elemTy->isFloatTy()) {
        if (numElem == 4 || numElem == 8) return true;
    } else if (elemTy->isDoubleTy()) {
        if (numElem == 2 || numElem == 4) return true;
    }

    return false;
}

bool X64Architecture::supportsVectorOperation(ir::Instruction::Opcode op, const ir::VectorType* type) const {
    if (!type || !type->getElementType()) return false;
    auto* elemTy = type->getElementType();
    unsigned numElem = type->getNumElements();

    bool validWidth = false;
    if (elemTy->isIntegerTy()) {
        auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
        if (!intTy) return false;
        unsigned bw = intTy->getBitwidth();
        if (bw == 8 && (numElem == 16 || numElem == 32)) validWidth = true;
        if (bw == 16 && (numElem == 8 || numElem == 16)) validWidth = true;
        if (bw == 32 && (numElem == 4 || numElem == 8)) validWidth = true;
        if (bw == 64 && (numElem == 2 || numElem == 4)) validWidth = true;
        if (bw == 64 && op == ir::Instruction::VMul) return false;
    } else if (elemTy->isFloatTy()) {
        if (numElem == 4 || numElem == 8) validWidth = true;
    } else if (elemTy->isDoubleTy()) {
        if (numElem == 2 || numElem == 4) validWidth = true;
    }

    if (!validWidth) return false;

    switch (op) {
        case ir::Instruction::VAdd:
        case ir::Instruction::VSub:
        case ir::Instruction::VMul:
        case ir::Instruction::VFAdd:
        case ir::Instruction::VFSub:
        case ir::Instruction::VFMul:
        case ir::Instruction::VFDiv:
        case ir::Instruction::VLoad:
        case ir::Instruction::VStore:
        case ir::Instruction::VAnd:
        case ir::Instruction::VOr:
        case ir::Instruction::VXor:
        case ir::Instruction::VBroadcast:
        case ir::Instruction::VExtract:
        case ir::Instruction::VInsert:
        case ir::Instruction::VMin:
        case ir::Instruction::VMax:
        case ir::Instruction::VCmp:
        case ir::Instruction::VSelect:
            return true;
        default:
            return false;
    }
}

bool X64Architecture::supportsVectorConversion(ir::Instruction::Opcode op, const ir::VectorType* srcType, const ir::VectorType* dstType) const {
    if (!srcType || !dstType) return false;
    auto* srcElemTy = dynamic_cast<const ir::IntegerType*>(srcType->getElementType());
    auto* dstElemTy = dynamic_cast<const ir::IntegerType*>(dstType->getElementType());
    if (!srcElemTy || !dstElemTy) return false;

    unsigned srcBw = srcElemTy->getBitwidth();
    unsigned dstBw = dstElemTy->getBitwidth();
    unsigned srcNum = srcType->getNumElements();
    unsigned dstNum = dstType->getNumElements();

    if (op == ir::Instruction::VSExt || op == ir::Instruction::VZExt) {
        // Support <4 x i32> -> <4 x i64> widening conversion
        if (srcBw == 32 && dstBw == 64 && srcNum == 4 && dstNum == 4) {
            return true;
        }
    }
    return false;
}

void X64Architecture::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string ptrOp = cg.getValueAsOperand(i.getOperands()[0]->get());
        std::string dstOp = cg.getValueAsOperand(&i);
        auto* vecType = dynamic_cast<const ir::VectorType*>(i.getType());
        unsigned totalBitWidth = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;

        bool isReg = isXmmRegisterName(dstOp) || isYmmRegisterName(dstOp);
        bool isFloat = vecType && vecType->getElementType()->isFloatTy();
        bool isDouble = vecType && vecType->getElementType()->isDoubleTy();
        std::string movInst = isFloat ? (totalBitWidth == 256 ? "vmovups" : "movups")
                            : isDouble ? (totalBitWidth == 256 ? "vmovupd" : "movupd")
                            : (totalBitWidth == 256 ? "vmovdqu" : "movdqu");
        std::string targetReg = isReg ? (totalBitWidth == 256 ? toYmmReg(dstOp) : dstOp) : (totalBitWidth == 256 ? "%ymm0" : "%xmm0");

        if (abi == X64ABI::Windows) {
            targetReg = isReg ? (totalBitWidth == 256 ? toYmmReg(dstOp) : dstOp) : (totalBitWidth == 256 ? "ymm0" : "xmm0");
            if (isDirectGprRegister(ptrOp)) {
                *os << "  " << movInst << " " << targetReg << ", [" << ptrOp << "]\n";
            } else {
                std::string rax = "rax";
                *os << "  mov " << rax << ", " << ptrOp << "\n";
                *os << "  " << movInst << " " << targetReg << ", [" << rax << "]\n";
            }
            if (!isReg) {
                *os << "  " << movInst << " [" << dstOp << "], " << targetReg << "\n";
            }
        } else {
            if (isDirectGprRegister(ptrOp)) {
                *os << "  " << movInst << " (" << ptrOp << "), " << targetReg << "\n";
            } else {
                std::string rax = "%rax";
                *os << "  movq " << ptrOp << ", " << rax << "\n";
                *os << "  " << movInst << " (%rax), " << targetReg << "\n";
            }
            if (!isReg) {
                *os << "  " << movInst << " " << targetReg << ", " << dstOp << "\n";
            }
        }
    }
}

void X64Architecture::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string srcOp = cg.getValueAsOperand(i.getOperands()[0]->get());
        std::string ptrOp = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto* vecType = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
        unsigned totalBitWidth = vecType ? vecType->getElementType()->getSize() * 8 * vecType->getNumElements() : 128;

        bool isFloat = vecType && vecType->getElementType()->isFloatTy();
        bool isDouble = vecType && vecType->getElementType()->isDoubleTy();
        std::string movInst = isFloat ? "movups" : isDouble ? "movupd" : "movdqu";
        if (totalBitWidth == 256) {
            movInst = isFloat ? "vmovups" : isDouble ? "vmovupd" : "vmovdqu";
            srcOp = toYmmReg(srcOp);
        } else if (totalBitWidth == 512) {
            movInst = "vmovdqu64";
            srcOp = toZmmReg(srcOp);
        }

        if (abi == X64ABI::Windows) {
            if (isDirectGprRegister(ptrOp)) {
                *os << "  " << movInst << " [" << ptrOp << "], " << srcOp << "\n";
            } else {
                std::string rax = "rax";
                *os << "  mov " << rax << ", " << ptrOp << "\n";
                *os << "  " << movInst << " [" << rax << "], " << srcOp << "\n";
            }
        } else {
            if (isDirectGprRegister(ptrOp)) {
                *os << "  " << movInst << " " << srcOp << ", (" << ptrOp << ")\n";
            } else {
                std::string rax = "%rax";
                *os << "  movq " << ptrOp << ", " << rax << "\n";
                *os << "  " << movInst << " " << srcOp << ", (%rax)\n";
            }
        }
    }
}

void X64Architecture::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (i.getOperands().empty() || !i.getOperands()[0] || !i.getOperands()[0]->get()) {
            throw std::runtime_error("Vector instruction missing primary operand");
        }

        std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        std::string op1 = (i.getOperands().size() > 1 && i.getOperands()[1] && i.getOperands()[1]->get()) ?
                           cg.getValueAsOperand(i.getOperands()[1]->get()) : "";
        std::string dst = cg.getValueAsOperand(&i);

        // VGather / VScatter Handling
        if (i.getOpcode() == ir::Instruction::VGather) {
            std::string basePtr = op0;
            std::string indexVec = op1;
            std::string maskVec = (i.getOperands().size() > 2 && i.getOperands()[2]) ? cg.getValueAsOperand(i.getOperands()[2]->get()) : "%xmm15";
            if (abi == X64ABI::Windows) {
                *os << "  vpgatherdd " << dst << ", [" << basePtr << " + " << indexVec << " * 4], " << maskVec << "\n";
            } else {
                *os << "  vpgatherdd " << maskVec << ", (" << basePtr << ", " << indexVec << ", 4), " << dst << "\n";
            }
            return;
        }

        if (i.getOpcode() == ir::Instruction::VScatter) {
            std::string valVec = op0;
            std::string basePtr = op1;
            std::string indexVec = (i.getOperands().size() > 2 && i.getOperands()[2]) ? cg.getValueAsOperand(i.getOperands()[2]->get()) : "";
            if (abi == X64ABI::Windows) {
                *os << "  vpscatterdd [" << basePtr << " + " << indexVec << " * 4], " << valVec << "\n";
            } else {
                *os << "  vpscatterdd " << valVec << ", (" << basePtr << ", " << indexVec << ", 4)\n";
            }
            return;
        }

        // VInsert handling
        if (i.getOpcode() == ir::Instruction::VInsert) {
            if (i.getOperands().size() < 3 || !i.getOperands()[0] || !i.getOperands()[1] || !i.getOperands()[2]) {
                throw std::runtime_error("VInsert requires vector, scalar, and lane index operands");
            }

            auto* vecVal = i.getOperands()[0]->get();
            auto* valVal = i.getOperands()[1]->get();
            auto* idxVal = i.getOperands()[2]->get();

            auto* constIdx = dynamic_cast<const ir::ConstantInt*>(idxVal);
            if (!constIdx) {
                throw std::runtime_error("VInsert requires constant lane index");
            }

            auto* vecType = dynamic_cast<const ir::VectorType*>(i.getType());
            if (!vecType || !vecType->getElementType()) {
                throw std::runtime_error("Invalid vector type for VInsert");
            }

            int idx = static_cast<int>(constIdx->getValue());
            int numElem = static_cast<int>(vecType->getNumElements());
            if (idx < 0 || idx >= numElem) {
                throw std::runtime_error("VInsert lane index out of bounds");
            }

            std::string opVal = cg.getValueAsOperand(valVal);
            bool inPlace = canUseInPlace(cg, i, vecVal);
            unsigned vectorBits = vecType->getBitWidth();
            std::string fullDst = vectorBits == 256 ? toYmmReg(dst) : dst;
            std::string fullSrc = vectorBits == 256 ? toYmmReg(op0) : op0;

            if (!inPlace && dst != op0) {
                if (abi == X64ABI::Windows) {
                    *os << "  " << (vectorBits == 256 ? "vmovdqu " : "movdqu ") << fullDst << ", " << fullSrc << "\n";
                } else {
                    *os << "  " << (vectorBits == 256 ? "vmovdqu " : "movdqu ") << fullSrc << ", " << fullDst << "\n";
                }
            }

            auto* elemTy = vecType->getElementType();
            unsigned halfElements = 128 / (elemTy->getSize() * 8);
            bool upperHalf = vectorBits == 256 && static_cast<unsigned>(idx) >= halfElements;
            int laneIndex = upperHalf ? idx - static_cast<int>(halfElements) : idx;
            std::string laneDst = dst;
            if (upperHalf) {
                laneDst = abi == X64ABI::Windows ? "xmm15" : "%xmm15";
                if (abi == X64ABI::Windows) *os << "  vextracti128 " << laneDst << ", " << fullDst << ", 1\n";
                else *os << "  vextracti128 $1, " << fullDst << ", " << laneDst << "\n";
            }
            if (elemTy->isIntegerTy()) {
                auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
                unsigned bw = intTy ? intTy->getBitwidth() : 32;

                bool isImm = (!opVal.empty() && opVal[0] == '$');
                std::string reg32 = isImm ? "%eax" : to32BitReg(opVal);
                std::string reg64 = isImm ? "%rax" : to64BitReg(opVal);

                if (isImm) {
                    if (bw == 64) {
                        if (abi == X64ABI::Windows) {
                            *os << "  mov rax, " << opVal.substr(1) << "\n";
                        } else {
                            *os << "  movabsq " << opVal << ", %rax\n";
                        }
                    } else {
                        if (abi == X64ABI::Windows) {
                            *os << "  mov eax, " << opVal.substr(1) << "\n";
                        } else {
                            *os << "  movl " << opVal << ", %eax\n";
                        }
                    }
                }

                if (abi == X64ABI::Windows) {
                    if (bw == 8) *os << "  pinsrb " << laneDst << ", " << reg32 << ", " << laneIndex << "\n";
                    else if (bw == 16) *os << "  pinsrw " << laneDst << ", " << reg32 << ", " << laneIndex << "\n";
                    else if (bw == 32) *os << "  pinsrd " << laneDst << ", " << reg32 << ", " << laneIndex << "\n";
                    else if (bw == 64) *os << "  pinsrq " << laneDst << ", " << reg64 << ", " << laneIndex << "\n";
                    else throw std::runtime_error("Unsupported integer bitwidth for VInsert");
                } else {
                    if (bw == 8) *os << "  pinsrb $" << laneIndex << ", " << reg32 << ", " << laneDst << "\n";
                    else if (bw == 16) *os << "  pinsrw $" << laneIndex << ", " << reg32 << ", " << laneDst << "\n";
                    else if (bw == 32) *os << "  pinsrd $" << laneIndex << ", " << reg32 << ", " << laneDst << "\n";
                    else if (bw == 64) *os << "  pinsrq $" << laneIndex << ", " << reg64 << ", " << laneDst << "\n";
                    else throw std::runtime_error("Unsupported integer bitwidth for VInsert");
                }
            } else if (elemTy->isFloatTy()) {
                std::string srcXmm = opVal;
                if (!isXmmRegisterName(opVal)) {
                    srcXmm = (abi == X64ABI::Windows) ? "xmm1" : "%xmm1";
                    if (abi == X64ABI::Windows) {
                        *os << "  movss xmm1, " << opVal << "\n";
                    } else {
                        if (isDirectGprRegister(opVal)) *os << "  movd " << to32BitReg(opVal) << ", %xmm1\n";
                        else *os << "  movss " << opVal << ", %xmm1\n";
                    }
                }

                if (abi == X64ABI::Windows) {
                    if (laneIndex == 0) {
                        *os << "  movss " << laneDst << ", " << srcXmm << "\n";
                    } else {
                        int imm = (laneIndex << 4);
                        *os << "  insertps " << laneDst << ", " << srcXmm << ", " << imm << "\n";
                    }
                } else {
                    if (laneIndex == 0) {
                        *os << "  movss " << srcXmm << ", " << laneDst << "\n";
                    } else {
                        int imm = (laneIndex << 4);
                        *os << "  insertps $" << imm << ", " << srcXmm << ", " << laneDst << "\n";
                    }
                }
            } else if (elemTy->isDoubleTy()) {
                std::string srcXmm = opVal;
                if (!isXmmRegisterName(opVal)) {
                    srcXmm = (abi == X64ABI::Windows) ? "xmm1" : "%xmm1";
                    if (abi == X64ABI::Windows) {
                        *os << "  movsd xmm1, " << opVal << "\n";
                    } else {
                        if (isDirectGprRegister(opVal)) *os << "  movq " << to64BitReg(opVal) << ", %xmm1\n";
                        else *os << "  movsd " << opVal << ", %xmm1\n";
                    }
                }

                if (abi == X64ABI::Windows) {
                    if (laneIndex == 0) {
                        *os << "  movsd " << laneDst << ", " << srcXmm << "\n";
                    } else if (laneIndex == 1) {
                        *os << "  movlhps " << laneDst << ", " << srcXmm << "\n";
                    }
                } else {
                    if (laneIndex == 0) {
                        *os << "  movsd " << srcXmm << ", " << laneDst << "\n";
                    } else if (laneIndex == 1) {
                        *os << "  movlhps " << srcXmm << ", " << laneDst << "\n";
                    }
                }
            } else {
                throw std::runtime_error("Unsupported element type for VInsert");
            }
            if (upperHalf) {
                if (abi == X64ABI::Windows) *os << "  vinserti128 " << fullDst << ", " << fullDst << ", " << laneDst << ", 1\n";
                else *os << "  vinserti128 $1, " << laneDst << ", " << fullDst << ", " << fullDst << "\n";
            }
            return;
        }

        // VExtract handling
        if (i.getOpcode() == ir::Instruction::VExtract) {
            auto* vecVal = i.getOperands()[0]->get();
            auto* idxVal = (i.getOperands().size() > 1 && i.getOperands()[1]) ? i.getOperands()[1]->get() : nullptr;
            auto* constIdx = dynamic_cast<const ir::ConstantInt*>(idxVal);
            if (!constIdx) {
                throw std::runtime_error("VExtract requires constant lane index");
            }

            auto* srcVecTy = dynamic_cast<const ir::VectorType*>(vecVal->getType());
            if (!srcVecTy || !srcVecTy->getElementType()) {
                throw std::runtime_error("Invalid source vector type for VExtract");
            }

            int idx = static_cast<int>(constIdx->getValue());
            int numElem = static_cast<int>(srcVecTy->getNumElements());
            if (idx < 0 || idx >= numElem) {
                throw std::runtime_error("VExtract lane index out of bounds");
            }

            auto* elemTy = srcVecTy->getElementType();
            unsigned extractHalfElements = 128 / (elemTy->getSize() * 8);
            if (srcVecTy->getBitWidth() == 256 && static_cast<unsigned>(idx) >= extractHalfElements) {
                std::string half = abi == X64ABI::Windows ? "xmm15" : "%xmm15";
                std::string fullSource = toYmmReg(op0);
                if (abi == X64ABI::Windows) *os << "  vextracti128 " << half << ", " << fullSource << ", 1\n";
                else *os << "  vextracti128 $1, " << fullSource << ", " << half << "\n";
                op0 = half;
                idx -= static_cast<int>(extractHalfElements);
            }
            if (elemTy->isIntegerTy()) {
                auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
                unsigned bw = intTy ? intTy->getBitwidth() : 32;

                bool directGpr = isDirectGprRegister(dst);
                std::string targetReg32 = directGpr ? to32BitReg(dst) : (abi == X64ABI::Windows ? "eax" : "%eax");
                std::string targetReg64 = directGpr ? to64BitReg(dst) : (abi == X64ABI::Windows ? "rax" : "%rax");

                if (abi == X64ABI::Windows) {
                    if (bw == 8) {
                        *os << "  pextrb " << targetReg32 << ", " << op0 << ", " << idx << "\n";
                        if (!directGpr) *os << "  mov " << dst << ", al\n";
                    } else if (bw == 16) {
                        *os << "  pextrw " << targetReg32 << ", " << op0 << ", " << idx << "\n";
                        if (!directGpr) *os << "  mov " << dst << ", ax\n";
                    } else if (bw == 32) {
                        if (idx == 0) {
                            *os << "  movd " << targetReg32 << ", " << op0 << "\n";
                        } else {
                            *os << "  pextrd " << targetReg32 << ", " << op0 << ", " << idx << "\n";
                        }
                        if (!directGpr) *os << "  mov " << dst << ", eax\n";
                    } else if (bw == 64) {
                        *os << "  pextrq " << targetReg64 << ", " << op0 << ", " << idx << "\n";
                        if (!directGpr) *os << "  mov " << dst << ", rax\n";
                    } else {
                        throw std::runtime_error("Unsupported bitwidth for integer VExtract");
                    }
                } else {
                    if (bw == 8) {
                        *os << "  pextrb $" << idx << ", " << op0 << ", " << targetReg32 << "\n";
                        if (!directGpr) *os << "  movb %al, " << dst << "\n";
                    } else if (bw == 16) {
                        *os << "  pextrw $" << idx << ", " << op0 << ", " << targetReg32 << "\n";
                        if (!directGpr) *os << "  movw %ax, " << dst << "\n";
                    } else if (bw == 32) {
                        if (idx == 0) {
                            *os << "  movd " << op0 << ", " << targetReg32 << "\n";
                        } else {
                            *os << "  pextrd $" << idx << ", " << op0 << ", " << targetReg32 << "\n";
                        }
                        if (!directGpr) *os << "  movl %eax, " << dst << "\n";
                    } else if (bw == 64) {
                        *os << "  pextrq $" << idx << ", " << op0 << ", " << targetReg64 << "\n";
                        if (!directGpr) *os << "  movq %rax, " << dst << "\n";
                    } else {
                        throw std::runtime_error("Unsupported bitwidth for integer VExtract");
                    }
                }
            } else if (elemTy->isFloatTy()) {
                bool directGpr = isDirectGprRegister(dst);
                bool directXmm = isXmmRegisterName(dst);
                std::string targetGpr32 = directGpr ? to32BitReg(dst) : (abi == X64ABI::Windows ? "eax" : "%eax");

                if (abi == X64ABI::Windows) {
                    if (directXmm) {
                        if (dst != op0) *os << "  movdqu " << dst << ", " << op0 << "\n";
                        if (idx > 0) *os << "  shufps " << dst << ", " << dst << ", " << idx << "\n";
                    } else if (directGpr) {
                        *os << "  movdqu xmm0, " << op0 << "\n";
                        if (idx > 0) *os << "  shufps xmm0, xmm0, " << idx << "\n";
                        *os << "  movd " << targetGpr32 << ", xmm0\n";
                    } else {
                        *os << "  movdqu xmm0, " << op0 << "\n";
                        if (idx > 0) *os << "  shufps xmm0, xmm0, " << idx << "\n";
                        *os << "  movss " << dst << ", xmm0\n";
                    }
                } else {
                    if (directXmm) {
                        if (dst != op0) *os << "  movdqu " << op0 << ", " << dst << "\n";
                        if (idx > 0) *os << "  shufps $" << idx << ", " << dst << ", " << dst << "\n";
                    } else if (directGpr) {
                        *os << "  movdqu " << op0 << ", %xmm0\n";
                        if (idx > 0) *os << "  shufps $" << idx << ", %xmm0, %xmm0\n";
                        *os << "  movd %xmm0, " << targetGpr32 << "\n";
                    } else {
                        *os << "  movdqu " << op0 << ", %xmm0\n";
                        if (idx > 0) *os << "  shufps $" << idx << ", %xmm0, %xmm0\n";
                        *os << "  movss %xmm0, " << dst << "\n";
                    }
                }
            } else if (elemTy->isDoubleTy()) {
                bool directGpr = isDirectGprRegister(dst);
                bool directXmm = isXmmRegisterName(dst);
                std::string targetGpr64 = directGpr ? to64BitReg(dst) : (abi == X64ABI::Windows ? "rax" : "%rax");

                if (abi == X64ABI::Windows) {
                    if (directXmm) {
                        if (idx == 0) {
                            if (dst != op0) *os << "  movdqu " << dst << ", " << op0 << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps " << dst << ", " << op0 << "\n";
                        }
                    } else if (directGpr) {
                        if (idx == 0) {
                            *os << "  movq " << targetGpr64 << ", " << op0 << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps xmm0, " << op0 << "\n";
                            *os << "  movq " << targetGpr64 << ", xmm0\n";
                        }
                    } else {
                        if (idx == 0) {
                            *os << "  movsd " << dst << ", " << op0 << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps xmm0, " << op0 << "\n";
                            *os << "  movsd " << dst << ", xmm0\n";
                        }
                    }
                } else {
                    if (directXmm) {
                        if (idx == 0) {
                            if (dst != op0) *os << "  movdqu " << op0 << ", " << dst << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps " << op0 << ", " << dst << "\n";
                        }
                    } else if (directGpr) {
                        if (idx == 0) {
                            *os << "  movq " << op0 << ", " << targetGpr64 << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps " << op0 << ", %xmm0\n";
                            *os << "  movq %xmm0, " << targetGpr64 << "\n";
                        }
                    } else {
                        if (idx == 0) {
                            *os << "  movsd " << op0 << ", " << dst << "\n";
                        } else if (idx == 1) {
                            *os << "  movhlps " << op0 << ", %xmm0\n";
                            *os << "  movsd %xmm0, " << dst << "\n";
                        }
                    }
                }
            } else {
                throw std::runtime_error("Unsupported element type for VExtract");
            }
            return;
        }

        auto* vecType = dynamic_cast<const ir::VectorType*>(i.getType());
        if (!vecType || !vecType->getElementType()) {
            throw std::runtime_error("Unsupported vector arithmetic type");
        }

        auto* elemTy = vecType->getElementType();
        unsigned numElem = vecType->getNumElements();
        std::string simdInst = "";

        // VBroadcast handling
        if (i.getOpcode() == ir::Instruction::VBroadcast) {
            if (elemTy->isIntegerTy()) {
                auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
                unsigned bw = intTy ? intTy->getBitwidth() : 32;
                if (bw == 32) {
                    unsigned bits = elemTy->getSize() * 8 * numElem;
                    if (bits == 256) {
                        std::string source = op0;
                        std::string dstYmm = toYmmReg(dst);
                        std::string scratchXmm = getReservedScratchVectorReg();
                        if (isXmmRegisterName(source)) {
                            *os << "  vpbroadcastd " << source << ", " << dstYmm << "\n";
                        } else {
                            if (!source.empty() && source[0] == '$') {
                                *os << "  movl " << source << ", %eax\n";
                                *os << "  vmovd %eax, " << scratchXmm << "\n";
                            } else {
                                std::string reg32 = to32BitReg(source);
                                *os << "  vmovd " << reg32 << ", " << scratchXmm << "\n";
                            }
                            *os << "  vpbroadcastd " << scratchXmm << ", " << dstYmm << "\n";
                        }
                    } else {
                        if (!op0.empty() && op0[0] == '$') {
                            *os << "  movl " << op0 << ", %eax\n";
                            *os << "  movd %eax, " << dst << "\n";
                        } else {
                            *os << "  movd " << op0 << ", " << dst << "\n";
                        }
                        *os << "  pshufd $0, " << dst << ", " << dst << "\n";
                    }
                } else if (bw == 64) {
                    std::string dstYmm = toYmmReg(dst);
                    if (!op0.empty() && op0[0] == '$') {
                        uint64_t val = 0;
                        if (op0.size() > 1) val = std::stoull(op0.substr(1));
                        if (val == 0) {
                            if (numElem == 4 || numElem == 8) {
                                *os << "  vpxor " << dstYmm << ", " << dstYmm << ", " << dstYmm << "\n";
                            } else {
                                *os << "  pxor " << dst << ", " << dst << "\n";
                            }
                        } else {
                            std::string scratchXmm = getReservedScratchVectorReg();
                            *os << "  movq " << op0 << ", %rax\n";
                            *os << "  movq %rax, " << scratchXmm << "\n";
                            if (numElem == 4) {
                                *os << "  vpbroadcastq " << scratchXmm << ", " << dstYmm << "\n";
                            } else {
                                *os << "  punpcklqdq " << scratchXmm << ", " << dst << "\n";
                            }
                        }
                    } else if (isXmmRegisterName(op0)) {
                        if (numElem == 4) {
                            *os << "  vpbroadcastq " << op0 << ", " << dstYmm << "\n";
                        } else {
                            if (dst != op0) *os << "  movdqu " << op0 << ", " << dst << "\n";
                            *os << "  punpcklqdq " << dst << ", " << dst << "\n";
                        }
                    } else {
                        std::string reg64 = to64BitReg(op0);
                        std::string scratchXmm = getReservedScratchVectorReg();
                        *os << "  movq " << reg64 << ", " << scratchXmm << "\n";
                        if (numElem == 4) {
                            *os << "  vpbroadcastq " << scratchXmm << ", " << dstYmm << "\n";
                        } else {
                            if (dst != scratchXmm) *os << "  movdqu " << scratchXmm << ", " << dst << "\n";
                            *os << "  punpcklqdq " << dst << ", " << dst << "\n";
                        }
                    }
                } else if (bw == 16) {
                    *os << "  movd " << op0 << ", " << dst << "\n";
                    *os << "  pshuflw $0, " << dst << ", " << dst << "\n";
                    *os << "  punpcklqdq " << dst << ", " << dst << "\n";
                } else if (bw == 8) {
                    *os << "  movd " << op0 << ", " << dst << "\n";
                    *os << "  punpcklbw " << dst << ", " << dst << "\n";
                    *os << "  pshuflw $0, " << dst << ", " << dst << "\n";
                    *os << "  punpcklqdq " << dst << ", " << dst << "\n";
                } else {
                    throw std::runtime_error("Unsupported integer bitwidth for VBroadcast");
                }
            } else if (elemTy->isFloatTy()) {
                if (isXmmRegisterName(op0)) {
                    if (dst != op0) *os << "  movdqu " << op0 << ", " << dst << "\n";
                    *os << "  shufps $0, " << dst << ", " << dst << "\n";
                } else if (isDirectGprRegister(op0)) {
                    *os << "  movd " << op0 << ", " << dst << "\n";
                    *os << "  shufps $0, " << dst << ", " << dst << "\n";
                } else {
                    *os << "  movdqu " << op0 << ", " << dst << "\n";
                    *os << "  shufps $0, " << dst << ", " << dst << "\n";
                }
            } else if (elemTy->isDoubleTy()) {
                if (isXmmRegisterName(op0)) {
                    *os << "  movddup " << op0 << ", " << dst << "\n";
                } else if (isDirectGprRegister(op0)) {
                    *os << "  movq " << op0 << ", " << dst << "\n";
                    *os << "  movddup " << dst << ", " << dst << "\n";
                } else {
                    *os << "  movdqu " << op0 << ", " << dst << "\n";
                    *os << "  movddup " << dst << ", " << dst << "\n";
                }
            } else {
                throw std::runtime_error("Unsupported vector type for VBroadcast");
            }
            return;
        }

        // FMA 3-operand instructions (Native FMA3 AVX)
        if (i.getOpcode() == ir::Instruction::FMA || i.getOpcode() == ir::Instruction::FMS ||
            i.getOpcode() == ir::Instruction::FNMA || i.getOpcode() == ir::Instruction::FNMS) {
            if (i.getOperands().size() < 3) throw std::runtime_error("FMA requires 3 operands");
            std::string op2 = cg.getValueAsOperand(i.getOperands()[2]->get());
            std::string fmaBase = "";
            if (i.getOpcode() == ir::Instruction::FMA) fmaBase = "vfmadd213";
            else if (i.getOpcode() == ir::Instruction::FMS) fmaBase = "vfmsub213";
            else if (i.getOpcode() == ir::Instruction::FNMA) fmaBase = "vfnmadd213";
            else if (i.getOpcode() == ir::Instruction::FNMS) fmaBase = "vfnmsub213";

            std::string suffix = elemTy->isFloatTy() ? "ps" : "pd";
            std::string fmaInst = fmaBase + suffix;

            unsigned totalBitWidth = elemTy->getSize() * 8 * numElem;
            std::string fullDst = totalBitWidth == 256 ? toYmmReg(dst) : dst;
            std::string fullOp0 = totalBitWidth == 256 ? toYmmReg(op0) : op0;
            std::string fullOp1 = totalBitWidth == 256 ? toYmmReg(op1) : op1;
            std::string fullOp2 = totalBitWidth == 256 ? toYmmReg(op2) : op2;

            if (fullDst != fullOp0) {
                *os << "  " << (totalBitWidth == 256 ? "vmovaps " : "movaps ") << fullOp0 << ", " << fullDst << "\n";
            }
            if (abi == X64ABI::Windows) {
                *os << "  " << fmaInst << " " << fullDst << ", " << fullOp1 << ", " << fullOp2 << "\n";
            } else {
                *os << "  " << fmaInst << " " << fullOp2 << ", " << fullOp1 << ", " << fullDst << "\n";
            }
            return;
        }


        // Vector Shuffle Handling
        if (i.getOpcode() == ir::Instruction::VShuffle) {
            auto* mask = i.getShuffleMask();
            std::string src0 = toYmmReg(op0);
            std::string dstYmm = toYmmReg(dst);
            if (mask && mask->indices.size() == 8 && mask->indices[0] == 4 && mask->indices[1] == 5 && mask->indices[2] == 6 && mask->indices[3] == 7) {
                *os << "  vperm2i128 $0x01, " << src0 << ", " << src0 << ", " << dstYmm << "\n";
                return;
            }
        }

        // Vector Conversion Handling
        if (i.getOpcode() == ir::Instruction::VSExt || i.getOpcode() == ir::Instruction::VZExt || i.getOpcode() == ir::Instruction::VTrunc) {
            auto* srcVecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
            auto* dstVecTy = dynamic_cast<const ir::VectorType*>(i.getType());
            if (!srcVecTy || !dstVecTy) throw std::runtime_error("Invalid vector conversion types");
            auto* srcElemTy = dynamic_cast<const ir::IntegerType*>(srcVecTy->getElementType());
            auto* dstElemTy = dynamic_cast<const ir::IntegerType*>(dstVecTy->getElementType());
            if (!srcElemTy || !dstElemTy) throw std::runtime_error("Invalid vector conversion integer element types");

            unsigned srcBw = srcElemTy->getBitwidth();
            unsigned dstBw = dstElemTy->getBitwidth();
            std::string srcReg = isXmmRegisterName(op0) ? op0 : "%xmm0";
            std::string dstYmm = toYmmReg(dst);

            if (i.getOpcode() == ir::Instruction::VSExt && srcBw == 32 && dstBw == 64) {
                if (!isXmmRegisterName(op0)) {
                    *os << "  vmovdqu " << op0 << ", %xmm0\n";
                }
                *os << "  vpmovsxdq " << srcReg << ", " << dstYmm << "\n";
            } else if (i.getOpcode() == ir::Instruction::VZExt && srcBw == 32 && dstBw == 64) {
                if (!isXmmRegisterName(op0)) {
                    *os << "  vmovdqu " << op0 << ", %xmm0\n";
                }
                *os << "  vpmovzxdq " << srcReg << ", " << dstYmm << "\n";
            } else {
                throw std::runtime_error("Unsupported vector conversion lowering");
            }
            return;
        }

        // Binary vector opcodes selection
        if (i.getOpcode() == ir::Instruction::VAnd) {
            simdInst = "pand";
        } else if (i.getOpcode() == ir::Instruction::VOr) {
            simdInst = "por";
        } else if (i.getOpcode() == ir::Instruction::VXor) {
            simdInst = "pxor";
        } else if (i.getOpcode() == ir::Instruction::VShl && elemTy->isIntegerTy()) {
            auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
            unsigned bw = intTy ? intTy->getBitwidth() : 32;
            simdInst = (bw == 16) ? "psllw" : "pslld";
        } else if (i.getOpcode() == ir::Instruction::VShr && elemTy->isIntegerTy()) {
            auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
            unsigned bw = intTy ? intTy->getBitwidth() : 32;
            simdInst = (bw == 16) ? "psrlw" : "psrld";
        } else if (i.getOpcode() == ir::Instruction::VMin) {
            simdInst = (elemTy->getSize() == 1) ? "pminub" : ((elemTy->getSize() == 2) ? "pminsw" : "pminsd");
        } else if (i.getOpcode() == ir::Instruction::VMax) {
            simdInst = (elemTy->getSize() == 1) ? "pmaxub" : ((elemTy->getSize() == 2) ? "pmaxsw" : "pmaxsd");
        } else if (i.getOpcode() == ir::Instruction::VFMin) {
            simdInst = (elemTy->isFloatTy()) ? "minps" : "minpd";
        } else if (i.getOpcode() == ir::Instruction::VFMax) {
            simdInst = (elemTy->isFloatTy()) ? "maxps" : "maxpd";
        } else if (i.getOpcode() == ir::Instruction::VHAdd) {
            simdInst = (elemTy->isFloatTy()) ? "haddps" : ((elemTy->isDoubleTy()) ? "haddpd" : "phaddd");
        } else if (i.getOpcode() == ir::Instruction::VCmp) {
            if (i.getOperands().size() != 3)
                throw std::runtime_error("VCmp requires two vectors and a predicate");
            auto* predicate = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get());
            if (!predicate) throw std::runtime_error("VCmp predicate must be constant");
            const auto pred = static_cast<ir::VectorCompareOp>(predicate->getValue());
            auto* comparedType = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
            if (!comparedType) throw std::runtime_error("VCmp operands must be vectors");
            const ir::Type* comparedElemTy = comparedType->getElementType();
            const unsigned totalBits = comparedElemTy->getSize() * 8 * numElem;
            std::string lhs = totalBits == 256 ? toYmmReg(op0) : op0;
            std::string rhs = totalBits == 256 ? toYmmReg(op1) : op1;
            std::string out = totalBits == 256 ? toYmmReg(dst) : dst;
            if (comparedElemTy->isIntegerTy() && comparedElemTy->getSize() == 4) {
                if (pred == ir::VectorCompareOp::EQ)
                    *os << "  vpcmpeqd " << rhs << ", " << lhs << ", " << out << "\n";
                else if (pred == ir::VectorCompareOp::GT)
                    *os << "  vpcmpgtd " << rhs << ", " << lhs << ", " << out << "\n";
                else if (pred == ir::VectorCompareOp::LT)
                    *os << "  vpcmpgtd " << lhs << ", " << rhs << ", " << out << "\n";
                else
                    throw std::runtime_error("Unsupported signed i32 vector comparison predicate");
            } else if (comparedElemTy->isFloatTy() || comparedElemTy->isDoubleTy()) {
                unsigned immediate = 0;
                bool swap = false;
                switch (pred) {
                    case ir::VectorCompareOp::EQ: immediate = 0; break;  // ordered, quiet
                    case ir::VectorCompareOp::LT: immediate = 1; break;
                    case ir::VectorCompareOp::LE: immediate = 2; break;
                    case ir::VectorCompareOp::NE: immediate = 12; break; // ordered, quiet
                    case ir::VectorCompareOp::GT: immediate = 1; swap = true; break;
                    case ir::VectorCompareOp::GE: immediate = 2; swap = true; break;
                    default: throw std::runtime_error("Unsupported floating vector comparison predicate");
                }
                const char* mnemonic = comparedElemTy->isFloatTy() ? "vcmpps" : "vcmppd";
                *os << "  " << mnemonic << " $" << immediate << ", "
                    << (swap ? lhs : rhs) << ", " << (swap ? rhs : lhs)
                    << ", " << out << "\n";
            } else {
                throw std::runtime_error("Unsupported VCmp element type");
            }
            return;
        } else if (i.getOpcode() == ir::Instruction::VSelect) {
            if (i.getOperands().size() != 3)
                throw std::runtime_error("VSelect requires mask, true, and false vectors");
            std::string trueValue = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string falseValue = cg.getValueAsOperand(i.getOperands()[2]->get());
            const unsigned totalBits = elemTy->getSize() * 8 * numElem;
            if (totalBits == 256) {
                op0 = toYmmReg(op0); trueValue = toYmmReg(trueValue);
                falseValue = toYmmReg(falseValue); dst = toYmmReg(dst);
            }
            // IR masks are lane-wise all-zero/all-one bit vectors.  AVX
            // blendv observes each lane's sign bit, which is therefore an
            // exact target-specific realization of VSelect's abstract mask.
            const char* mnemonic = elemTy->isDoubleTy() ? "vblendvpd" :
                                   (elemTy->isFloatTy() ? "vblendvps" : "vpblendvb");
            if (abi == X64ABI::Windows) {
                *os << "  " << mnemonic << " " << dst << ", " << falseValue << ", " << trueValue << ", " << op0 << "\n";
            } else {
                *os << "  " << mnemonic << " " << op0 << ", " << trueValue << ", " << falseValue << ", " << dst << "\n";
            }
            return;
        } else if (elemTy->isIntegerTy()) {
            auto* intTy = dynamic_cast<const ir::IntegerType*>(elemTy);
            if (!intTy) throw std::runtime_error("Invalid integer vector element type");
            unsigned bw = intTy->getBitwidth();

            if (bw == 8 && (numElem == 16 || numElem == 32 || numElem == 64)) {
                if (i.getOpcode() == ir::Instruction::VAdd) simdInst = "paddb";
                else if (i.getOpcode() == ir::Instruction::VSub) simdInst = "psubb";
                else throw std::runtime_error("Unsupported vector instruction for i8");
            } else if (bw == 16 && (numElem == 8 || numElem == 16 || numElem == 32)) {
                if (i.getOpcode() == ir::Instruction::VAdd) simdInst = "paddw";
                else if (i.getOpcode() == ir::Instruction::VSub) simdInst = "psubw";
                else if (i.getOpcode() == ir::Instruction::VMul) simdInst = "pmullw";
                else throw std::runtime_error("Unsupported vector instruction for i16");
            } else if (bw == 32 && (numElem == 4 || numElem == 8 || numElem == 16)) {
                if (i.getOpcode() == ir::Instruction::VAdd) simdInst = "paddd";
                else if (i.getOpcode() == ir::Instruction::VSub) simdInst = "psubd";
                else if (i.getOpcode() == ir::Instruction::VMul) simdInst = "pmulld";
                else throw std::runtime_error("Unsupported vector instruction for i32");
            } else if (bw == 64 && (numElem == 2 || numElem == 4 || numElem == 8)) {
                if (i.getOpcode() == ir::Instruction::VAdd) simdInst = "paddq";
                else if (i.getOpcode() == ir::Instruction::VSub) simdInst = "psubq";
                else throw std::runtime_error("Unsupported vector instruction for i64");
            } else {
                throw std::runtime_error("Unsupported integer vector type for arithmetic emission");
            }
        } else if (elemTy->isFloatTy() && (numElem == 4 || numElem == 8 || numElem == 16)) {
            if (i.getOpcode() == ir::Instruction::VFAdd) simdInst = "addps";
            else if (i.getOpcode() == ir::Instruction::VFSub) simdInst = "subps";
            else if (i.getOpcode() == ir::Instruction::VFMul) simdInst = "mulps";
            else if (i.getOpcode() == ir::Instruction::VFDiv) simdInst = "divps";
            else throw std::runtime_error("Unsupported floating-point vector instruction for f32");
        } else if (elemTy->isDoubleTy() && (numElem == 2 || numElem == 4 || numElem == 8)) {
            if (i.getOpcode() == ir::Instruction::VFAdd) simdInst = "addpd";
            else if (i.getOpcode() == ir::Instruction::VFSub) simdInst = "subpd";
            else if (i.getOpcode() == ir::Instruction::VFMul) simdInst = "mulpd";
            else if (i.getOpcode() == ir::Instruction::VFDiv) simdInst = "divpd";
            else throw std::runtime_error("Unsupported floating-point vector instruction for f64");
        } else {
            throw std::runtime_error("Unsupported vector type for arithmetic emission");
        }

        if (simdInst.empty()) throw std::runtime_error("Unrecognized SIMD instruction opcode");

        bool isCommutative = (i.getOpcode() == ir::Instruction::VAdd || i.getOpcode() == ir::Instruction::VMul ||
                              i.getOpcode() == ir::Instruction::VFAdd || i.getOpcode() == ir::Instruction::VFMul ||
                              i.getOpcode() == ir::Instruction::VAnd || i.getOpcode() == ir::Instruction::VOr ||
                              i.getOpcode() == ir::Instruction::VXor);

        unsigned totalBitWidth = elemTy->getSize() * 8 * numElem;
        bool op0IsMem = !isXmmRegisterName(op0) && !isYmmRegisterName(op0);
        bool op1IsMem = !isXmmRegisterName(op1) && !isYmmRegisterName(op1);
        bool dstIsMem = !isXmmRegisterName(dst) && !isYmmRegisterName(dst);

        std::string scratchVec = (abi == X64ABI::Windows) ? "xmm5" : "%xmm15";
        std::string scratchYmm = (abi == X64ABI::Windows) ? "ymm5" : "%ymm15";

        if (totalBitWidth == 256) {
            std::string realDst = dstIsMem ? scratchYmm : toYmmReg(dst);
            std::string realOp0 = op0;
            std::string realOp1 = op1;

            if (op0IsMem && op1IsMem) {
                *os << "  vmovdqu " << toYmmReg(realOp0) << ", " << scratchYmm << "\n";
                realOp0 = scratchYmm;
            } else if (op0IsMem && isCommutative) {
                std::swap(realOp0, realOp1);
            } else if (op0IsMem) {
                *os << "  vmovdqu " << toYmmReg(realOp0) << ", " << scratchYmm << "\n";
                realOp0 = scratchYmm;
            }

            realOp0 = toYmmReg(realOp0);
            realOp1 = toYmmReg(realOp1);

            if (!simdInst.empty() && simdInst[0] != 'v') simdInst = "v" + simdInst;
            if (abi == X64ABI::Windows) {
                *os << "  " << simdInst << " " << realDst << ", " << realOp0 << ", " << realOp1 << "\n";
            } else {
                *os << "  " << simdInst << " " << realOp1 << ", " << realOp0 << ", " << realDst << "\n";
            }

            if (dstIsMem) {
                *os << "  vmovdqu " << scratchYmm << ", " << toYmmReg(dst) << "\n";
            }
        } else if (totalBitWidth == 512) {
            dst = toZmmReg(dst);
            op0 = toZmmReg(op0);
            op1 = toZmmReg(op1);
            if (!simdInst.empty() && simdInst[0] != 'v') simdInst = "v" + simdInst;
            if (dst == op0) {
                *os << "  " << simdInst << " " << op1 << ", " << dst << "\n";
            } else if (dst == op1 && isCommutative) {
                *os << "  " << simdInst << " " << op0 << ", " << dst << "\n";
            } else {
                *os << "  vmovdqu64 " << op0 << ", " << dst << "\n";
                *os << "  " << simdInst << " " << op1 << ", " << dst << "\n";
            }
        } else {
            std::string realDst = dstIsMem ? scratchVec : dst;
            std::string realOp0 = op0;
            std::string realOp1 = op1;

            if (realDst != realOp0) {
                if (isCommutative && realDst == realOp1) {
                    std::swap(realOp0, realOp1);
                } else {
                    *os << "  movdqu " << realOp0 << ", " << realDst << "\n";
                }
            }
            *os << "  " << simdInst << " " << realOp1 << ", " << realDst << "\n";
            if (dstIsMem) {
                *os << "  movdqu " << scratchVec << ", " << dst << "\n";
            }
        }
    }
}

std::string ComplexAddress::format(X64ABI abi) const {
    if (!isValid) return "";
    std::string res;
    if (abi == X64ABI::SystemV) {
        if (disp != 0) res += std::to_string(disp);
        res += "(";
        if (!base.empty()) res += base;
        if (!index.empty()) {
            if (!base.empty()) res += ", ";
            res += index;
            if (scale > 1) res += ", " + std::to_string(scale);
        }
        res += ")";
    } else {
        res = "[";
        bool hasBase = false;
        if (!base.empty()) { res += base; hasBase = true; }
        if (!index.empty()) {
            if (hasBase) res += " + ";
            res += index;
            if (scale > 1) res += " * " + std::to_string(scale);
            hasBase = true;
        }
        if (disp != 0 || !hasBase) {
            if (disp > 0) {
                if (hasBase) res += " + ";
                res += std::to_string(disp);
            } else if (disp < 0) {
                if (hasBase) res += " - ";
                res += std::to_string(-disp);
            } else if (!hasBase) {
                res += "0";
            }
        }
        res += "]";
    }
    return res;
}

ComplexAddress X64Architecture::matchComplexAddress(CodeGen& cg, ir::Value* val) const {
    ComplexAddress result;
    if (!val) return result;

    auto unwrapExt = [](ir::Value* v) -> ir::Value* {
        if (!v) return v;
        while (auto* inst = dynamic_cast<ir::Instruction*>(v)) {
            if (inst->getOpcode() == ir::Instruction::ExtSW || inst->getOpcode() == ir::Instruction::ExtUW) {
                if (!inst->getOperands().empty() && inst->getOperands()[0] && inst->getOperands()[0]->get()) {
                    v = inst->getOperands()[0]->get();
                } else break;
            } else break;
        }
        return v;
    };

    auto* inst = dynamic_cast<ir::Instruction*>(val);
    if (!inst) return result;

    int64_t disp = 0;
    ir::Value* coreAddr = val;

    if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
            disp = static_cast<int64_t>(c1->getValue());
            coreAddr = op0;
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
            disp = static_cast<int64_t>(c0->getValue());
            coreAddr = op1;
        }
    }

    ir::Value* baseVal = nullptr;
    ir::Value* indexVal = nullptr;
    int scale = 1;

    auto* coreInst = dynamic_cast<ir::Instruction*>(coreAddr);
    if (coreInst && coreInst->getOpcode() == ir::Instruction::Add && coreInst->getOperands().size() == 2) {
        ir::Value* op0 = coreInst->getOperands()[0]->get();
        ir::Value* op1 = coreInst->getOperands()[1]->get();

        auto* mul0 = dynamic_cast<ir::Instruction*>(unwrapExt(op0));
        auto* mul1 = dynamic_cast<ir::Instruction*>(unwrapExt(op1));

        if (mul1 && mul1->getOpcode() == ir::Instruction::Mul && mul1->getOperands().size() == 2) {
            baseVal = op0;
            ir::Value* m0 = mul1->getOperands()[0]->get();
            ir::Value* m1 = mul1->getOperands()[1]->get();
            if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
                indexVal = m0; scale = static_cast<int>(c1->getValue());
            } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
                indexVal = m1; scale = static_cast<int>(c0->getValue());
            }
        } else if (mul0 && mul0->getOpcode() == ir::Instruction::Mul && mul0->getOperands().size() == 2) {
            baseVal = op1;
            ir::Value* m0 = mul0->getOperands()[0]->get();
            ir::Value* m1 = mul0->getOperands()[1]->get();
            if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
                indexVal = m0; scale = static_cast<int>(c1->getValue());
            } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
                indexVal = m1; scale = static_cast<int>(c0->getValue());
            }
        } else {
            baseVal = op0;
            indexVal = op1;
            scale = 1;
        }
    } else if (coreInst && coreInst->getOpcode() == ir::Instruction::Mul && coreInst->getOperands().size() == 2) {
        ir::Value* m0 = coreInst->getOperands()[0]->get();
        ir::Value* m1 = coreInst->getOperands()[1]->get();
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
            indexVal = m0; scale = static_cast<int>(c1->getValue());
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
            indexVal = m1; scale = static_cast<int>(c0->getValue());
        }
    }

    if (scale != 1 && scale != 2 && scale != 4 && scale != 8) return result;

    indexVal = unwrapExt(indexVal);
    baseVal = unwrapExt(baseVal);

    std::string bStr = baseVal ? cg.getValueAsOperand(baseVal) : "";
    std::string iStr = indexVal ? cg.getValueAsOperand(indexVal) : "";

    auto isReg = [this](const std::string& s) {
        if (s.empty()) return true;
        if (abi == X64ABI::Windows) {
            return s[0] != '[' && s[0] != '$' && s[0] != '-' && s[0] != '+';
        }
        return s[0] == '%';
    };

    if (!isReg(bStr) || !isReg(iStr)) return result;
    if (bStr.empty() && iStr.empty()) return result;

    if (abi == X64ABI::SystemV) {
        if (!bStr.empty() && bStr[0] == '%') bStr = to64BitReg(bStr);
        if (!iStr.empty() && iStr[0] == '%') iStr = to64BitReg(iStr);
    } else {
        if (!bStr.empty()) bStr = to64BitReg(bStr);
        if (!iStr.empty()) iStr = to64BitReg(iStr);
    }

    result.base = bStr;
    result.index = iStr;
    result.scale = scale;
    result.disp = disp;
    result.isValid = true;
    return result;
}

std::string X64Architecture::getRegisterName(const std::string& base, const ir::Type* type) const {
    std::string b = base;
    if (b[0] == '%') b = b.substr(1);

    if (type && type->isInteger()) {
        auto* it = dynamic_cast<const ir::IntegerType*>(type);
        if (it && it->getBitwidth() <= 32) {
            if (b == "rax") b = "eax";
            else if (b == "rcx") b = "ecx";
            else if (b == "rdx") b = "edx";
            else if (b == "rbx") b = "ebx";
            else if (b == "rsi") b = "esi";
            else if (b == "rdi") b = "edi";
            else if (b == "r8") b = "r8d";
            else if (b == "r9") b = "r9d";
            else if (b == "r10") b = "r10d";
            else if (b == "r11") b = "r11d";
            else if (b == "r12") b = "r12d";
            else if (b == "r13") b = "r13d";
            else if (b == "r14") b = "r14d";
            else if (b == "r15") b = "r15d";
        }
    }

    if (abi == X64ABI::SystemV) {
        return "%" + b;
    }
    return b;
}

// Helpers
void X64Architecture::emitRegMem(asm_::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
    if (rex) as.emitByte(rex);
    as.emitByte(opcode);
    if (offset >= -128 && offset <= 127) { as.emitByte(0x45 | (reg << 3)); as.emitByte((uint8_t)offset); }
    else { as.emitByte(0x85 | (reg << 3)); as.emitDWord(offset); }
}

void X64Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& as, ir::Value* v, uint8_t regIdx) {
    if (!v) { uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(0); return; }
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) { uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(ci->getValue()); }
    else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(v)) {
        uint64_t bits = 0;
        double val = cfp->getValue();
        std::memcpy(&bits, &val, sizeof(double));
        uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(bits);
    }
    else if (v->getName() == "__heap_ptr" || v->getName() == "heap_ptr") {
        uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; as.emitByte(rex); as.emitByte(0x8B); as.emitByte(0x05 | ((regIdx & 7) << 3));
        uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, v->getName(), ".text"});
    } else if (dynamic_cast<ir::GlobalVariable*>(v) || dynamic_cast<ir::GlobalValue*>(v)) {
        uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; as.emitByte(rex); as.emitByte(0x8D); as.emitByte(0x05 | ((regIdx & 7) << 3));
        uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, v->getName(), ".text"});
    } else if (auto* param = dynamic_cast<ir::Parameter*>(v)) {
        size_t idx = 0;
        if (cg.getCurrentFunction()) {
            for (auto& p : cg.getCurrentFunction()->getParameters()) {
                if (p.get() == param) break;
                idx++;
            }
        }
        if (idx < integerArgRegs.size()) {
            uint8_t srcRegIdx = getArchRegIndex(integerArgRegs[idx]);
            if (srcRegIdx != regIdx) {
                uint8_t rex = (regIdx >= 8 || srcRegIdx >= 8) ? 0x4C : 0x48;
                as.emitByte(rex); as.emitByte(0x89);
                as.emitByte(0xC0 | ((srcRegIdx & 7) << 3) | (regIdx & 7));
            }
        } else {
            int32_t offset = 16 + (idx - 6) * 8;
            uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(as, rex, 0x8B, regIdx & 7, offset);
        }
    } else {
        int32_t offset = cg.getStackOffset(v); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(as, rex, 0x8B, regIdx & 7, offset);
    }
}

void X64Architecture::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx) {
    int32_t offset = cg.getStackOffset(&instr); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(cg.getAssembler(), rex, 0x89, regIdx & 7, offset);
}

uint8_t X64Architecture::getRex(const ir::Type* t) { if (!t || t->isPointerTy()) return 0x48; if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 64) return 0x48; } return 0; }
uint8_t X64Architecture::getOpcode(uint8_t baseOp, const ir::Type* t) { if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 8) return baseOp - 1; } return baseOp; }
uint8_t X64Architecture::getArchRegIndex(const std::string& regName) {
    static std::map<std::string, uint8_t> regToIdx = {{"rax",0}, {"rcx",1}, {"rdx",2}, {"rbx",3}, {"rsp",4}, {"rbp",5}, {"rsi",6}, {"rdi",7}, {"r8",8}, {"r9",9}, {"r10",10}, {"r11",11}, {"r12",12}, {"r13",13}, {"r14",14}, {"r15",15}};
    std::string name = regName; if (name[0] == '%') name = name.substr(1);
    auto it = regToIdx.find(name); return (it != regToIdx.end()) ? it->second : 0;
}

void X64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void X64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
