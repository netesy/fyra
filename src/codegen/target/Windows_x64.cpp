#include "codegen/target/Windows_x64.h"
#include "codegen/CodeGen.h"
#include "codegen/execgen/Assembler.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/Parameter.h"
#include <ostream>
#include <algorithm>

namespace codegen {
namespace target {

namespace {
    std::string getEpilogueLabel(const ir::Function* f) { return f ? f->getName() + "_epilogue" : "L_epilogue"; }
    std::string getImm(ir::Value* v, CodeGen& cg) { if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) return std::to_string(ci->getValue()); return cg.getValueAsOperand(v); }
}

void Windows_x64::initRegisters() {
    integerRegs = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "rsi", "rdi", "rbx", "r12", "r13", "r14", "r15"};
    floatRegs.clear();
    for (int i=0; i<16; ++i) floatRegs.push_back("xmm"+std::to_string(i));
    vectorRegs = floatRegs;
    callerSaved.clear();
    for (const auto& r : {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}) callerSaved[r] = true;
    calleeSaved.clear();
    for (const auto& r : {"rbx", "rsi", "rdi", "r12", "r13", "r14", "r15", "rbp"}) calleeSaved[r] = true;
    intReturnReg = "rax"; floatReturnReg = "xmm0"; stackPtrReg = "rsp"; framePtrReg = "rbp";
    integerArgRegs = {"rcx", "rdx", "r8", "r9"};
    floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3"};
}

Windows_x64::Windows_x64() : X86_64Base() { initRegisters(); }

std::string Windows_x64::formatStackOperand(int offset) const {
    if (offset >= 0) return "[rbp + " + std::to_string(offset) + "]";
    return "[rbp - " + std::to_string(-offset) + "]";
}

std::string Windows_x64::formatGlobalOperand(const std::string& name) const {
    return "[rel " + name + "]";
}

void Windows_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (!cg.getTextStream()) {
        CodeGen::SymbolInfo s; s.name = func.getName(); s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
    }

    resetStackOffset();
    int current_offset = -8 - 56;

    int p_idx = 0;
    for (auto& p : func.getParameters()) {
        cg.getStackOffsets()[p.get()] = 16 + (p_idx * 8);
        p_idx++;
    }

    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        for (auto& i_ptr : bb->getInstructions()) {
            ir::Instruction* i = i_ptr.get();
            if (!i->getType()->isVoidTy() && cg.getStackOffsets().find(i) == cg.getStackOffsets().end()) {
                cg.getStackOffsets()[i] = current_offset;
                current_offset -= 8;
            }
        }
    }

    int stack_alloc = std::abs(current_offset + 8);
    stack_alloc += 32;
    if (stack_alloc % 16 != 0) stack_alloc += 16 - (stack_alloc % 16);

    if (auto* os = cg.getTextStream()) {
        *os << "  push rbp\n  mov rbp, rsp\n  push rbx\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n";
        if (stack_alloc > 0) *os << "  sub rsp, " << stack_alloc << "\n";

        int j = 0;
        for (auto const& p : func.getParameters()) {
            (void)p;
            if (j < 4) {
                *os << "  mov " << formatStackOperand(16 + (j * 8)) << ", " << integerArgRegs[j] << "\n";
            }
            j++;
        }
    } else {
        auto& as = cg.getAssembler();
        as.emitByte(0x55); as.emitBytes({0x48, 0x89, 0xE5});
        as.emitByte(0x53); as.emitByte(0x56); as.emitByte(0x57);
        as.emitBytes({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
        if (stack_alloc > 0) {
            if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc});
            else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); }
        }
        int j = 0;
        for (auto const& p : func.getParameters()) {
            if (j < 4) {
                uint8_t r = getArchRegIndex(integerArgRegs[j]);
                emitRegMem(as, (r>=8?0x4C:0x48), 0x89, r&7, 16 + (j * 8));
            } else {
                // Already on stack at [rbp + 16 + j*8]
            }
            (void)p;
            j++;
        }
    }
}

