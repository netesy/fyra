#include "codegen/target/Windows_x64.h"
#include "codegen/CodeGen.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Parameter.h"
#include "ir/Syscall.h"
#include "ir/GlobalVariable.h"
#include <ostream>
#include <algorithm>

namespace codegen { namespace target {

Windows_x64::Windows_x64() { initRegisters(); }

void Windows_x64::initRegisters() {
    integerRegs = {"rbx", "rsi", "rdi", "r12", "r13", "r14", "r15"};
    floatRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    integerArgRegs = {"rcx", "rdx", "r8", "r9"};
    floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3"};
    intReturnReg = "rax"; floatReturnReg = "xmm0"; framePtrReg = "rbp"; stackPtrReg = "rsp";
    for (const auto& r : integerRegs) { callerSaved[r] = false; calleeSaved[r] = true; }
}

void Windows_x64::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .data\n.align 8\nheap_ptr:\n  .quad __fyra_heap\n";
        *os << ".section .bss\n.align 16\n__fyra_heap:\n  .zero 1048576\n";
        *os << ".text\n";
    } else {
        // Binary header: we need to ensure heap_ptr and __fyra_heap exist in data sections
        // This is complex for in-memory, so we'll assume CodeGen handles data sections.
    }
}

void Windows_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "  push rbp\n  mov rbp, rsp\n";
        *os << "  push rbx\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n";
    } else {
        auto& as = cg.getAssembler();
        as.emitByte(0x55); as.emitBytes({0x48, 0x89, 0xE5}); // push rbp; mov rbp, rsp
        as.emitByte(0x53); as.emitByte(0x56); as.emitByte(0x57); // push rbx; rsi; rdi
        as.emitBytes({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}); // push r12; r13; r14; r15
    }
    int current_offset = -64;
    for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } } }
    int stack_alloc = std::abs(current_offset + 56);
    stack_alloc += 32; // Shadow space
    // Total pushed: 8 regs (rbp, rbx, rsi, rdi, r12, r13, r14, r15) = 64 bytes.
    if ((stack_alloc + 64 + 8) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 64 + 8) % 16);
    if (auto* os = cg.getTextStream()) {
        if (stack_alloc > 0) *os << "  sub rsp, " << stack_alloc << "\n";
        int j = 0; for (auto& param : func.getParameters()) { if (j < 4) *os << "  mov " << formatStackOperand(cg.getStackOffsets()[param.get()]) << ", " << integerArgRegs[j] << "\n"; j++; }
    } else {
        auto& as = cg.getAssembler();
        if (stack_alloc > 0) { if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc}); else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); } }
        int j = 0; for (auto& param : func.getParameters()) { if (j < 4) { uint8_t r = getArchRegIndex(integerArgRegs[j]); emitRegMem(as, (r >= 8 ? 0x4C : 0x48), 0x89, r & 7, cg.getStackOffsets()[param.get()]); } j++; }
    }
}

void Windows_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getFunctionEpilogueLabel(func) << ":\n";
        *os << "  lea rsp, [rbp - 56]\n";
        *os << "  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbx\n";
        *os << "  leave\n  ret\n";
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo s; s.name = getFunctionEpilogueLabel(func); s.sectionName = ".text"; s.value = as.getCodeSize(); s.type = 0; s.binding = 0; cg.addSymbol(s);
        as.emitBytes({0x48, 0x8D, 0x65, 0xC8}); // lea rsp, [rbp - 56]
        as.emitBytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C}); // pop r15-r12
        as.emitByte(0x5F); as.emitByte(0x5E); as.emitByte(0x5B); // pop rdi, rsi, rbx
        as.emitByte(0xC9); as.emitByte(0xC3); // leave; ret
    }
}

void Windows_x64::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { if (!i.getOperands().empty()) *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n"; *os << "  jmp " << getFunctionEpilogueLabel(*i.getParent()->getParent()) << "\n"; }
    else { if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitByte(0xE9); uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getFunctionEpilogueLabel(*i.getParent()->getParent()), ".text"}); }
}

