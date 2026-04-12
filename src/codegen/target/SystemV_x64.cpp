#include "codegen/target/SystemV_x64.h"
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

SystemV_x64::SystemV_x64() { initRegisters(); }

void SystemV_x64::initRegisters() {
    integerRegs = {"r10", "r11", "rbx", "r12", "r13", "r14", "r15"};
    floatRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    integerArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    intReturnReg = "rax"; floatReturnReg = "xmm0"; framePtrReg = "rbp"; stackPtrReg = "rsp";
    for (const auto& r : integerRegs) { callerSaved[r] = false; calleeSaved[r] = true; }
}

void SystemV_x64::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .data\n.align 8\nheap_ptr:\n  .quad __fyra_heap\n";
        *os << ".section .bss\n.align 16\n__fyra_heap:\n  .zero 1048576\n";
        *os << ".text\n";
    }
}

void SystemV_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "  pushq %rbp\n  movq %rsp, %rbp\n";
        *os << "  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n";
    }
    // Saved: rbp (0), rbx (-8), r12 (-16), r13 (-24), r14 (-32), r15 (-40)
    int current_offset = -48;
    for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } } }
    int stack_alloc = std::abs(current_offset + 40); // Base on rsp after pushes
    if ((stack_alloc + 48) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 48) % 16);
    if (auto* os = cg.getTextStream()) {
        if (stack_alloc > 0) *os << "  subq $" << stack_alloc << ", %rsp\n";
        int j = 0; for (auto& param : func.getParameters()) { if (j < 6) *os << "  movq %" << integerArgRegs[j] << ", " << formatStackOperand(cg.getStackOffsets()[param.get()]) << "\n"; j++; }
    }
}

void SystemV_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getFunctionEpilogueLabel(func) << ":\n";
        *os << "  leaq -40(%rbp), %rsp\n"; // Move to after saved regs
        *os << "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n";
        *os << "  leave\n  ret\n";
    }
}

