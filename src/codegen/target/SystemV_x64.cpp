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
        *os << ".section .rodata\n.Lproc_environ:\n  .string \"/proc/self/environ\"\n";
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
    // Ensure stack is 16-byte aligned before call. Since we pushed 6 8-byte regs (rbp, rbx, r12, r13, r14, r15),
    // total pushed = 48 bytes. (48 + stack_alloc + 8 (ret addr)) % 16 == 0.
    if ((stack_alloc + 48 + 8) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 48 + 8) % 16);
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

namespace {
void emitLinuxSyscallArgs(CodeGen& cg, ir::Instruction& instr, size_t maxArgs = 6) {
    static const char* regs[] = {"%rdi", "%rsi", "%rdx", "%r10", "%r8", "%r9"};
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(instr.getOperands().size(), maxArgs); ++i) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << regs[i] << "\n";
        }
    }
}
void emitStoreExternResult(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}
}

bool SystemV_x64::supportsCapability(const CapabilitySpec& spec) const {
    switch (spec.id) {
        case CapabilityId::IO_READ:
        case CapabilityId::IO_WRITE:
        case CapabilityId::IO_OPEN:
        case CapabilityId::IO_CLOSE:
        case CapabilityId::IO_SEEK:
        case CapabilityId::IO_STAT:
        case CapabilityId::FS_OPEN:
        case CapabilityId::FS_CREATE:
        case CapabilityId::FS_STAT:
        case CapabilityId::FS_REMOVE:
        case CapabilityId::MEMORY_ALLOC:
        case CapabilityId::MEMORY_FREE:
        case CapabilityId::MEMORY_MAP:
        case CapabilityId::MEMORY_PROTECT:
        case CapabilityId::PROCESS_EXIT:
        case CapabilityId::PROCESS_ABORT:
        case CapabilityId::PROCESS_SLEEP:
        case CapabilityId::PROCESS_SPAWN:
        case CapabilityId::SYNC_MUTEX_LOCK:
        case CapabilityId::SYNC_MUTEX_UNLOCK:
        case CapabilityId::TIME_NOW:
        case CapabilityId::TIME_MONOTONIC:
        case CapabilityId::EVENT_POLL:
        case CapabilityId::NET_SOCKET:
        case CapabilityId::NET_CONNECT:
        case CapabilityId::NET_LISTEN:
        case CapabilityId::NET_ACCEPT:
        case CapabilityId::NET_SEND:
        case CapabilityId::NET_RECV:
        case CapabilityId::IPC_SEND:
        case CapabilityId::IPC_RECV:
        case CapabilityId::ENV_GET:
        case CapabilityId::ENV_LIST:
        case CapabilityId::SYSTEM_INFO:
        case CapabilityId::SIGNAL_SEND:
        case CapabilityId::SIGNAL_REGISTER:
        case CapabilityId::RANDOM_U64:
        case CapabilityId::ERROR_GET:
        case CapabilityId::DEBUG_LOG:
        case CapabilityId::MODULE_LOAD:
        case CapabilityId::TTY_ISATTY:
        case CapabilityId::SECURITY_CHMOD:
        case CapabilityId::THREAD_SPAWN:
        case CapabilityId::THREAD_JOIN:
            return true;
        default:
            return false;
    }
}