void Windows_x64::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  add rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x01, 0xC8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  sub rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x29, 0xC8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  imul rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  cqo\n  mov rcx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  idiv rcx\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitBytes({0x48, 0x99}); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitRem(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  cqo\n  mov rcx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  idiv rcx\n  mov " << cg.getValueAsOperand(&i) << ", rdx\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitBytes({0x48, 0x99}); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9}); emitStoreResult(cg, i, 2); }
}
void Windows_x64::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)5); ++j) *os << "  mov " << integerArgRegs[j-1] << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n"; *os << "  call " << i.getOperands()[0]->get()->getName() << "\n"; if (i.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)5); ++j) { uint8_t r = getArchRegIndex(integerArgRegs[j-1]); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), r); } cg.getAssembler().emitByte(0xE8); uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getOperands()[0]->get()->getName(), ".text"}); if (i.getType()->getTypeID() != ir::Type::VoidTyID) emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { std::string set; switch(i.getOpcode()){case ir::Instruction::Ceq:set="sete";break;case ir::Instruction::Cne:set="setne";break;case ir::Instruction::Cslt:set="setl";break;case ir::Instruction::Csle:set="setle";break;case ir::Instruction::Csgt:set="setg";break;case ir::Instruction::Csge:set="setge";break;default:set="sete";break;} *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  cmp rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  " << set << " al\n  movzx eax, al\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x39, 0xC8}); uint8_t s = 0x94; switch(i.getOpcode()){case ir::Instruction::Ceq:s=0x94;break;case ir::Instruction::Cne:s=0x95;break;case ir::Instruction::Cslt:s=0x9C;break;case ir::Instruction::Csle:s=0x9E;break;case ir::Instruction::Csgt:s=0x9F;break;case ir::Instruction::Csge:s=0x9D;break;default:s=0x94;break;} cg.getAssembler().emitBytes({0x0F, s, 0xC0, 0x48, 0x0F, 0xB6, 0xC0}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  test rax, rax\n  jne " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())) << "\n  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())) << "\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitBytes({0x48, 0x85, 0xC0, 0x0F, 0x85}); uint64_t off1 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())), ".text"}); cg.getAssembler().emitByte(0xE9); uint64_t off2 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())), ".text"}); }
}
void Windows_x64::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())) << "\n"; }
    else { cg.getAssembler().emitByte(0xE9); uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())), ".text"}); }
}
void Windows_x64::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  and rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x21, 0xC8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  or rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x09, 0xC8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  xor rax, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0x31, 0xC8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  mov rcx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  shl rax, cl\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0xD3, 0xE0}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  mov rcx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  shr rax, cl\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0xD3, 0xE8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  mov rcx, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  sar rax, cl\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1); cg.getAssembler().emitBytes({0x48, 0xD3, 0xF8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  neg rax\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitBytes({0x48, 0xF7, 0xD8}); emitStoreResult(cg, i, 0); }
}
void Windows_x64::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  not rax\n  mov " << cg.getValueAsOperand(&i) << ", rax\n"; }
    else { emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0); cg.getAssembler().emitBytes({0x48, 0xF7, 0xD0}); emitStoreResult(cg, i, 0); }
}

void Windows_x64::emitLoad(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8; bool isSigned = true;
    switch(i.getOpcode()) {
        case ir::Instruction::Loadub: size = 1; isSigned = false; break;
        case ir::Instruction::Loadsb: size = 1; isSigned = true; break;
        case ir::Instruction::Loaduh: size = 2; isSigned = false; break;
        case ir::Instruction::Loadsh: size = 2; isSigned = true; break;
        case ir::Instruction::Loaduw: size = 4; isSigned = false; break;
        case ir::Instruction::Loadl:  size = 8; isSigned = true; break;
        default: size = 4; break;
    }
    if (auto* os = cg.getTextStream()) {
        std::string op = cg.getValueAsOperand(i.getOperands()[0]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr;
        if (isGlobal) {
            if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr " : "  movzx rax, byte ptr ") << op << "\n";
            else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr " : "  movzx rax, word ptr ") << op << "\n";
            else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr " : "  mov eax, dword ptr ") << op << "\n";
            else *os << "  mov rax, " << op << "\n";
        } else {
            *os << "  mov rax, " << op << "\n";
            if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr [rax]\n" : "  movzx rax, byte ptr [rax]\n");
            else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr [rax]\n" : "  movzx rax, word ptr [rax]\n");
            else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr [rax]\n" : "  mov eax, dword ptr [rax]\n");
            else *os << "  mov rax, [rax]\n";
        }
        *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    } else {
        auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        if (size == 1) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBE : 0xB6), 0x00});
        else if (size == 2) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBF : 0xB7), 0x00});
        else if (size == 4) as.emitBytes(isSigned ? std::vector<uint8_t>{0x48, 0x63, 0x00} : std::vector<uint8_t>{0x8B, 0x00});
        else as.emitBytes({0x48, 0x8B, 0x00});
        emitStoreResult(cg, i, 0);
    }
}