void SystemV_x64::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { if (!i.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n"; *os << "  jmp " << getFunctionEpilogueLabel(*i.getParent()->getParent()) << "\n"; }
}

void SystemV_x64::emitAdd(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  addq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitSub(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  subq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitMul(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  imulq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitDiv(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  cqto\n  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rcx\n  idivq %rcx\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitRem(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  cqto\n  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rcx\n  idivq %rcx\n  movq %rdx, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitCopy(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitCall(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { for (size_t j=1; j<std::min(i.getOperands().size(), (size_t)7); ++j) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[j]->get()) << ", %" << integerArgRegs[j-1] << "\n"; *os << "  call " << i.getOperands()[0]->get()->getName() << "\n"; if (i.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitCmp(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { std::string set; switch(i.getOpcode()){case ir::Instruction::Ceq:set="sete";break;case ir::Instruction::Cne:set="setne";break;case ir::Instruction::Cslt:set="setl";break;case ir::Instruction::Csle:set="setle";break;case ir::Instruction::Csgt:set="setg";break;case ir::Instruction::Csge:set="setge";break;default:set="sete";break;} *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  cmpq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  " << set << " %al\n  movzbq %al, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitBr(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  testq %rax, %rax\n  jne " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())) << "\n  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())) << "\n"; } }
void SystemV_x64::emitJmp(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  jmp " << getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())) << "\n"; } }
void SystemV_x64::emitAnd(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  andq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitOr(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  orq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitXor(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  xorq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitShl(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rcx\n  shlq %cl, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitShr(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rcx\n  shrq %cl, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitSar(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", %rcx\n  sarq %cl, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitNeg(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  negq %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }
void SystemV_x64::emitNot(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) { *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n  notq %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n"; } }

void SystemV_x64::emitLoad(CodeGen& cg, ir::Instruction& i) {
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
            if (size == 1) *os << (isSigned ? "  movsbq " : "  movzbq ") << op << ", %rax\n";
            else if (size == 2) *os << (isSigned ? "  movswq " : "  movzwq ") << op << ", %rax\n";
            else if (size == 4) *os << (isSigned ? "  movslq " : "  movl ") << op << ", %eax\n";
            else *os << "  movq " << op << ", %rax\n";
        } else {
            *os << "  movq " << op << ", %rax\n";
            if (size == 1) *os << (isSigned ? "  movsbq (%rax), %rax\n" : "  movzbq (%rax), %rax\n");
            else if (size == 2) *os << (isSigned ? "  movswq (%rax), %rax\n" : "  movzwq (%rax), %rax\n");
            else if (size == 4) *os << (isSigned ? "  movslq (%rax), %rax\n" : "  movl (%rax), %eax\n");
            else *os << "  movq (%rax), %rax\n";
        }
        *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void SystemV_x64::emitStore(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8;
    switch(i.getOpcode()) {
        case ir::Instruction::Storeb: size = 1; break;
        case ir::Instruction::Storeh: size = 2; break;
        case ir::Instruction::Storel: size = 8; break;
        default: size = 4; break;
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
        std::string op = cg.getValueAsOperand(i.getOperands()[1]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr;
        if (isGlobal) {
            if (size == 1) *os << "  movb %al, " << op << "\n";
            else if (size == 2) *os << "  movw %ax, " << op << "\n";
            else if (size == 4) *os << "  movl %eax, " << op << "\n";
            else *os << "  movq %rax, " << op << "\n";
        } else {
            *os << "  movq " << op << ", %rdx\n";
            if (size == 1) *os << "  movb %al, (%rdx)\n";
            else if (size == 2) *os << "  movw %ax, (%rdx)\n";
            else if (size == 4) *os << "  movl %eax, (%rdx)\n";
            else *os << "  movq %rax, (%rdx)\n";
        }
    }
}

void SystemV_x64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    uint64_t size = 8;
    if (instr.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (instr.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!instr.getOperands().empty()) { if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get())) size = sizeConst->getValue(); }
    uint64_t alignedSize = (size + 7) & ~7;
    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  movq heap_ptr(%rip), %rax\n";
        *os << "  movq %rax, " << formatStackOperand(pointerOffset) << "\n";
        *os << "  addq $" << alignedSize << ", %rax\n";
        *os << "  movq %rax, heap_ptr(%rip)\n";
    }
}

void SystemV_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _start\n_start:\n  call main\n  movq %rax, %rdi\n  movq $60, %rax\n  syscall\n";
    }
}

uint64_t SystemV_x64::getSyscallNumber(ir::SyscallId id) const {
    switch(id) {
        case ir::SyscallId::Exit: return 60; case ir::SyscallId::Write: return 1; case ir::SyscallId::Read: return 0; case ir::SyscallId::OpenAt: return 2; case ir::SyscallId::Close: return 3; default: return 0;
    }
}

void SystemV_x64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    ir::SyscallInstruction* si = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = si ? si->getSyscallId() : ir::SyscallId::None;
    if (auto* os = cg.getTextStream()) {
        if (sid != ir::SyscallId::None) *os << "  movq $" << getSyscallNumber(sid) << ", %rax\n"; else *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rax\n";
        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i + 1 : i; std::string dest_reg;
            switch(argIdx) { case 1: dest_reg = "%rdi"; break; case 2: dest_reg = "%rsi"; break; case 3: dest_reg = "%rdx"; break; case 4: dest_reg = "%r10"; break; case 5: dest_reg = "%r8"; break; case 6: dest_reg = "%r9"; break; }
            if (!dest_reg.empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << dest_reg << "\n";
        }
        *os << "  syscall\n"; if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void SystemV_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const std::string& cap = ei->getCapability();
    if (auto* os = cg.getTextStream()) {
        if (cap == "io.write") {
            *os << "  movq $1, %rax\n"; // sys_write
            for (size_t i = 0; i < instr.getOperands().size(); ++i) {
                std::string dest_reg;
                switch(i + 1) { case 1: dest_reg = "%rdi"; break; case 2: dest_reg = "%rsi"; break; case 3: dest_reg = "%rdx"; break; case 4: dest_reg = "%r10"; break; case 5: dest_reg = "%r8"; break; case 6: dest_reg = "%r9"; break; }
                if (!dest_reg.empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << dest_reg << "\n";
            }
            *os << "  syscall\n";
        } else if (cap == "io.read") {
            *os << "  movq $0, %rax\n"; // sys_read
            for (size_t i = 0; i < instr.getOperands().size(); ++i) {
                std::string dest_reg;
                switch(i + 1) { case 1: dest_reg = "%rdi"; break; case 2: dest_reg = "%rsi"; break; case 3: dest_reg = "%rdx"; break; case 4: dest_reg = "%r10"; break; case 5: dest_reg = "%r8"; break; case 6: dest_reg = "%r9"; break; }
                if (!dest_reg.empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << dest_reg << "\n";
            }
            *os << "  syscall\n";
        } else if (cap == "io.open") {
            *os << "  movq $2, %rax\n"; // sys_open
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
            *os << "  syscall\n";
        } else if (cap == "io.close") {
            *os << "  movq $3, %rax\n"; // sys_close
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  syscall\n";
        } else if (cap == "io.seek") {
            *os << "  movq $8, %rax\n"; // sys_lseek
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
            *os << "  syscall\n";
        } else if (cap == "io.stat") {
            *os << "  movq $4, %rax\n"; // sys_stat
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  syscall\n";
        } else if (cap == "process.exit") {
            *os << "  movq $60, %rax\n"; // sys_exit
            if (!instr.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  syscall\n";
        } else if (cap == "process.abort") {
            *os << "  movq $39, %rax\n"; // sys_getpid
            *os << "  syscall\n";
            *os << "  movq %rax, %rdi\n"; // pid
            *os << "  movq $6, %rsi\n"; // SIGABRT
            *os << "  movq $62, %rax\n"; // sys_kill
            *os << "  syscall\n";
        } else if (cap == "process.sleep") {
            // nanosleep implementation
            *os << "  subq $16, %rsp\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rax\n";
            *os << "  movq $1000, %rcx\n";
            *os << "  xorq %rdx, %rdx\n";
            *os << "  divq %rcx\n"; // rax = sec, rdx = ms
            *os << "  movq %rax, (%rsp)\n"; // tv_sec
            *os << "  imulq $1000000, %rdx, %rdx\n";
            *os << "  movq %rdx, 8(%rsp)\n"; // tv_nsec
            *os << "  movq $35, %rax\n"; // sys_nanosleep
            *os << "  movq %rsp, %rdi\n";
            *os << "  xorq %rsi, %rsi\n";
            *os << "  syscall\n";
            *os << "  addq $16, %rsp\n";
        } else if (cap == "memory.alloc") {
            *os << "  movq $9, %rax\n"; // sys_mmap
            if (instr.getOperands().size() == 1) {
                *os << "  xorq %rdi, %rdi\n"; // addr
                *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n"; // len
                *os << "  movq $3, %rdx\n"; // prot
                *os << "  movq $34, %r10\n"; // flags
                *os << "  movq $-1, %r8\n"; // fd
                *os << "  xorq %r9, %r9\n"; // off
            } else {
                for (size_t i = 0; i < std::min(instr.getOperands().size(), (size_t)6); ++i) {
                    std::string dest_reg;
                    switch(i + 1) { case 1: dest_reg = "%rdi"; break; case 2: dest_reg = "%rsi"; break; case 3: dest_reg = "%rdx"; break; case 4: dest_reg = "%r10"; break; case 5: dest_reg = "%r8"; break; case 6: dest_reg = "%r9"; break; }
                    if (!dest_reg.empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << dest_reg << "\n";
                }
            }
            *os << "  syscall\n";
        } else if (cap == "memory.map") {
            *os << "  movq $9, %rax\n"; // sys_mmap
            for (size_t i = 0; i < std::min(instr.getOperands().size(), (size_t)6); ++i) {
                std::string dest_reg;
                switch(i + 1) { case 1: dest_reg = "%rdi"; break; case 2: dest_reg = "%rsi"; break; case 3: dest_reg = "%rdx"; break; case 4: dest_reg = "%r10"; break; case 5: dest_reg = "%r8"; break; case 6: dest_reg = "%r9"; break; }
                if (!dest_reg.empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << dest_reg << "\n";
            }
            *os << "  syscall\n";
        } else if (cap == "memory.free") {
            // munmap(addr, size)
            *os << "  movq $11, %rax\n"; // sys_munmap
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  syscall\n";
        } else if (cap == "memory.protect") {
            *os << "  movq $10, %rax\n"; // sys_mprotect
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
            *os << "  syscall\n";
        } else if (cap == "random.u64") {
            // getrandom(&buf, 8, 0)
            *os << "  subq $8, %rsp\n";
            *os << "  movq $318, %rax\n"; // sys_getrandom
            *os << "  movq %rsp, %rdi\n";
            *os << "  movq $8, %rsi\n";
            *os << "  xorq %rdx, %rdx\n";
            *os << "  syscall\n";
            *os << "  popq %rax\n";
        } else if (cap == "time.now") {
            // clock_gettime(CLOCK_REALTIME, &ts)
            *os << "  subq $16, %rsp\n";
            *os << "  movq $228, %rax\n"; // sys_clock_gettime
            *os << "  movq $0, %rdi\n";   // CLOCK_REALTIME
            *os << "  movq %rsp, %rsi\n";
            *os << "  syscall\n";
            *os << "  movq (%rsp), %rax\n"; // tv_sec
            *os << "  addq $16, %rsp\n";
        } else if (cap == "time.monotonic") {
            *os << "  subq $16, %rsp\n";
            *os << "  movq $228, %rax\n"; // sys_clock_gettime
            *os << "  movq $1, %rdi\n";   // CLOCK_MONOTONIC
            *os << "  movq %rsp, %rsi\n";
            *os << "  syscall\n";
            *os << "  movq (%rsp), %rax\n";
            *os << "  imulq $1000000000, %rax, %rax\n";
            *os << "  addq 8(%rsp), %rax\n"; // total ns
            *os << "  addq $16, %rsp\n";
        } else if (cap == "error.get") {
            // This is a bit tricky as errno is in TLS.
            // Simplified: we'll just assume we can call a __errno_location style thing if libc is there
            // or just return a dummy for now. Industrial would use TLS.
            *os << "  xorq %rax, %rax\n";
        } else if (cap == "sync.mutex.lock") {
             // simplified spinlock for demonstration, industrial would use futex
             *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
             *os << "  movq $1, %rax\n";
             *os << "  .Lmutex_retry_" << cg.labelCounter << ":\n";
             *os << "  xchgq %rax, (%rdi)\n";
             *os << "  testq %rax, %rax\n";
             *os << "  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
             cg.labelCounter++;
        } else if (cap == "sync.mutex.unlock") {
             *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
             *os << "  movq $0, (%rdi)\n";
        } else if (cap == "debug.log") {
            // log to stderr
            *os << "  movq $1, %rax\n"; // sys_write
            *os << "  movq $2, %rdi\n"; // stderr
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
            *os << "  movq $128, %rdx\n"; // fixed length for simplicity
            *os << "  syscall\n";
        }
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

}} // namespace codegen::target
