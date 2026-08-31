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
        for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    cg.getStackOffsets()[instr.get()] = current_offset;
                    current_offset -= 8;
                }
            }
        }
        int stack_alloc = std::abs(current_offset + 8 + 8 * (int)usedCalleeRegs.size());

        bool isZeroFrame = (!makesCalls && stack_alloc == 0 && func.getParameters().empty());

        if (auto* os = cg.getTextStream()) {
            *os << "  .cfi_startproc\n";
            if (!isZeroFrame || !usedCalleeRegs.empty()) {
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
            if (!isZeroFrame || !usedCalleeRegs.empty()) {
                as.emitByte(0x55);
                as.emitBytes({0x48, 0x89, 0xE5});
                for (const auto& reg : usedCalleeRegs) {
                    uint8_t r = getArchRegIndex(reg);
                    if (r >= 8) as.emitByte(0x41);
                    as.emitByte(0x50 + (r & 7));
                }
            }
        }

        if (stack_alloc % 16 != 0) stack_alloc += (16 - (stack_alloc % 16));
        if (auto* os = cg.getTextStream()) {
            if (stack_alloc > 0) *os << "  subq $" << stack_alloc << ", %rsp\n";
            size_t i_idx = 0, f_idx = 0;
            for (auto& param : func.getParameters()) {
                bool isF = param->getType() && (param->getType()->isFloatTy() || param->getType()->isDoubleTy());
                if (isF) {
                    if (f_idx < floatArgRegs.size()) {
                        *os << "  movsd %" << floatArgRegs[f_idx++] << ", " << formatStackOperand(cg.getStackOffsets()[param.get()]) << "\n";
                    }
                } else {
                    if (i_idx < integerArgRegs.size()) {
                        *os << "  movq %" << integerArgRegs[i_idx++] << ", " << formatStackOperand(cg.getStackOffsets()[param.get()]) << "\n";
                    }
                }
            }
        } else {
            auto& as = cg.getAssembler();
            if (stack_alloc > 0) {
                if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc});
                else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); }
            }
            int j = 0; for (auto& param : func.getParameters()) {
                if (j < 6) {
                    uint8_t r = getArchRegIndex(integerArgRegs[j]);
                    emitRegMem(as, (r >= 8 ? 0x4C : 0x48), 0x89, r & 7, cg.getStackOffsets()[param.get()]);
                }
                j++;
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
        for (auto& param : func.getParameters()) { (void)param; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getType() && !instr->getType()->isVoidTy()) {
                    current_offset -= 8;
                }
            }
        }
        int stack_alloc = std::abs(current_offset + 8 + 8 * (int)usedCalleeRegs.size());

        bool isZeroFrame = (!makesCalls && stack_alloc == 0 && func.getParameters().empty());

        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            if (!usedCalleeRegs.empty()) {
                size_t bytes = usedCalleeRegs.size() * 8;
                *os << "  leaq -" << bytes << "(%rbp), %rsp\n";
                for (auto it = usedCalleeRegs.rbegin(); it != usedCalleeRegs.rend(); ++it) {
                    *os << "  popq %" << *it << "\n";
                }
            }
            if (!isZeroFrame || !usedCalleeRegs.empty()) {
                *os << "  popq %rbp\n";
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
            }
            if (!isZeroFrame || !usedCalleeRegs.empty()) {
                as.emitByte(0x5D); // pop rbp
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
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) {
            if (abi == X64ABI::Windows)
                *os << "  mov " << rax << ", " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            else
                *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        }
        *os << "  jmp " << i.getParent()->getParent()->getName() << "_epilogue" << "\n";
    } else {
        if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize();
        cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getParent()->getParent()->getName() + "_epilogue", ".text"});
    }
}