void Windows_x64::emitStore(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8;
    switch(i.getOpcode()) {
        case ir::Instruction::Storeb: size = 1; break;
        case ir::Instruction::Storeh: size = 2; break;
        case ir::Instruction::Storel: size = 8; break;
        default: size = 4; break;
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  mov rax, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        std::string op = cg.getValueAsOperand(i.getOperands()[1]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr;
        if (isGlobal) {
            if (size == 1) *os << "  mov byte ptr " << op << ", al\n";
            else if (size == 2) *os << "  mov word ptr " << op << ", ax\n";
            else if (size == 4) *os << "  mov dword ptr " << op << ", eax\n";
            else *os << "  mov " << op << ", rax\n";
        } else {
            *os << "  mov rdx, " << op << "\n";
            if (size == 1) *os << "  mov byte ptr [rdx], al\n";
            else if (size == 2) *os << "  mov word ptr [rdx], ax\n";
            else if (size == 4) *os << "  mov dword ptr [rdx], eax\n";
            else *os << "  mov [rdx], rax\n";
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

void Windows_x64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    uint64_t size = 8;
    if (instr.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (instr.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!instr.getOperands().empty()) { if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get())) size = sizeConst->getValue(); }
    uint64_t alignedSize = (size + 7) & ~7;
    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  mov rax, [rel heap_ptr]\n";
        *os << "  mov " << formatStackOperand(pointerOffset) << ", rax\n";
        *os << "  add rax, " << alignedSize << "\n";
        *os << "  mov [rel heap_ptr], rax\n";
    } else {
        auto& as = cg.getAssembler(); ir::GlobalValue hp_val(cg.module.getContext()->getVoidType(), "heap_ptr"); emitLoadValue(cg, as, &hp_val, 0);
        emitRegMem(as, 0x48, 0x89, 0, pointerOffset); as.emitBytes({0x48, 0x05}); as.emitDWord(alignedSize);
        as.emitBytes({0x48, 0x89, 0x05}); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "heap_ptr", ".text"});
    }
}

void Windows_x64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    ir::SyscallInstruction* si = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = si ? si->getSyscallId() : ir::SyscallId::None;
    if (sid == ir::SyscallId::Exit) {
        if (auto* os = cg.getTextStream()) { *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call ExitProcess\n"; }
        else { auto& as = cg.getAssembler(); emitLoadValue(cg, as, instr.getOperands()[0]->get(), 1); as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "ExitProcess", ".text"}); }
    }
}

