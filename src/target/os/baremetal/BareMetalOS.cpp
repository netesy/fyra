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
        *os << "  # BareMetal MMIO UART Capability\n";
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

void BareMetalOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Freestanding Halt\n";
        if (targetArch == Arch::RISCV64) {
            *os << "1: wfi\n  j 1b\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "1: wfe\n  b 1b\n";
        } else {
            *os << "1: hlt\n  jmp 1b\n";
        }
    }
}

void BareMetalOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Bump Allocation\n";
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

void BareMetalOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal Hardware CPU Core Query\n";
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
