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
    }
}

void BareMetalOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << ".text\n.globl _start\n_start:\n";
        if (targetArch == Arch::RISCV64) {
            *os << "  la sp, __stack_top\n";
            *os << "  call main\n";
            *os << "1: wfi\n  j 1b\n";
        } else if (targetArch == Arch::AArch64) {
            *os << "  adrp x9, __stack_top\n";
            *os << "  mov sp, x9\n";
            *os << "  bl main\n";
            *os << "1: wfe\n  b 1b\n";
        } else {
            *os << "  movabs $__stack_top, %rsp\n";
            *os << "  call main\n";
            *os << "1: hlt\n  jmp 1b\n";
        }
    }
}

void BareMetalOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        Arch targetArch = arch.getArch();
        *os << "  # BareMetal MMIO UART Write\n";
        if (!i.getOperands().empty() && i.getOperands()[0]) {
            std::string src = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (targetArch == Arch::RISCV64) {
                *os << "  li t0, 0x10000000 # QEMU virt UART0 MMIO\n";
                *os << "  li t1, " << src << "\n";
                *os << "  sb t1, 0(t0)\n";
            } else if (targetArch == Arch::AArch64) {
                *os << "  mov x9, #0x09000000 # QEMU virt PL011 UART MMIO\n";
                *os << "  ldr w10, " << src << "\n";
                *os << "  strb w10, [x9]\n";
            } else {
                *os << "  mov $0x3F8, %dx # COM1 UART Port\n";
                *os << "  outb %al, %dx\n";
            }
        }
    }
}

}