void X64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
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
            
            if (isGlobal1) *os << "  lea rdx, " << op1 << "\n  add " << rax << ", rdx\n";
            else *os << "  add " << rax << ", " << op1 << "\n";
            
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else *os << "  movq " << op0 << ", " << rax << "\n";
            
            if (isGlobal1) *os << "  leaq " << op1 << ", %rdx\n  addq %rdx, " << rax << "\n";
            else *os << "  addq " << op1 << ", " << rax << "\n";
            
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  sub " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  subq " << op1 << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x29, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        auto op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
        auto op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
        auto dst = cg.getValueAsOperand(&i);
        if (abi == X64ABI::Windows) {
            *os << "  mov " << rax << ", " << op0 << "\n";
            *os << "  imul " << rax << ", " << op1 << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  imulq " << op1 << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
        }
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
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
            *os << "  cqo\n";
            if (isGlobal1) *os << "  lea " << rcx << ", " << op1 << "\n";
            else *os << "  mov " << rcx << ", " << op1 << "\n";
            *os << "  idiv " << rcx << "\n";
            *os << "  mov " << dst << ", " << rax << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcx << "\n";
            else *os << "  movq " << op1 << ", " << rcx << "\n";
            *os << "  idivq " << rcx << "\n";
            *os << "  movq " << rax << ", " << dst << "\n";
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
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    std::string rdx = (abi == X64ABI::SystemV) ? "%rdx" : "rdx";
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
            *os << "  cqo\n";
            if (isGlobal1) *os << "  lea " << rcx << ", " << op1 << "\n";
            else *os << "  mov " << rcx << ", " << op1 << "\n";
            *os << "  idiv " << rcx << "\n";
            *os << "  mov " << dst << ", " << rdx << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << op0 << ", " << rax << "\n";
            else *os << "  movq " << op0 << ", " << rax << "\n";
            *os << "  cqto\n";
            if (isGlobal1) *os << "  leaq " << op1 << ", " << rcx << "\n";
            else *os << "  movq " << op1 << ", " << rcx << "\n";
            *os << "  idivq " << rcx << "\n";
            *os << "  movq " << rdx << ", " << dst << "\n";
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
                    std::string reg = getRegisterName(integerArgRegs[int_idx++], argVal->getType());
                    if (argIsGlobal && abi == X64ABI::Windows) {
                        *os << "  lea " << reg << ", " << cg.getValueAsOperand(argVal) << "\n";
                    } else if (argIsGlobal && abi == X64ABI::SystemV) {
                        *os << "  leaq " << cg.getValueAsOperand(argVal) << ", " << reg << "\n";
                    } else {
                        if (abi == X64ABI::Windows)
                            *os << "  mov " << reg << ", " << cg.getValueAsOperand(argVal) << "\n";
                        else
                            *os << "  movq " << cg.getValueAsOperand(argVal) << ", " << reg << "\n";
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
                else *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
                if (isGlobal1) *os << "  leaq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rdx\n  cmpq %rdx, " << rax << "\n";
                else *os << "  cmpq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
                *os << "  " << set << " " << al << "\n";
                *os << "  movzbq " << al << ", " << rax << "\n";
                *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
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
            *os << "  movzbq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            *os << "  movzwq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            *os << "  movq " << srcOp << ", " << rax << "\n";
            *os << "  movl " << eax << ", " << eax << "\n"; // Zero-extends %eax to %rax
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            *os << "  movsbq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            *os << "  movswq " << srcOp << ", " << rax << "\n";
            *os << "  movq " << rax << ", " << destOp << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            *os << "  movq " << srcOp << ", " << rax << "\n";
            *os << "  cltq\n"; // Sign-extends %eax to %rax
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
    for (const auto& move : phiMoves) {
        ir::Value* incomingVal = move.first;
        if (auto* os = cg.getTextStream()) {
            std::string srcOp = cg.getValueAsOperand(incomingVal);
            std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
            if (abi == X64ABI::Windows) {
                *os << "  mov rax, " << srcOp << "\n";
                *os << "  push rax\n";
            } else {
                *os << "  movq " << srcOp << ", " << rax << "\n";
                *os << "  pushq " << rax << "\n";
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
            std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
            if (abi == X64ABI::Windows)
                *os << "  pop " << rax << "\n";
            else
                *os << "  popq " << rax << "\n";
            if (abi == X64ABI::Windows)
                *os << "  mov " << destOp << ", " << rax << "\n";
            else
                *os << "  movq " << rax << ", " << destOp << "\n";
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

    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        bool isGlobal0 = dynamic_cast<ir::GlobalVariable*>(cmp.getOperands()[0]->get()) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(cmp.getOperands()[0]->get()) != nullptr && !dynamic_cast<ir::Function*>(cmp.getOperands()[0]->get()));
        bool isGlobal1 = dynamic_cast<ir::GlobalVariable*>(cmp.getOperands()[1]->get()) != nullptr ||
                         (dynamic_cast<ir::GlobalValue*>(cmp.getOperands()[1]->get()) != nullptr && !dynamic_cast<ir::Function*>(cmp.getOperands()[1]->get()));

        if (abi == X64ABI::Windows) {
            if (isGlobal0) *os << "  lea " << rax << ", " << cg.getValueAsOperand(cmp.getOperands()[0]->get()) << "\n";
            else *os << "  mov " << rax << ", " << cg.getValueAsOperand(cmp.getOperands()[0]->get()) << "\n";
            if (isGlobal1) *os << "  lea rdx, " << cg.getValueAsOperand(cmp.getOperands()[1]->get()) << "\n  cmp " << rax << ", rdx\n";
            else *os << "  cmp " << rax << ", " << cg.getValueAsOperand(cmp.getOperands()[1]->get()) << "\n";
        } else {
            if (isGlobal0) *os << "  leaq " << cg.getValueAsOperand(cmp.getOperands()[0]->get()) << ", " << rax << "\n";
            else *os << "  movq " << cg.getValueAsOperand(cmp.getOperands()[0]->get()) << ", " << rax << "\n";
            if (isGlobal1) *os << "  leaq " << cg.getValueAsOperand(cmp.getOperands()[1]->get()) << ", %rdx\n  cmpq %rdx, " << rax << "\n";
            else *os << "  cmpq " << cg.getValueAsOperand(cmp.getOperands()[1]->get()) << ", " << rax << "\n";
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
    if (abi == X64ABI::SystemV) {
        if (base[0] == '%') return base;
        return "%" + base;
    }
    if (base[0] == '%') return base.substr(1);
    return base;
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
