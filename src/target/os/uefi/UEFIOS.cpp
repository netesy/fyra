#include "target/os/uefi/UEFIOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include <iostream>

namespace target {

bool UEFIOS::supportsCapability(const CapabilitySpec& spec) const {
    // UEFI provides console IO and memory services
    switch (spec.id) {
        case CapabilityId::IO_WRITE:
        case CapabilityId::MEMORY_ALLOC:
        case CapabilityId::SYSTEM_INFO:
            return true;
        default:
            return false;
    }
}

void UEFIOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IO_WRITE) { // Console output
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI Console Output\n";
            *os << "  # Call ConOutput protocol\n";
            // For now, emit a stub - actual implementation would call EFI protocol
            *os << "  li a0, 0\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    } else {
        // Unsupported - emit error code
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI unsupported IO capability\n";
            *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

void UEFIOS::emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported FS capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::MEMORY_ALLOC) { // memory.alloc
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI Pool Allocation\n";
            *os << "  # Call AllocatePool\n";
            // Stub implementation
            *os << "  li a0, 0\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI unsupported memory capability\n";
            *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

void UEFIOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported process capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported thread capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported sync capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported time capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported event capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported net capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported IPC capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported env capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::SYSTEM_INFO) { // system.info
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI System Information\n";
            *os << "  # Access EFI System Table\n";
            *os << "  li a0, 0\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << "  # UEFI unsupported system capability\n";
            *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
            if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
                std::string reg = arch.getReturnRegister(i.getType());
                *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
            }
        }
    }
}

void UEFIOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported signal capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported random capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported error capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported debug capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported module capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported TTY capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported security capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  # UEFI unsupported GPU capability\n";
        *os << "  li a0, 1  # EFI_UNSUPPORTED\n";
        if (i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string reg = arch.getReturnRegister(i.getType());
            *os << "  " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void UEFIOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "# UEFI Application\n";
        *os << "# Entry: efi_main(ImageHandle, SystemTable*) -> EFI_STATUS\n";
    }
}

void UEFIOS::emitStartFunction(CodeGen& cg, const class ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .text\n";
        *os << ".globl efi_main\n";
        *os << "efi_main:\n";
        *os << "  # Entry point: efi_main(ImageHandle, SystemTable*) -> EFI_STATUS\n";
        *os << "  # a0 = ImageHandle, a1 = SystemTable* (RISC-V)\n";
        *os << "  # rcx = ImageHandle, rdx = SystemTable* (x86-64)\n";
        *os << "  # x0 = ImageHandle, x1 = SystemTable* (AArch64)\n";
        *os << "  # Stack frame\n";
        *os << "  addi sp, sp, -32\n";
        *os << "  sd ra, 24(sp)\n";
        *os << "  sd s0, 16(sp)\n";
        *os << "  # Save ImageHandle and SystemTable for later use\n";
        *os << "  sd a0, 8(sp)\n";
        *os << "  sd a1, 0(sp)\n";
        *os << "  # Call main\n";
        *os << "  call main\n";
        *os << "  # Restore\n";
        *os << "  ld a0, 8(sp)\n";
        *os << "  ld a1, 0(sp)\n";
        *os << "  ld ra, 24(sp)\n";
        *os << "  ld s0, 16(sp)\n";
        *os << "  addi sp, sp, 32\n";
        *os << "  # Return EFI_SUCCESS (0)\n";
        *os << "  li a0, 0\n";
        *os << "  ret\n";
    }
}

}