void SystemV_x64::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::IO_WRITE: *os << "  movq $1, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_READ: *os << "  movq $0, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_OPEN: *os << "  movq $2, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_CLOSE: *os << "  movq $3, %rax\n"; emitLinuxSyscallArgs(cg, instr, 1); *os << "  syscall\n"; break;
            case CapabilityId::IO_SEEK: *os << "  movq $8, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_STAT: *os << "  movq $4, %rax\n"; emitLinuxSyscallArgs(cg, instr, 2); *os << "  syscall\n"; break;
            case CapabilityId::IO_FLUSH: *os << "  xorq %rax, %rax\n"; break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id == CapabilityId::FS_OPEN || spec.id == CapabilityId::FS_CREATE) {
        emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 2, 3, true, true});
        return;
    }
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::FS_STAT: *os << "  movq $4, %rax\n"; emitLinuxSyscallArgs(cg, instr, 2); *os << "  syscall\n"; break;
            case CapabilityId::FS_REMOVE: *os << "  movq $87, %rax\n"; emitLinuxSyscallArgs(cg, instr, 1); *os << "  syscall\n"; break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::MEMORY_ALLOC:
                *os << "  movq $9, %rax\n";
                if (instr.getOperands().size() == 1) {
                    *os << "  xorq %rdi, %rdi\n";
                    *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
                    *os << "  movq $3, %rdx\n  movq $34, %r10\n  movq $-1, %r8\n  xorq %r9, %r9\n";
                } else emitLinuxSyscallArgs(cg, instr, 6);
                *os << "  syscall\n";
                break;
            case CapabilityId::MEMORY_MAP: *os << "  movq $9, %rax\n"; emitLinuxSyscallArgs(cg, instr, 6); *os << "  syscall\n"; break;
            case CapabilityId::MEMORY_FREE: *os << "  movq $11, %rax\n"; emitLinuxSyscallArgs(cg, instr, 2); *os << "  syscall\n"; break;
            case CapabilityId::MEMORY_PROTECT: *os << "  movq $10, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::PROCESS_EXIT: *os << "  movq $60, %rax\n"; emitLinuxSyscallArgs(cg, instr, 1); *os << "  syscall\n"; break;
            case CapabilityId::PROCESS_ABORT:
                *os << "  movq $39, %rax\n  syscall\n  movq %rax, %rdi\n  movq $6, %rsi\n  movq $62, %rax\n  syscall\n";
                break;
            case CapabilityId::PROCESS_SLEEP:
                *os << "  subq $16, %rsp\n  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rax\n";
                *os << "  movq $1000, %rcx\n  xorq %rdx, %rdx\n  divq %rcx\n  movq %rax, (%rsp)\n";
                *os << "  imulq $1000000, %rdx, %rdx\n  movq %rdx, 8(%rsp)\n  movq $35, %rax\n";
                *os << "  movq %rsp, %rdi\n  xorq %rsi, %rsi\n  syscall\n  addq $16, %rsp\n";
                break;
            case CapabilityId::PROCESS_SPAWN:
                *os << "  movq $57, %rax\n  syscall\n"; // fork
                *os << "  testq %rax, %rax\n";
                *os << "  jnz .Lspawn_parent_" << cg.labelCounter << "\n";
                *os << "  movq $59, %rax\n"; // execve(path, argv, envp)
                *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
                if (instr.getOperands().size() > 1) {
                    *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
                } else {
                    *os << "  xorq %rsi, %rsi\n";
                }
                if (instr.getOperands().size() > 2) {
                    *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
                } else {
                    *os << "  xorq %rdx, %rdx\n";
                }
                *os << "  syscall\n";
                *os << "  movq $60, %rax\n  movq $127, %rdi\n  syscall\n";
                *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
                cg.labelCounter++;
                break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::THREAD_SPAWN:
                *os << "  movq $56, %rax\n"; // clone
                emitLinuxSyscallArgs(cg, instr, 5);
                *os << "  syscall\n";
                break;
            case CapabilityId::THREAD_JOIN:
                *os << "  movq $61, %rax\n"; // wait4
                emitLinuxSyscallArgs(cg, instr, 1);
                *os << "  xorq %rsi, %rsi\n  xorq %rdx, %rdx\n  xorq %r10, %r10\n";
                *os << "  syscall\n";
                break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::EVENT_POLL) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $232, %rax\n"; // epoll_wait
        if (instr.getOperands().size() >= 4) {
            emitLinuxSyscallArgs(cg, instr, 4);
        } else {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  subq $16, %rsp\n  movq %rsp, %rsi\n  movq $1, %rdx\n  xorq %r10, %r10\n";
            *os << "  syscall\n  addq $16, %rsp\n";
            emitStoreExternResult(cg, instr);
            return;
        }
        *os << "  syscall\n";
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::IPC_SEND) {
            *os << "  movq $1, %rax\n";
            emitLinuxSyscallArgs(cg, instr, 3);
            *os << "  syscall\n";
        } else if (spec.id == CapabilityId::IPC_RECV) {
            *os << "  movq $0, %rax\n";
            emitLinuxSyscallArgs(cg, instr, 3);
            *os << "  syscall\n";
        } else {
            emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        if (spec.id != CapabilityId::ENV_GET && spec.id != CapabilityId::ENV_LIST) {
            emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
        *os << "  movq $257, %rax\n"; // openat
        *os << "  movq $-100, %rdi\n";
        *os << "  leaq .Lproc_environ(%rip), %rsi\n";
        *os << "  xorq %rdx, %rdx\n  xorq %r10, %r10\n  syscall\n";
        *os << "  movq %rax, %r12\n";
        *os << "  movq $0, %rax\n  movq %r12, %rdi\n";
        if (!instr.getOperands().empty()) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        } else {
            *os << "  subq $4096, %rsp\n  movq %rsp, %rsi\n";
        }
        *os << "  movq $4096, %rdx\n  syscall\n";
        *os << "  movq %rax, %r13\n";
        *os << "  movq $3, %rax\n  movq %r12, %rdi\n  syscall\n";
        *os << "  movq %r13, %rax\n";
        if (instr.getOperands().empty()) {
            *os << "  addq $4096, %rsp\n";
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::MODULE_LOAD) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $257, %rax\n  movq $-100, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  xorq %rdx, %rdx\n  xorq %r10, %r10\n  syscall\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq $313, %rax\n"; // finit_module
        if (instr.getOperands().size() > 1) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        } else {
            *os << "  xorq %rsi, %rsi\n";
        }
        *os << "  xorq %rdx, %rdx\n  syscall\n";
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::TTY_ISATTY) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  subq $64, %rsp\n";
        *os << "  movq $16, %rax\n"; // ioctl
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq $0x5401, %rsi\n"; // TCGETS
        *os << "  movq %rsp, %rdx\n  syscall\n";
        *os << "  cmpq $0, %rax\n  sete %al\n  movzbq %al, %rax\n";
        *os << "  addq $64, %rsp\n";
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::SECURITY_CHMOD) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $90, %rax\n";
        emitLinuxSyscallArgs(cg, instr, 2);
        *os << "  syscall\n";
    }
    emitStoreExternResult(cg, instr);
}
void SystemV_x64::emitGPUCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { emitUnsupportedCapability(cg, instr, &spec); }

