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

static void emitMov(CodeGen& cg, std::ostream* os, const std::string& src, const std::string& dst, bool is32) {
    if (!os) return;
    if (is32BitRegisterName(src) || is32BitRegisterName(dst)) is32 = true;
    std::string regRax = is32 ? "%eax" : "%rax";
    std::string s = src.empty() ? regRax : src;
    std::string d = dst.empty() ? regRax : dst;

    if (!s.empty() && s[0] == '%') s = is32 ? to32BitReg(s) : to64BitReg(s);
    if (!d.empty() && d[0] == '%') d = is32 ? to32BitReg(d) : to64BitReg(d);
    if (s == d) return;

    if (!cg.lastStoreOp.empty() && s == cg.lastStoreOp && (d == regRax || d == "%rax" || d == "%eax")) {
        std::cerr << "SKIPPED emitMov: s=" << s << " d=" << d << " lastStoreOp=" << cg.lastStoreOp << "\n";
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

void X64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (abi == X64ABI::SystemV) {
        bool makesCalls = false;
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                auto opc = instr->getOpcode();
                if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                    makesCalls = true;
                    break;
                }
            }
            if (makesCalls) break;
        }

        std::vector<std::string> usedCalleeRegs;
        static const std::vector<std::string> calleeList = {"rbx", "r12", "r13", "r14", "r15"};
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->hasPhysicalRegister()) {
                    size_t regIdx = instr->getPhysicalRegister();
                    if (regIdx < integerRegs.size()) {
                        const std::string& regName = integerRegs[regIdx];
                        if (std::find(calleeList.begin(), calleeList.end(), regName) != calleeList.end()) {
                            if (std::find(usedCalleeRegs.begin(), usedCalleeRegs.end(), regName) == usedCalleeRegs.end()) {
                                usedCalleeRegs.push_back(regName);
                            }
                        }
                    }
                }
            }
        }

        int current_offset = -8 - 8 * (int)usedCalleeRegs.size();
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    if (func.hasStackSlot(instr.get())) {
                        cg.getStackOffsets()[instr.get()] = -8 - 8 * (int)usedCalleeRegs.size() - func.getStackSlotForVreg(instr.get());
                    } else if (!instr->hasPhysicalRegister()) {
                        cg.getStackOffsets()[instr.get()] = current_offset;
                        current_offset -= 8;
                    }
                }
            }
        }
        int stack_alloc = std::abs(current_offset + 8 + 8 * (int)usedCalleeRegs.size());

        bool isZeroFrame = (!makesCalls && stack_alloc == 0 && usedCalleeRegs.empty());

        if (auto* os = cg.getTextStream()) {
            *os << "  .cfi_startproc\n";
            if (!isZeroFrame) {
                *os << "  pushq %rbp\n";
                *os << "  .cfi_def_cfa_offset 16\n";
                *os << "  .cfi_offset 6, -16\n";
                *os << "  movq %rsp, %rbp\n";
                *os << "  .cfi_def_cfa_register 6\n";
                for (const auto& reg : usedCalleeRegs) {
                    *os << "  pushq %" << reg << "\n";
                }
            }
        } else {
            auto& as = cg.getAssembler();
            if (!isZeroFrame) {
                as.emitByte(0x55);
                as.emitBytes({0x48, 0x89, 0xE5});
                for (const auto& reg : usedCalleeRegs) {
                    uint8_t r = getArchRegIndex(reg);
                    if (r >= 8) as.emitByte(0x41);
                    as.emitByte(0x50 + (r & 7));
                }
            }
        }

        if (!isZeroFrame) {
            int total_frame = 8 * (1 + (int)usedCalleeRegs.size()) + stack_alloc;
            if (total_frame % 16 != 0) {
                stack_alloc += (16 - (total_frame % 16));
            }
            if (auto* os = cg.getTextStream()) {
                if (stack_alloc > 0) *os << "  subq $" << stack_alloc << ", %rsp\n";
            } else {
                auto& as = cg.getAssembler();
                if (stack_alloc > 0) {
                    if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc});
                    else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); }
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
        int current_offset = -64;
        for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } }
        int stack_alloc = std::abs(current_offset + 56) + 32; // Shadow space
        if ((stack_alloc + 64 + 8) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 64 + 8) % 16);
        if (auto* os = cg.getTextStream()) {
            if (stack_alloc > 0) *os << "  sub rsp, " << stack_alloc << "\n";
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
            if (stack_alloc > 0) { if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc}); else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); } }
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
    if (abi == X64ABI::SystemV) {
        bool makesCalls = false;
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                auto opc = instr->getOpcode();
                if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                    makesCalls = true;
                    break;
                }
            }
            if (makesCalls) break;
        }

        std::vector<std::string> usedCalleeRegs;
        static const std::vector<std::string> calleeList = {"rbx", "r12", "r13", "r14", "r15"};
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->hasPhysicalRegister()) {
                    size_t regIdx = instr->getPhysicalRegister();
                    if (regIdx < integerRegs.size()) {
                        const std::string& regName = integerRegs[regIdx];
                        if (std::find(calleeList.begin(), calleeList.end(), regName) != calleeList.end()) {
                            if (std::find(usedCalleeRegs.begin(), usedCalleeRegs.end(), regName) == usedCalleeRegs.end()) {
                                usedCalleeRegs.push_back(regName);
                            }
                        }
                    }
                }
            }
        }

        int current_offset = -8 - 8 * (int)usedCalleeRegs.size();
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    if (func.hasStackSlot(instr.get()) || !instr->hasPhysicalRegister()) {
                        current_offset -= 8;
                    }
                }
            }
        }
        int stack_alloc = std::abs(current_offset + 8 + 8 * (int)usedCalleeRegs.size());

        bool isZeroFrame = (!makesCalls && stack_alloc == 0 && usedCalleeRegs.empty());

        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            if (!usedCalleeRegs.empty()) {
                size_t bytes = usedCalleeRegs.size() * 8;
                *os << "  leaq -" << bytes << "(%rbp), %rsp\n";
                for (auto it = usedCalleeRegs.rbegin(); it != usedCalleeRegs.rend(); ++it) {
                    *os << "  popq %" << *it << "\n";
                }
                *os << "  popq %rbp\n";
            } else if (!isZeroFrame) {
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

            if (!usedCalleeRegs.empty()) {
                size_t bytes = usedCalleeRegs.size() * 8;
                emitRegMem(as, 0x48, 0x8D, 4, -(int32_t)bytes); // leaq -N(%rbp), %rsp
                for (auto it = usedCalleeRegs.rbegin(); it != usedCalleeRegs.rend(); ++it) {
                    uint8_t r = getArchRegIndex(*it);
                    if (r >= 8) as.emitByte(0x41);
                    as.emitByte(0x58 + (r & 7));
                }
                as.emitByte(0x5D); // pop rbp
            } else if (!isZeroFrame) {
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
        if (!i.getOperands().empty()) {
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
        bool makesCalls = false;
        ir::Function* func = i.getParent()->getParent();
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                auto opc = instr->getOpcode();
                if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                    makesCalls = true;
                    break;
                }
            }
            if (makesCalls) break;
        }
        std::vector<std::string> usedCalleeRegs;
        static const std::vector<std::string> calleeList = {"rbx", "r12", "r13", "r14", "r15"};
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->hasPhysicalRegister()) {
                    size_t regIdx = instr->getPhysicalRegister();
                    if (regIdx < integerRegs.size()) {
                        const std::string& regName = integerRegs[regIdx];
                        if (std::find(calleeList.begin(), calleeList.end(), regName) != calleeList.end()) {
                            if (std::find(usedCalleeRegs.begin(), usedCalleeRegs.end(), regName) == usedCalleeRegs.end()) {
                                usedCalleeRegs.push_back(regName);
                            }
                        }
                    }
                }
            }
        }
        int current_offset = -8 - 8 * (int)usedCalleeRegs.size();
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    if (func->hasStackSlot(instr.get()) || !instr->hasPhysicalRegister()) {
                        current_offset -= 8;
                    }
                }
            }
        }
        int stack_alloc = std::abs(current_offset + 8 + 8 * (int)usedCalleeRegs.size());
        bool isZeroFrame = (!makesCalls && stack_alloc == 0 && usedCalleeRegs.empty());
        if (isZeroFrame) {
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
                *os << "  " << addOp << " " << s0 << ", " << d << "\n";
            }
            cg.lastStoreOp = "";
            return;
        }

        if (abi == X64ABI::Windows) {
            if (isGlobal0) *os << "  lea " << rax << ", " << op0 << "\n";
            else *os << "  mov " << rax << ", " << op0 << "\n";
            
            if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  add " << rax << ", rdx\n";
            else *os << "  add " << rax << ", " << op1 << "\n";
            
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else emitMov(cg, os, op0, rax, is32);
            
            if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  " << addOp << " %rdx, " << rax << "\n";
            else *os << "  " << addOp << " " << op1 << ", " << rax << "\n";
            
            emitMov(cg, os, rax, dst, is32);
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

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

        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  sub " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            emitMov(cg, os, op0, rax, is32);
            *os << "  " << subOp << " " << op1 << ", " << rax << "\n";
            emitMov(cg, os, rax, dst, is32);
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

        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  imul " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            emitMov(cg, os, op0, rax, is32);
            *os << "  " << mulOp << " " << op1 << ", " << rax << "\n";
            emitMov(cg, os, rax, dst, is32);
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
    std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
    std::string rcx = (abi == X64ABI::SystemV) ? (is32 ? "%ecx" : "%rcx") : (is32 ? "ecx" : "rcx");
    std::string movOp = is32 ? "movl" : "movq";
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
            if (isGlobal0) *os << "  lea " << rax << ", " << op0 << "\n";
            else *os << "  mov " << rax << ", " << op0 << "\n";
            if (is32) *os << "  cdq\n"; else *os << "  cqo\n";
            if (isGlobal1) *os << "  lea " << rcx << ", " << op1 << "\n";
            else *os << "  mov " << rcx << ", " << op1 << "\n";
            *os << "  idiv " << rcx << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else emitMov(cg, os, op0, rax, is32);
            if (is32) *os << "  cltd\n"; else *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcx << "\n";
            else emitMov(cg, os, op1, rcx, is32);
            *os << "  " << idivOp << " " << rcx << "\n";
            emitMov(cg, os, rax, dst, is32);
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
    std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
    std::string rcx = (abi == X64ABI::SystemV) ? (is32 ? "%ecx" : "%rcx") : (is32 ? "ecx" : "rcx");
    std::string rdx = (abi == X64ABI::SystemV) ? (is32 ? "%edx" : "%rdx") : (is32 ? "edx" : "rdx");
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
            if (isGlobal0) *os << "  lea " << rax << ", " << op0 << "\n";
            else *os << "  mov " << rax << ", " << op0 << "\n";
            if (is32) *os << "  cdq\n"; else *os << "  cqo\n";
            if (isGlobal1) *os << "  lea " << rcx << ", " << op1 << "\n";
            else *os << "  mov " << rcx << ", " << op1 << "\n";
            *os << "  idiv " << rcx << "\n";
            *os << "  mov " << dst << ", " << rdx << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else emitMov(cg, os, op0, rax, is32);
            if (is32) *os << "  cltd\n"; else *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcx << "\n";
            else emitMov(cg, os, op1, rcx, is32);
            *os << "  " << idivOp << " " << rcx << "\n";
            emitMov(cg, os, rdx, dst, is32);
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
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  and " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  andq " << op1 << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x21, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  or " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  orq " << op1 << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x09, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  xor " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  xorq " << op1 << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x31, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  shlq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xE0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  shrq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xE8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  sarq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xF8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  neg " << rax << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  negq " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xD8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  not " << rax << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  notq " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
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

    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << srcOp << "\n";
            *os << "  mov " << destOp << ", " << rax << "\n";
        } else {
            *os << "  movq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
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
        size_t int_idx = 0, float_idx = 0;
        for (size_t j = 1; j < i.getOperands().size(); ++j) {
            ir::Value* argVal = i.getOperands()[j]->get();
            bool isFloat = argVal->getType() && (argVal->getType()->isFloatTy() || argVal->getType()->isDoubleTy());
            bool argIsGlobal = dynamic_cast<ir::GlobalVariable*>(argVal) != nullptr ||
                               (dynamic_cast<ir::GlobalValue*>(argVal) != nullptr && !dynamic_cast<ir::Function*>(argVal));
            if (isFloat) {
                if (float_idx < floatArgRegs.size()) {
                    std::string reg = floatArgRegs[float_idx++];
                    if (abi == X64ABI::Windows)
                        *os << "  movsd " << reg << ", " << cg.getValueAsOperand(argVal) << "\n";
                    else
                        *os << "  movsd " << cg.getValueAsOperand(argVal) << ", %" << reg << "\n";
                }
            } else {
                size_t maxArgs = (abi == X64ABI::SystemV) ? 6 : 4;
                if (int_idx < maxArgs) {
                    bool is32 = is32BitType(argVal->getType());
                    std::string reg = getRegisterName(integerArgRegs[int_idx++], argVal->getType());
                    if (argIsGlobal && abi == X64ABI::Windows) {
                        *os << "  lea " << reg << ", " << cg.getValueAsOperand(argVal) << "\n";
                    } else if (argIsGlobal && abi == X64ABI::SystemV) {
                        *os << "  leaq " << cg.getValueAsOperand(argVal) << ", " << reg << "\n";
                    } else {
                        if (abi == X64ABI::Windows)
                            *os << "  mov " << reg << ", " << cg.getValueAsOperand(argVal) << "\n";
                        else
                            emitMov(cg, os, cg.getValueAsOperand(argVal), reg, is32);
                    }
                }
            }
            if (false) if (abi == X64ABI::Windows) {
                // Stack arg (5th+): load into rax then store at [rsp + (j-1)*8]
                size_t stackOff = (j - 1) * 8;
                if (argIsGlobal) {
                    *os << "  lea rax, " << cg.getValueAsOperand(argVal) << "\n";
                } else {
                    *os << "  mov rax, " << cg.getValueAsOperand(argVal) << "\n";
                }
                *os << "  mov [rsp + " << stackOff << "], rax\n";
            }
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
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) {
            if (abi == X64ABI::Windows)
                *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
            else
                *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
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

void X64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  movsd xmm0, " << op0 << "\n";
            *os << "  addsd xmm0, " << op1 << "\n";
            *os << "  movsd " << dst << ", xmm0\n";
        } else {
            *os << "  movsd " << op0 << ", %xmm0\n";
            *os << "  addsd " << op1 << ", %xmm0\n";
            *os << "  movsd %xmm0, " << dst << "\n";
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
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  movsd xmm0, " << op0 << "\n";
            *os << "  subsd xmm0, " << op1 << "\n";
            *os << "  movsd " << dst << ", xmm0\n";
        } else {
            *os << "  movsd " << op0 << ", %xmm0\n";
            *os << "  subsd " << op1 << ", %xmm0\n";
            *os << "  movsd %xmm0, " << dst << "\n";
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
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  movsd xmm0, " << op0 << "\n";
            *os << "  mulsd xmm0, " << op1 << "\n";
            *os << "  movsd " << dst << ", xmm0\n";
        } else {
            *os << "  movsd " << op0 << ", %xmm0\n";
            *os << "  mulsd " << op1 << ", %xmm0\n";
            *os << "  movsd %xmm0, " << dst << "\n";
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
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  movsd xmm0, " << op0 << "\n";
            *os << "  divsd xmm0, " << op1 << "\n";
            *os << "  movsd " << dst << ", xmm0\n";
        } else {
            *os << "  movsd " << op0 << ", %xmm0\n";
            *os << "  divsd " << op1 << ", %xmm0\n";
            *os << "  movsd %xmm0, " << dst << "\n";
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
            if (abi == X64ABI::Windows) {
                *os << "  movsd xmm0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
                *os << "  ucomisd xmm0, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzx " << eax << ", " << al << "\n";
                *os << "  mov " << cg.getValueAsOperand(&i) << ", " << rax << "\n";
            } else {
                *os << "  movsd " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %xmm0\n";
                *os << "  ucomisd " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %xmm0\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzbq " << al << ", " << rax << "\n";
                *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
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
        if (op == ir::Instruction::Sltof || op == ir::Instruction::SWtoF || op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            if (abi == X64ABI::Windows) {
                *os << "  cvtsi2sd xmm0, " << srcOp << "\n";
                *os << "  movsd " << destOp << ", xmm0\n";
            } else {
                *os << "  cvtsi2sd " << srcOp << ", %xmm0\n";
                *os << "  movsd %xmm0, " << destOp << "\n";
            }
        } else if (op == ir::Instruction::ExtUB) {
            std::string r8 = (srcOp[0] == '%') ? to8BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movzbl " << srcOp << ", %eax\n";
            } else {
                *os << "  movzbl " << r8 << ", %eax\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            std::string r16 = (srcOp[0] == '%') ? to16BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movzwl " << srcOp << ", %eax\n";
            } else {
                *os << "  movzwl " << r16 << ", %eax\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            std::string r32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movl " << srcOp << ", " << eax << "\n";
            } else {
                *os << "  movl " << r32 << ", " << eax << "\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            std::string r8 = (srcOp[0] == '%') ? to8BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movsbq " << srcOp << ", %rax\n";
            } else {
                *os << "  movsbq " << r8 << ", %rax\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            std::string r16 = (srcOp[0] == '%') ? to16BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movswq " << srcOp << ", %rax\n";
            } else {
                *os << "  movswq " << r16 << ", %rax\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            std::string r32 = (srcOp[0] == '%') ? to32BitReg(srcOp) : srcOp;
            if (srcOp[0] != '%') {
                *os << "  movslq " << srcOp << ", %rax\n";
            } else {
                *os << "  movl " << r32 << ", " << eax << "\n";
                *os << "  cltq\n";
            }
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else {
            *os << "  movq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
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
    if (auto* os = cg.getTextStream()) {
        std::string op = cg.getValueAsOperand(i.getOperands()[0]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr;
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
                if (size == 1) *os << (isSigned ? "  movsbq (%rax), %rax\n" : "  movzbq (%rax), %rax\n");
                else if (size == 2) *os << (isSigned ? "  movswq (%rax), %rax\n" : "  movzwq (%rax), %rax\n");
                else if (size == 4) *os << (isSigned ? "  movslq (%rax), %rax\n" : "  movl (%rax), %eax\n");
                else if (i.getType() && (i.getType()->isFloatTy() || i.getType()->isDoubleTy())) *os << "  movsd (%rax), %xmm0\n";
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
            if (abi == X64ABI::Windows)
                *os << "  movsd " << cg.getValueAsOperand(&i) << ", xmm0\n";
            else
                *os << "  movsd %xmm0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (abi == X64ABI::Windows)
            *os << "  mov " << cg.getValueAsOperand(&i) << ", " << rax << "\n";
        else
            *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
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
        // Load value-to-store into rax
        bool isGlobalVal = dynamic_cast<ir::GlobalVariable*>(i.getOperands()[0]->get()) != nullptr || 
                           (dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(i.getOperands()[0]->get()));
        if (abi == X64ABI::Windows) {
            if (isGlobalVal) *os << "  lea " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            else *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        } else {
            if (isGlobalVal) *os << "  leaq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
            else *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        }
        std::string op = cg.getValueAsOperand(i.getOperands()[1]->get());
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
            // op = stack operand holding the pointer address — load it into rdx
            if (abi == X64ABI::Windows)
                *os << "  mov " << rdx << ", " << op << "\n";
            else
                *os << "  movq " << op << ", " << rdx << "\n";
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << "  movb " << al << ", (" << rdx << ")\n";
                else if (size == 2) *os << "  movw " << ax << ", (" << rdx << ")\n";
                else if (size == 4) *os << "  movl " << eax << ", (" << rdx << ")\n";
                else *os << "  movq " << rax << ", (" << rdx << ")\n";
            } else {
                if (size == 1) *os << "  mov byte ptr [rdx], al\n";
                else if (size == 2) *os << "  mov word ptr [rdx], ax\n";
                else if (size == 4) *os << "  mov dword ptr [rdx], eax\n";
                else *os << "  mov [rdx], rax\n";
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
    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        if (abi == X64ABI::SystemV) {
            *os << "  movq heap_ptr(%rip), " << rax << "\n";
            *os << "  movq " << rax << ", " << formatStackOperand(pointerOffset) << "\n";
            *os << "  addq $" << alignedSize << ", " << rax << "\n";
            *os << "  movq " << rax << ", heap_ptr(%rip)\n";
        } else {
            *os << "  mov rax, [rip + heap_ptr]\n";
            *os << "  mov " << formatStackOperand(pointerOffset) << ", rax\n";
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
            if (auto* os = cg.getTextStream()) {
                if (abi == X64ABI::Windows) {
                    *os << "  mov " << destOp << ", " << srcOp << "\n";
                } else {
                    emitMov(cg, os, srcOp, destOp, is32);
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
            bool is32 = is32BitType(incomingVal->getType());
            std::string rax = (abi == X64ABI::SystemV) ? (is32 ? "%eax" : "%rax") : (is32 ? "eax" : "rax");
            if (abi == X64ABI::Windows) {
                *os << "  mov " << rax << ", " << srcOp << "\n";
                *os << "  push " << rax << "\n";
            } else {
                emitMov(cg, os, srcOp, rax, is32);
                *os << "  pushq %rax\n";
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
        } else {
            auto& as = cg.getAssembler();
            as.emitByte(0x58);
            emitStoreResult(cg, *phi, 0);
        }
    }
}

void X64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get());

    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            *os << "  test " << rax << ", " << rax << "\n";
        } else {
            *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
            *os << "  testq " << rax << ", " << rax << "\n";
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
                *os << "  " << cmpOp << " " << op1 << ", " << regRax << "\n";
            } else {
                *os << "  " << cmpOp << " " << op1 << ", " << op0 << "\n";
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