void Windows_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const std::string& cap = ei->getCapability();
    if (auto* os = cg.getTextStream()) {
        if (cap == "io.write") {
             // Windows write: WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped)
             *os << "  sub rsp, 48\n";
             *os << "  mov rcx, -11\n"; // STD_OUTPUT_HANDLE
             *os << "  call GetStdHandle\n";
             *os << "  mov rcx, rax\n"; // hFile
             *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n"; // lpBuffer
             *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n"; // nNumberOfBytesToWrite
             *os << "  lea r9, [rsp + 40]\n"; // lpNumberOfBytesWritten
             *os << "  mov qword ptr [rsp + 32], 0\n"; // lpOverlapped (5th arg)
             *os << "  call WriteFile\n";
             *os << "  add rsp, 48\n";
        } else if (cap == "io.open") {
             // CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
             *os << "  sub rsp, 64\n";
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  mov rdx, 3221225472\n"; // GENERIC_READ | GENERIC_WRITE (0xC0000000)
             *os << "  mov r8, 1\n"; // FILE_SHARE_READ
             *os << "  xor r9, r9\n";
             *os << "  mov qword ptr [rsp + 32], 3\n"; // OPEN_EXISTING
             *os << "  mov qword ptr [rsp + 40], 128\n"; // FILE_ATTRIBUTE_NORMAL
             *os << "  mov qword ptr [rsp + 48], 0\n";
             *os << "  call CreateFileA\n";
             *os << "  add rsp, 64\n";
        } else if (cap == "io.close") {
             *os << "  sub rsp, 40\n";
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  call CloseHandle\n";
             *os << "  add rsp, 40\n";
        } else if (cap == "io.read") {
             // ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped)
             *os << "  sub rsp, 48\n";
             *os << "  mov rcx, -10\n"; // STD_INPUT_HANDLE
             *os << "  call GetStdHandle\n";
             *os << "  mov rcx, rax\n";
             *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
             *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
             *os << "  lea r9, [rsp + 40]\n";
             *os << "  mov qword ptr [rsp + 32], 0\n"; // lpOverlapped (5th arg)
             *os << "  call ReadFile\n";
             *os << "  add rsp, 48\n";
        } else if (cap == "process.exit") {
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  call ExitProcess\n";
        } else if (cap == "process.abort") {
            *os << "  call abort\n";
        } else if (cap == "process.sleep") {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  call Sleep\n";
            *os << "  add rsp, 40\n";
        } else if (cap == "memory.alloc") {
             // VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
             *os << "  sub rsp, 40\n";
             *os << "  xor rcx, rcx\n"; // lpAddress
             *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n"; // dwSize
             *os << "  mov r8, 12288\n"; // MEM_COMMIT | MEM_RESERVE (0x3000)
             *os << "  mov r9, 4\n";     // PAGE_READWRITE
             *os << "  call VirtualAlloc\n";
             *os << "  add rsp, 40\n";
             *os << "  mov " << cg.getValueAsOperand(&instr) << ", rax\n";
             return;
        } else if (cap == "memory.free") {
             // VirtualFree(addr, 0, MEM_RELEASE)
             *os << "  sub rsp, 40\n";
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  xor rdx, rdx\n"; // dwSize must be 0 for MEM_RELEASE
             *os << "  mov r8, 32768\n"; // MEM_RELEASE (0x8000)
             *os << "  call VirtualFree\n";
             *os << "  add rsp, 40\n";
        } else if (cap == "random.u64") {
             // BCryptGenRandom(NULL, &buf, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG)
             *os << "  sub rsp, 48\n";
             *os << "  xor rcx, rcx\n";
             *os << "  lea rdx, [rsp + 32]\n";
             *os << "  mov r8, 8\n";
             *os << "  mov r9, 2\n"; // BCRYPT_USE_SYSTEM_PREFERRED_RNG
             *os << "  call BCryptGenRandom\n";
             *os << "  mov rax, [rsp + 32]\n";
             *os << "  add rsp, 48\n";
        } else if (cap == "time.now") {
             // GetSystemTimeAsFileTime(&ft)
             *os << "  sub rsp, 40\n";
             *os << "  lea rcx, [rsp + 32]\n";
             *os << "  call GetSystemTimeAsFileTime\n";
             *os << "  mov rax, [rsp + 32]\n";
             *os << "  add rsp, 40\n";
        } else if (cap == "sync.mutex.lock") {
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  mov rax, 1\n";
             *os << "  .Lmutex_retry_" << cg.labelCounter << ":\n";
             *os << "  lock xchg qword ptr [rcx], rax\n";
             *os << "  test rax, rax\n";
             *os << "  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
             cg.labelCounter++;
        } else if (cap == "sync.mutex.unlock") {
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  mov qword ptr [rcx], 0\n";
        } else if (cap == "time.monotonic") {
             *os << "  sub rsp, 40\n";
             *os << "  lea rcx, [rsp + 32]\n";
             *os << "  call QueryPerformanceCounter\n";
             *os << "  mov rax, [rsp + 32]\n";
             *os << "  add rsp, 40\n";
        } else if (cap == "error.get") {
             *os << "  sub rsp, 40\n";
             *os << "  call GetLastError\n";
             *os << "  add rsp, 40\n";
        } else if (cap == "debug.log") {
             *os << "  sub rsp, 40\n";
             *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
             *os << "  call OutputDebugStringA\n";
             *os << "  add rsp, 40\n";
        }
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  mov " << cg.getValueAsOperand(&instr) << ", rax\n";
    }
}

void Windows_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _start\n_start:\n  sub rsp, 40\n  call main\n  mov rcx, rax\n  call ExitProcess\n";
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = as.getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        as.emitBytes({0x48, 0x83, 0xEC, 0x28}); // sub rsp, 40
        as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "main", ".text"});
        as.emitBytes({0x48, 0x89, 0xC1}); // mov rcx, rax
        as.emitByte(0xE8); uint64_t off2 = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, "ExitProcess", ".text"});
    }
}

std::string Windows_x64::formatStackOperand(int offset) const { return "[rbp + " + std::to_string(offset) + "]"; }
std::string Windows_x64::formatGlobalOperand(const std::string& name) const { return "[rel " + name + "]"; }

}} // namespace codegen::target