void SystemV_x64::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::SYNC_MUTEX_LOCK) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq $1, %rax\n  .Lmutex_retry_" << cg.labelCounter << ":\n  xchgq %rax, (%rdi)\n  testq %rax, %rax\n  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
            cg.labelCounter++;
        } else if (spec.id == CapabilityId::SYNC_MUTEX_UNLOCK) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n  movq $0, (%rdi)\n";
        } else {
            emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::TIME_NOW || spec.id == CapabilityId::TIME_MONOTONIC) {
            *os << "  subq $16, %rsp\n  movq $228, %rax\n";
            *os << "  movq $" << (spec.id == CapabilityId::TIME_NOW ? 0 : 1) << ", %rdi\n";
            *os << "  movq %rsp, %rsi\n  syscall\n  movq (%rsp), %rax\n";
            if (spec.id == CapabilityId::TIME_MONOTONIC) *os << "  imulq $1000000000, %rax, %rax\n  addq 8(%rsp), %rax\n";
            *os << "  addq $16, %rsp\n";
        } else {
            emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::NET_SOCKET: *os << "  movq $41, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::NET_CONNECT: *os << "  movq $42, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::NET_LISTEN: *os << "  movq $50, %rax\n"; emitLinuxSyscallArgs(cg, instr, 2); *os << "  syscall\n"; break;
            case CapabilityId::NET_ACCEPT: *os << "  movq $43, %rax\n"; emitLinuxSyscallArgs(cg, instr, 3); *os << "  syscall\n"; break;
            case CapabilityId::NET_SEND: *os << "  movq $44, %rax\n"; emitLinuxSyscallArgs(cg, instr, 4); *os << "  syscall\n"; break;
            case CapabilityId::NET_RECV: *os << "  movq $45, %rax\n"; emitLinuxSyscallArgs(cg, instr, 4); *os << "  syscall\n"; break;
            default: emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::SIGNAL_SEND) {
            *os << "  movq $62, %rax\n";
            emitLinuxSyscallArgs(cg, instr, 2);
            *os << "  syscall\n";
        } else if (spec.id == CapabilityId::SIGNAL_REGISTER) {
            *os << "  movq $13, %rax\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
            *os << "  xorq %rdx, %rdx\n  movq $8, %r10\n";
            *os << "  syscall\n";
        } else {
            emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::RANDOM_U64) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  subq $8, %rsp\n  movq $318, %rax\n  movq %rsp, %rdi\n  movq $8, %rsi\n  xorq %rdx, %rdx\n  syscall\n  popq %rax\n";
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::ERROR_GET) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) *os << "  xorq %rax, %rax\n";
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::DEBUG_LOG) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $1, %rax\n  movq $2, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  movq $128, %rdx\n  syscall\n";
    }
    emitStoreExternResult(cg, instr);
}

void SystemV_x64::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id != CapabilityId::SYSTEM_INFO) { emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $63, %rax\n"; // uname
        emitLinuxSyscallArgs(cg, instr, 1);
        *os << "  syscall\n";
    }
    emitStoreExternResult(cg, instr);
}

}} // namespace codegen::target
