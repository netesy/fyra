#include "codegen/target/SystemV_x64.h"
#include "codegen/CodeGen.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Parameter.h"
#include <ostream>
#include <algorithm>

namespace codegen {
namespace target {

SystemV_x64::SystemV_x64() {
    initRegisters();
}

void SystemV_x64::initRegisters() {
    integerRegs = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    floatRegs.clear();
    for (int i=0; i<16; ++i) floatRegs.push_back("xmm"+std::to_string(i));
    vectorRegs = floatRegs;
    callerSaved.clear();
    for (const auto& r : {"rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"}) callerSaved[r] = true;
    calleeSaved.clear();
    for (const auto& r : {"rbx", "rbp", "r12", "r13", "r14", "r15"}) calleeSaved[r] = true;
    intReturnReg = "rax"; floatReturnReg = "xmm0"; stackPtrReg = "rsp"; framePtrReg = "rbp";
    integerArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
}

void SystemV_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (!cg.getTextStream()) {
        CodeGen::SymbolInfo s; s.name = func.getName(); s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
    }

    resetStackOffset();
    int current_offset = -8;

    int p_idx = 0;
    for (auto& p : func.getParameters()) {
        if (p_idx < 6) {
            current_offset -= 8;
            cg.getStackOffsets()[p.get()] = current_offset;
        } else {
            cg.getStackOffsets()[p.get()] = 16 + (p_idx - 6) * 8;
        }
        p_idx++;
    }

    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        for (auto& i_ptr : bb->getInstructions()) {
            ir::Instruction* i = i_ptr.get();
            if (!i->getType()->isVoidTy() && cg.getStackOffsets().find(i) == cg.getStackOffsets().end()) {
                current_offset -= 8;
                cg.getStackOffsets()[i] = current_offset;
            }
        }
    }

    int stack_alloc = std::abs(current_offset);
    if (stack_alloc % 16 != 0) stack_alloc += 16 - (stack_alloc % 16);

    if (auto* os = cg.getTextStream()) {
        *os << func.getName() << ":\n  pushq %rbp\n  movq %rsp, %rbp\n";
        if (stack_alloc > 0) *os << "  subq $" << stack_alloc << ", %rsp\n";

        int j = 0;
        for (auto const& p : func.getParameters()) {
            if (j < 6) {
                *os << "  movq %" << integerArgRegs[j] << ", " << formatStackOperand(cg.getStackOffset(p.get())) << "\n";
            }
            j++;
        }
    } else {
        auto& as = cg.getAssembler();
        as.emitByte(0x55); as.emitBytes({0x48, 0x89, 0xE5});
        if (stack_alloc > 0) {
            if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc});
            else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); }
        }
        int j = 0;
        for (auto const& p : func.getParameters()) {
            if (j < 6) {
                uint8_t r = getArchRegIndex(integerArgRegs[j]);
                emitRegMem(as, (r>=8?0x4C:0x48), 0x89, r&7, cg.getStackOffset(p.get()));
            }
            j++;
        }
    }
}

void SystemV_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getFunctionEpilogueLabel(func) << ":\n  leave\n  ret\n";
    } else {
        CodeGen::SymbolInfo s; s.name = getFunctionEpilogueLabel(func); s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 0; s.binding = 0; cg.addSymbol(s);
        auto& as = cg.getAssembler();
        as.emitByte(0xC9); as.emitByte(0xC3);
    }
}

void SystemV_x64::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  jmp " << getFunctionEpilogueLabel(*i.getParent()->getParent()) << "\n";
    } else {
        if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getFunctionEpilogueLabel(*i.getParent()->getParent()), ".text"});
    }
}

