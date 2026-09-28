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
        *os << "  __baremetal_errno:\n";
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

// 1. IO Capability: MMIO UART Stream read/write/open/close/stat/flush
void BareMetalOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal MMIO UART Capability (" << spec.name << ")\n";

        if (spec.id == CapabilityId::IO_OPEN || spec.id == CapabilityId::IO_CLOSE || spec.id == CapabilityId::IO_FLUSH || spec.id == CapabilityId::IO_STAT) {
            if (targetArch == Arch::RISCV64) {
                *os << "  li a0, 0 # UART Stream Handle 0\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mov x0, #0\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  mov $0, %rax\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
            return;
        }

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

// 2. FS Capability: ROM / RAMFS Storage file table lookup
void BareMetalOS::emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal RAMFS Storage Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __ramfs_root\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x0, __ramfs_root\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movabs $__ramfs_root, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 3. Memory Capability: Bump allocation, free, RAM usage, MPU/PMP memory mapping
void BareMetalOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Memory Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::MEMORY_FREE) {
            *os << "  # memory.free: freestanding no-op\n";
            return;
        }

        if (spec.id == CapabilityId::MEMORY_MAP || spec.id == CapabilityId::MEMORY_PROTECT) {
            if (targetArch == Arch::RISCV64) {
                *os << "  csrw pmpcfg0, zero # BareMetal PMP Memory Protection\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  msr mair_el1, xzr # BareMetal MMU Translation\n";
            } else {
                *os << "  mov %cr3, %rax # BareMetal CR3 Page Table Register\n";
            }
            if (i.getType() && !i.getType()->isVoidTy()) {
                if (targetArch == Arch::RISCV64) *os << "  sd zero, " << cg.getValueAsOperand(&i) << "\n";
                else if (targetArch == Arch::AArch64) *os << "  str xzr, " << cg.getValueAsOperand(&i) << "\n";
                else *os << "  movq $0, " << cg.getValueAsOperand(&i) << "\n";
            }
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

// 4. Process Capability: Hart halt loops, delay spin, CPU core ID
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

        if (spec.id == CapabilityId::PROCESS_ARGS) {
            if (targetArch == Arch::RISCV64) {
                *os << "  la a0, __boot_cmdline\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  adrp x0, __boot_cmdline\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  movabs $__boot_cmdline, %rax\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
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

// 5. Thread Capability: Hart context switch & stack frame allocation
void BareMetalOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Thread / Hart Context Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::THREAD_GETID) {
            emitProcessCapability(cg, i, CapabilitySpec{CapabilityId::PROCESS_GETPID, "process.getpid", CapabilityDomain::PROCESS, 0, 0, true, false}, arch);
            return;
        }

        if (spec.id == CapabilityId::THREAD_YIELD) {
            if (targetArch == Arch::RISCV64) *os << "  wfi\n";
            else if (targetArch == Arch::AArch64) *os << "  yield\n";
            else *os << "  pause\n";
            return;
        }

        if (spec.id == CapabilityId::THREAD_SPAWN) {
            emitMemoryCapability(cg, i, CapabilitySpec{CapabilityId::MEMORY_ALLOC, "memory.alloc", CapabilityDomain::MEMORY, 1, 1, true, true}, arch);
            return;
        }
    }
}

// 6. Sync Capability: Spinlocks, atomic add/sub/cas
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
            } else if (spec.id == CapabilityId::SYNC_ATOMIC_ADD || spec.id == CapabilityId::SYNC_ATOMIC_SUB) {
                bool isSub = (spec.id == CapabilityId::SYNC_ATOMIC_SUB);
                if (targetArch == Arch::RISCV64) {
                    *os << "  ld a0, " << ptr << "\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "a1";
                    *os << "  ld a1, " << val << "\n";
                    if (isSub) *os << "  sub a1, zero, a1\n";
                    *os << "  amoadd.w a0, a1, (a0)\n";
                } else if (targetArch == Arch::AArch64) {
                    *os << "  ldr x9, " << ptr << "\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "x10";
                    *os << "  ldr x10, " << val << "\n";
                    if (isSub) *os << "  1: ldaxr w11, [x9]\n  sub w12, w11, w10\n  stlxr w13, w12, [x9]\n  cbnz w13, 1b\n";
                    else *os << "  1: ldaxr w11, [x9]\n  add w12, w11, w10\n  stlxr w13, w12, [x9]\n  cbnz w13, 1b\n";
                } else {
                    *os << "  movq " << ptr << ", %rax\n";
                    std::string val = (i.getOperands().size() > 1 && i.getOperands()[1]) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "%ecx";
                    *os << "  movl " << val << ", %ecx\n";
                    if (isSub) *os << "  lock subl %ecx, (%rax)\n";
                    else *os << "  lock addl %ecx, (%rax)\n";
                }
            } else if (spec.id == CapabilityId::SYNC_ATOMIC_CAS) {
                if (targetArch == Arch::RISCV64) {
                    *os << "  ld a0, " << ptr << "\n";
                    *os << "1: lr.w a1, (a0)\n  sc.w a2, a1, (a0)\n  bnez a2, 1b\n";
                } else if (targetArch == Arch::AArch64) {
                    *os << "  ldr x9, " << ptr << "\n";
                    *os << "1: ldaxr w11, [x9]\n  stlxr w13, w11, [x9]\n  cbnz w13, 1b\n";
                } else {
                    *os << "  movq " << ptr << ", %rax\n";
                    *os << "  lock cmpxchgl %ecx, (%rax)\n";
                }
            }
        }
    }
}