void Windows_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getEpilogueLabel(&func) << ":\n  lea rsp, [rbp - 56]\n  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbx\n  pop rbp\n  ret\n";
    } else {
        CodeGen::SymbolInfo s; s.name = getEpilogueLabel(&func); s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 0; s.binding = 0; cg.addSymbol(s);
        auto& as = cg.getAssembler();
        // lea rsp, [rbp - 56]
        as.emitBytes({0x48, 0x8D, 0x65, 0xC8});
        // pop r15, r14, r13, r12
        as.emitBytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C});
        // pop rdi, rsi, rbx
        as.emitBytes({0x5F, 0x5E, 0x5B});
        // pop rbp
        as.emitByte(0x5D);
        // ret
        as.emitByte(0xC3);
    }
}

void Windows_x64::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n";
        *os << "  jmp " << getEpilogueLabel(i.getParent()->getParent()) << "\n";
    } else {
        if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getEpilogueLabel(i.getParent()->getParent()), ".text"});
    }
}

void Windows_x64::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n";
        *os << "  add rax, " << getImm(i.getOperands()[1]->get(), cg) << "\n";
        *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n";
        *os << "  sub rax, " << getImm(i.getOperands()[1]->get(), cg) << "\n";
        *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x29, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n";
        *os << "  imul rax, " << getImm(i.getOperands()[1]->get(), cg) << "\n";
        *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n  cqo\n";
        *os << "  idiv " << getImm(i.getOperands()[1]->get(), cg) << "\n";
        *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string src = getImm(i.getOperands()[0]->get(), cg), dst = cg.getValueAsOperand(&i);
        if (src == dst) return;
        *os << "  mov rax, " << src << "\n  mov " << dst << ", rax\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)5); ++j) {
            *os << "  mov " << integerArgRegs[j-1] << ", " << getImm(i.getOperands()[j]->get(), cg) << "\n";
        }
        *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        if (!i.getType()->isVoidTy()) *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)5); ++j) {
            uint8_t r = getArchRegIndex(integerArgRegs[j-1]);
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), r);
        }
        cg.getAssembler().emitByte(0xE8);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getOperands()[0]->get()->getName(), ".text"});
        if (!i.getType()->isVoidTy()) emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitCmp(CodeGen& cg, ir::Instruction& i) {
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
        *os << "  mov rax, " << getImm(i.getOperands()[0]->get(), cg) << "\n";
        *os << "  cmp rax, " << getImm(i.getOperands()[1]->get(), cg) << "\n";
        *os << "  " << set << " al\n  movzx eax, al\n  mov " << cg.getValueAsOperand(&i) << ", eax\n";
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
        cg.getAssembler().emitBytes({0x0F, s, 0xC0, 0x0F, 0xB6, 0xC0});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  test rax, rax\n";
        *os << "  jne " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())) << "\n";
        if (i.getOperands().size() > 2)
            *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x85, 0xC0, 0x0F, 0x85});
        uint64_t off1 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())), ".text"});
        if (i.getOperands().size() > 2) {
            cg.getAssembler().emitByte(0xE9);
            uint64_t off2 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())), ".text"});
        }
    }
}

void Windows_x64::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())) << "\n";
    } else {
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())), ".text"});
    }
}

void Windows_x64::emitStartFunction(CodeGen& cg) {
    if (!cg.getTextStream()) {
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        cg.getAssembler().emitBytes({0x48, 0x83, 0xEC, 0x28});
        cg.getAssembler().emitByte(0xE8);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "main", ".text"});
        cg.getAssembler().emitBytes({0x48, 0x83, 0xC4, 0x28, 0xC3});
    }
}

void Windows_x64::emitPrologue(CodeGen&, int) {}
void Windows_x64::emitEpilogue(CodeGen&) {}
const std::vector<std::string>& Windows_x64::getRegisters(RegisterClass rc) const { if(rc==RegisterClass::Float)return floatRegs; return integerRegs; }
const std::string& Windows_x64::getReturnRegister(const ir::Type* t) const { if(t && t->isFloatingPoint())return floatReturnReg; return intReturnReg; }

} // namespace target
} // namespace codegen