void SystemV_x64::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  addq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n";
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  subq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n";
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x29, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  imulq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n";
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1});
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  cqto\n";
        *os << "  idivq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)7); ++j) {
            *os << "  movq " << cg.getValueAsOperand(i.getOperands()[j]->get()) << ", %" << integerArgRegs[j-1] << "\n";
        }
        *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    } else {
        for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)7); ++j) {
            uint8_t r = getArchRegIndex(integerArgRegs[j-1]);
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), r);
        }
        cg.getAssembler().emitByte(0xE8);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getOperands()[0]->get()->getName(), ".text"});
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string set;
        switch(i.getOpcode()){
            case ir::Instruction::Ceq:set="sete";break;
            case ir::Instruction::Cne:set="setne";break;
            case ir::Instruction::Cslt:set="setl";break;
            case ir::Instruction::Csle:set="setle";break;
            case ir::Instruction::Csgt:set="setg";break;
            case ir::Instruction::Csge:set="setge";break;
            default:set="sete";break;
        }
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  cmpq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n";
        *os << "  " << set << " %al\n  movzbq %al, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x39, 0xC8});
        uint8_t s = 0x94;
        switch(i.getOpcode()){
            case ir::Instruction::Ceq:s=0x94;break;
            case ir::Instruction::Cne:s=0x95;break;
            case ir::Instruction::Cslt:s=0x9C;break;
            case ir::Instruction::Csle:s=0x9E;break;
            case ir::Instruction::Csgt:s=0x9F;break;
            case ir::Instruction::Csge:s=0x9D;break;
            default:s=0x94;break;
        }
        cg.getAssembler().emitBytes({0x0F, s, 0xC0, 0x48, 0x0F, 0xB6, 0xC0});
        emitStoreResult(cg, i, 0);
    }
}

void SystemV_x64::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        *os << "  testq %rax, %rax\n";
        *os << "  jne " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())) << "\n";
        *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x85, 0xC0, 0x0F, 0x85});
        uint64_t off1 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())), ".text"});
        cg.getAssembler().emitByte(0xE9);
        uint64_t off2 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())), ".text"});
    }
}

void SystemV_x64::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())) << "\n";
    } else {
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())), ".text"});
    }
}

void SystemV_x64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::Value* sizeVal = instr.getOperands().empty() ? nullptr : instr.getOperands()[0]->get();
    ir::ConstantInt* sizeConst = (sizeVal) ? dynamic_cast<ir::ConstantInt*>(sizeVal) : nullptr;
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  movq __heap_ptr(%rip), %r11\n";
        *os << "  movq %r11, " << formatStackOperand(pointerOffset) << "\n";
        *os << "  addq $" << alignedSize << ", %r11\n";
        *os << "  movq %r11, __heap_ptr(%rip)\n";
    } else {
        auto& assembler = cg.getAssembler();
        assembler.emitBytes({0x4C, 0x8B, 0x1D});
        uint64_t off1 = assembler.getCodeSize(); assembler.emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, "__heap_ptr", ".text"});
        emitRegMem(assembler, 0x4C, 0x89, 3, pointerOffset); // mov %r11, offset(%rbp)
        assembler.emitBytes({0x49, 0x81, 0xC3}); assembler.emitDWord(alignedSize);
        assembler.emitBytes({0x4C, 0x89, 0x1D});
        uint64_t off2 = assembler.getCodeSize(); assembler.emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, "__heap_ptr", ".text"});
    }
}

void SystemV_x64::emitStartFunction(CodeGen& cg) {
    if (!cg.getTextStream()) {
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        cg.getAssembler().emitByte(0xE8);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "main", ".text"});
        cg.getAssembler().emitBytes({0x48, 0x89, 0xC7, 0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05});
    }
}

std::string SystemV_x64::formatStackOperand(int offset) const {
    return std::to_string(offset) + "(%rbp)";
}

std::string SystemV_x64::formatGlobalOperand(const std::string& name) const {
    return name + "(%rip)";
}

void SystemV_x64::emitPrologue(CodeGen&, int) {}
void SystemV_x64::emitEpilogue(CodeGen&) {}

const std::vector<std::string>& SystemV_x64::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Integer) return integerRegs;
    if (regClass == RegisterClass::Float) return floatRegs;
    return vectorRegs;
}

const std::string& SystemV_x64::getReturnRegister(const ir::Type* type) const {
    if (type && type->isFloatingPoint()) return floatReturnReg;
    return intReturnReg;
}

}
}