// 7. Time Capability: Hardware timer counter reads and spin delays
void BareMetalOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Hardware Timer Counter Read (" << spec.name << ")\n";
        if (spec.id == CapabilityId::TIME_SLEEP) {
            emitProcessCapability(cg, i, CapabilitySpec{CapabilityId::PROCESS_SLEEP, "process.sleep", CapabilityDomain::PROCESS, 1, 1, false, false}, arch);
            return;
        }

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

// 8. Event Capability: MMIO Event ring buffer polling & creation
void BareMetalOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal MMIO Event Ring Buffer (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  li a0, 0x10001000 # MMIO Event Register\n  ld a0, 0(a0)\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  mov x9, #0x09001000 # MMIO Event Register\n  ldr x0, [x9]\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  mov $0x3F0, %dx # Port Event Register\n  inb %dx, %al\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movzbq %al, %rax\n  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 9. Net Capability: VirtIO-Net / Ethernet MAC packet ring buffer send/recv
void BareMetalOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal VirtIO-Net Ethernet Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  li a0, 0x10002000 # VirtIO-Net MMIO Base\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  mov x9, #0x09002000 # VirtIO-Net MMIO Base\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  mov $0x300, %dx # NE2000 / VirtIO-Net IO Port\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rdx, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 10. IPC Capability: Inter-hart shared memory message queues
void BareMetalOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Inter-Hart Shared Memory IPC (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __ipc_mailbox\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x0, __ipc_mailbox\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movabs $__ipc_mailbox, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 11. Env Capability: Boot command line / Flattened Device Tree (FDT) string parsing
void BareMetalOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal DeviceTree / Boot Env Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __dtb_header\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x0, __dtb_header\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movabs $__dtb_header, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 12. System Capability: CPU core ID query, QEMU syscon reboot/shutdown
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

// 13. Signal Capability: Hardware exception & trap vector handler registration (mtvec / vbar_el1 / IDT)
void BareMetalOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Trap Vector Handler Registration (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la t0, __trap_vector\n  csrw mtvec, t0\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x9, __trap_vector\n  msr vbar_el1, x9\n";
        } else {
            *os << "  lidt __idt_descriptor(%rip)\n";
        }
    }
}

// 14. Random Capability: Hardware cycle/timer entropy PRNG
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

// 15. Error Capability: Global error code tracking
void BareMetalOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Error Status Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __baremetal_errno\n  ld a0, 0(a0)\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x9, __baremetal_errno\n  ldr x0, [x9, :lo12:__baremetal_errno]\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movq __baremetal_errno(%rip), %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 16. Debug Capability: UART debug string logging & hardware breakpoints
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

        if (spec.id == CapabilityId::DEBUG_TRACE) {
            if (targetArch == Arch::RISCV64) *os << "  mv a0, s0 # Frame Pointer Stack Trace\n";
            else if (targetArch == Arch::AArch64) *os << "  mov x0, x29 # Frame Pointer Stack Trace\n";
            else *os << "  mov %rbp, %rax # Frame Pointer Stack Trace\n";
            if (i.getType() && !i.getType()->isVoidTy()) {
                if (targetArch == Arch::RISCV64) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                else if (targetArch == Arch::AArch64) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
                else *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
            return;
        }

        if (spec.id == CapabilityId::DEBUG_LOG) {
            emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch);
        }
    }
}

// 17. Module Capability: In-memory baremetal symbol table lookup
void BareMetalOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Module / Symbol Table Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __symtab_start\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x0, __symtab_start\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movabs $__symtab_start, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 18. TTY Capability: UART VT100 console mode & screen size queries
void BareMetalOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal VT100 Serial Console Capability (" << spec.name << ")\n";
        if (spec.id == CapabilityId::TTY_ISATTY) {
            if (targetArch == Arch::RISCV64) {
                *os << "  li a0, 1 # UART is VT100 TTY\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mov x0, #1\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  mov $1, %rax\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
        } else if (spec.id == CapabilityId::TTY_GETSIZE) {
            if (targetArch == Arch::RISCV64) {
                *os << "  li a0, 0x00180050 # 80x24 VT100 Console Size\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mov x0, #0x00180050\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
            } else {
                *os << "  mov $0x00180050, %rax\n";
                if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

// 19. Security Capability: PMP / MPU physical memory protection configuration
void BareMetalOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal PMP / MPU Physical Memory Protection (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  csrw pmpaddr0, zero\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  msr prbar_el1, xzr\n";
        } else {
            *os << "  mov %cr0, %rax\n";
        }
        if (i.getType() && !i.getType()->isVoidTy()) {
            if (targetArch == Arch::RISCV64) *os << "  sd zero, " << cg.getValueAsOperand(&i) << "\n";
            else if (targetArch == Arch::AArch64) *os << "  str xzr, " << cg.getValueAsOperand(&i) << "\n";
            else *os << "  movq $0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

// 20. GPU Capability: VirtIO-GPU / Linear Framebuffer (LFB) memory mapping & blit pixel operations
void BareMetalOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal VirtIO-GPU Linear Framebuffer Capability (" << spec.name << ")\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la a0, __framebuffer_start\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x0, __framebuffer_start\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  movabs $__framebuffer_start, %rax\n";
            if (i.getType() && !i.getType()->isVoidTy()) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

}
