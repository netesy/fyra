#include "target/os/baremetal/BareMetalOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace target {

void BareMetalOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # BareMetal Freestanding Target Header\n";
        *os << "  .section .bss\n";
        *os << "  .align 8\n";
        *os << "  __baremetal_heap_ptr:\n";
        *os << "    .skip 8\n";
        *os << "  .text\n";
    }
}

void BareMetalOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << ".text\n.globl _start\n_start:\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la sp, __stack_top\n";
            *os << "  la t0, __heap_start\n";
            *os << "  la t1, __baremetal_heap_ptr\n";
            *os << "  sd t0, 0(t1)\n";
            *os << "  call main\n";
            *os << "1: wfi\n  j 1b\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x9, __stack_top\n";
            *os << "  mov sp, x9\n";
            *os << "  adrp x9, __heap_start\n";
            *os << "  adrp x10, __baremetal_heap_ptr\n";
            *os << "  str x9, [x10, :lo12:__baremetal_heap_ptr]\n";
            *os << "  bl main\n";
            *os << "1: wfe\n  b 1b\n";
        } else {
            *os << "  movabs $__stack_top, %rsp\n";
            *os << "  movabs $__heap_start, %rax\n";
            *os << "  movq %rax, __baremetal_heap_ptr(%rip)\n";
            *os << "  call main\n";
            *os << "1: hlt\n  jmp 1b\n";
        }
    }
}

void BareMetalOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal MMIO UART Capability (" << spec.name << ")\n";
        bool isWrite = (spec.id == CapabilityId::IO_WRITE);

        if (targetArch == Arch::RISCV64) {
            *os << "  li t0, 0x10000000 # QEMU virt UART0 MMIO\n";
            if (isWrite) {
                if (i.getOperands().size() > 1 && i.getOperands()[1]) {
                    std::string src = cg.getValueAsOperand(i.getOperands()[1]->get());
                    *os << "  ld t1, " << src << "\n";
                    *os << "  sb t1, 0(t0)\n";
                }
            } else {
                *os << "  lb a0, 0(t0)\n";
                if (i.getType() && !i.getType()->isVoidTy()) {
                    *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                }
            }
        } else if (targetArch == Arch::AArch64) {
            *os << "  mov x9, #0x09000000 # QEMU virt PL011 UART MMIO\n";
            if (isWrite) {
                if (i.getOperands().size() > 1 && i.getOperands()[1]) {
                    std::string src = cg.getValueAsOperand(i.getOperands()[1]->get());
                    *os << "  ldr w10, " << src << "\n";
                    *os << "  strb w10, [x9]\n";
                }
            } else {
                *os << "  ldrb w0, [x9]\n";
                if (i.getType() && !i.getType()->isVoidTy()) {
                    *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
                }
            }
        } else {
            *os << "  mov $0x3F8, %dx # COM1 UART Port\n";
            if (isWrite) {
                if (i.getOperands().size() > 1 && i.getOperands()[1]) {
                    std::string src = cg.getValueAsOperand(i.getOperands()[1]->get());
                    *os << "  movb " << src << ", %al\n";
                    *os << "  outb %al, %dx\n";
                }
            } else {
                *os << "  inb %dx, %al\n";
                if (i.getType() && !i.getType()->isVoidTy()) {
                    *os << "  movzbq %al, %rax\n";
                    *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
                }
            }
        }
    }
}

void BareMetalOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Memory Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::MEMORY_FREE) {
            *os << "  # memory.free: freestanding no-op\n";
            return;
        }

        if (spec.id == CapabilityId::MEMORY_USAGE) {
            if (targetArch == Arch::RISCV64) {
                *os << "  la a0, __baremetal_heap_ptr\n  ld a0, 0(a0)\n";
                *os << "  la a1, __heap_start\n  sub a0, a0, a1\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  adrp x9, __baremetal_heap_ptr\n  ldr x0, [x9, :lo12:__baremetal_heap_ptr]\n";
                *os << "  adrp x10, __heap_start\n  sub x0, x0, x10\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  movq __baremetal_heap_ptr(%rip), %rax\n";
                *os << "  movabs $__heap_start, %rdx\n  sub %rdx, %rax\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
            return;
        }

        std::string allocSize = "64";
        if (!i.getOperands().empty() && i.getOperands()[0]) {
            allocSize = cg.getValueAsOperand(i.getOperands()[0]->get());
        }

        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __baremetal_heap_ptr\n";
            *os << "  ld a1, 0(a0)\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  sd a1, " << cg.getValueAsOperand(&i) << "\n";
            }
            *os << "  ld a2, " << allocSize << "\n";
            *os << "  add a1, a1, a2\n";
            *os << "  sd a1, 0(a0)\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x9, __baremetal_heap_ptr\n";
            *os << "  ldr x10, [x9, :lo12:__baremetal_heap_ptr]\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  str x10, " << cg.getValueAsOperand(&i) << "\n";
            }
            *os << "  ldr x11, " << allocSize << "\n";
            *os << "  add x10, x10, x11\n";
            *os << "  str x10, [x9, :lo12:__baremetal_heap_ptr]\n";
        } else {
            *os << "  movq __baremetal_heap_ptr(%rip), %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
            *os << "  addq " << allocSize << ", %rax\n";
            *os << "  movq %rax, __baremetal_heap_ptr(%rip)\n";
        }
    }
}

void BareMetalOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Process Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::PROCESS_GETPID) {
            if (targetArch == Arch::RISCV64) {
                *os << "  csrr a0, mhartid\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mrs x0, mpidr_el1\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  mov $1, %eax\n  cpuid\n  shr $24, %ebx\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movzbq %bl, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
            return;
        }

        if (spec.id == CapabilityId::PROCESS_SLEEP) {
            if (targetArch == Arch::RISCV64) {
                *os << "  rdtime t0\n  addi t0, t0, 1000\n1: rdtime t1\n  blt t1, t0, 1b\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mrs x9, cntvct_el0\n  add x9, x9, #1000\n1: mrs x10, cntvct_el0\n  cmp x10, x9\n  b.lt 1b\n";
            } else {
                *os << "  rdtsc\n  add $1000, %rax\n1: rdtsc\n  cmp %rax, %rdx\n  jge 1b\n";
            }
            return;
        }

        if (targetArch == Arch::RISCV64) {
            *os << "1: wfi\n  j 1b\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "1: wfe\n  b 1b\n";
        } else {
            *os << "1: hlt\n  jmp 1b\n";
        }
    }
}

void BareMetalOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Spinlock / Atomic Capability (" << spec.name << ")\n";
        if (!i.getOperands().empty() && i.getOperands()[0]) {
            std::string ptr = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (spec.id == CapabilityId::SYNC_MUTEX_LOCK) {
                if (targetArch == Arch::RISCV64) {
                    *os << "  ld a0, " << ptr << "\n";
                    *os << "  li a1, 1\n";
                    *os << "1: amoswap.w.aq a2, a1, (a0)\n";
                    *os << "  bnez a2, 1b\n";
                } else if (targetArch == Arch::AArch64) {
                    *os << "  ldr x9, " << ptr << "\n";
                    *os << "  mov w10, #1\n";
                    *os << "1: ldaxxr w11, [x9]\n";
                    *os << "  cbnz w11, 1b\n";
                    *os << "  stxr w12, w10, [x9]\n";
                    *os << "  cbnz w12, 1b\n";
                } else {
                    *os << "  movq " << ptr << ", %rax\n";
                    *os << "1: mov $1, %ecx\n";
                    *os << "  xchg %ecx, (%rax)\n";
                    *os << "  test %ecx, %ecx\n";
                    *os << "  jnz 1b\n";
                }
            } else if (spec.id == CapabilityId::SYNC_MUTEX_UNLOCK) {
                if (targetArch == Arch::RISCV64) {
                    *os << "  ld a0, " << ptr << "\n";
                    *os << "  amoswap.w.rl x0, x0, (a0)\n";
                } else if (targetArch == Arch::AArch64) {
                    *os << "  ldr x9, " << ptr << "\n";
                    *os << "  stlr wzr, [x9]\n";
                } else {
                    *os << "  movq " << ptr << ", %rax\n";
                    *os << "  movl $0, (%rax)\n";
                }
            } else if (spec.id == CapabilityId::SYNC_ATOMIC_ADD) {
                if (targetArch == Arch::RISCV64) {
                    *os << "  ld a0, " << ptr << "\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "a1";
                    *os << "  ld a1, " << val << "\n";
                    *os << "  amoadd.w a0, a1, (a0)\n";
                } else if (targetArch == Arch::AArch64) {
                    *os << "  ldr x9, " << ptr << "\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "x10";
                    *os << "  ldr x10, " << val << "\n";
                    *os << "1: ldaxr w11, [x9]\n  add w12, w11, w10\n  stlxr w13, w12, [x9]\n  cbnz w13, 1b\n";
                } else {
                    *os << "  movq " << ptr << ", %rax\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "%ecx";
                    *os << "  movl " << val << ", %ecx\n";
                    *os << "  lock addl %ecx, (%rax)\n";
                }
            }
        }
    }
}

void BareMetalOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Hardware Timer Counter Read\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  rdtime a0\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            }
        } else if (targetArch == Arch::AArch64) {
            *os << "  mrs x0, cntvct_el0\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            }
        } else {
            *os << "  rdtsc\n";
            *os << "  shl $32, %rdx\n";
            *os << "  or %rdx, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

void BareMetalOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Cycle Entropy PRNG\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  rdcycle a0\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  mrs x0, cntvct_el0\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  rdtsc\n  shl $32, %rdx\n  or %rdx, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void BareMetalOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Debug Trap / Log (" << spec.name << ")\n";
        if (spec.id == CapabilityId::DEBUG_BREAK) {
            if (targetArch == Arch::RISCV64) {
                *os << "  ebreak\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  brk #0\n";
            } else {
                *os << "  int3\n";
            }
            return;
        }

        if (spec.id == CapabilityId::DEBUG_LOG) {
            emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch);
        }
    }
}

void BareMetalOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal System Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::SYSTEM_REBOOT || spec.id == CapabilityId::SYSTEM_SHUTDOWN) {
            if (targetArch == Arch::RISCV64) {
                *os << "  li t0, 0x100000 # QEMU virt test syscon\n  li t1, 0x5555\n  sw t1, 0(t0)\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mov x9, #0x09000000\n  mov w10, #0x5555\n  str w10, [x9]\n";
            } else {
                *os << "  mov $0x604, %dx # QEMU ACPI poweroff\n  mov $0x2000, %ax\n  outw %ax, %dx\n";
            }
            return;
        }

        if (targetArch == Arch::RISCV64) {
            *os << "  csrr a0, mhartid\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            }
        } else if (targetArch == Arch::AArch64) {
            *os << "  mrs x0, mpidr_el1\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            }
        } else {
            *os << "  mov $1, %eax\n";
            *os << "  cpuid\n";
            *os << "  shr $24, %ebx\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                *os << "  movzbq %bl, %rax\n";
                *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

}
