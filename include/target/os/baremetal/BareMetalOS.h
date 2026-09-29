#pragma once
#include "target/core/OperatingSystemInfo.h"

#include <string>
#include <cstdint>
#include <optional>
#include <utility>

namespace target {

struct BareMetalBoard {
    std::string name;
    uint64_t uartAddress = 0;       // MMIO address or Port I/O
    bool isPortIO = false;          // True for x86 port I/O
    uint64_t eventAddress = 0;      // MMIO event register
    uint64_t netAddress = 0;        // MMIO net base

    static BareMetalBoard qemuVirtRiscV() {
        BareMetalBoard b;
        b.name = "QEMU virt";
        b.uartAddress = 0x10000000;
        b.eventAddress = 0x10001000;
        b.netAddress = 0x10002000;
        b.isPortIO = false;
        return b;
    }

    static BareMetalBoard qemuVirtAArch64() {
        BareMetalBoard b;
        b.name = "QEMU virt";
        b.uartAddress = 0x09000000;
        b.eventAddress = 0x09001000;
        b.netAddress = 0x09002000;
        b.isPortIO = false;
        return b;
    }

    static BareMetalBoard pcCom1() {
        BareMetalBoard b;
        b.name = "PC COM1";
        b.uartAddress = 0x3F8;
        b.eventAddress = 0x3F0;
        b.netAddress = 0x300;
        b.isPortIO = true;
        return b;
    }

    static BareMetalBoard raspberryPi3() {
        BareMetalBoard b;
        b.name = "Raspberry Pi 3";
        b.uartAddress = 0x3F201000;
        b.eventAddress = 0x3F00B000;
        b.netAddress = 0x3F980000;
        b.isPortIO = false;
        return b;
    }

    static BareMetalBoard sifiveHiFive() {
        BareMetalBoard b;
        b.name = "SiFive HiFive";
        b.uartAddress = 0x10013000;
        b.eventAddress = 0x10014000;
        b.netAddress = 0x10090000;
        b.isPortIO = false;
        return b;
    }

    static BareMetalBoard custom(const std::string& name, uint64_t uartAddr, uint64_t eventAddr = 0, uint64_t netAddr = 0, bool portIO = false) {
        BareMetalBoard b;
        b.name = name;
        b.uartAddress = uartAddr;
        b.eventAddress = eventAddr;
        b.netAddress = netAddr;
        b.isPortIO = portIO;
        return b;
    }
};

class BareMetalOS : public OperatingSystemInfo {
private:
    std::optional<BareMetalBoard> customBoard_;

public:
    // -----------------------------------------------------------------------
    // Board abstraction layer
    //
    // By default, BareMetalOS uses the QEMU virt-board peripheral map:
    //   - RISC-V 64 : UART at 0x10000000  (QEMU virt, SiFive-compatible)
    //   - AArch64   : UART at 0x09000000  (QEMU virt, PL011)
    //   - x86-64    : UART via Port I/O 0x3F8 (PC COM1)
    //
    // For real hardware (microcontrollers, custom SoCs, RPi, HiFive, etc.)
    // you MUST supply an explicit BareMetalBoard before invoking codegen:
    //
    //   auto os = BareMetalOS(BareMetalBoard::raspberryPi3());
    //   // or:
    //   BareMetalOS os;
    //   os.setBoard(BareMetalBoard::custom("MyMCU", 0x40013800));
    //
    // Failing to do so will generate MMIO accesses targeting QEMU addresses
    // that will silently produce no output on real hardware.
    // -----------------------------------------------------------------------

    BareMetalOS() = default;
    explicit BareMetalOS(BareMetalBoard board) : customBoard_(std::move(board)) {}

    // Convenience factory: creates a BareMetalOS with a board already set.
    static BareMetalOS withBoard(BareMetalBoard board) {
        return BareMetalOS(std::move(board));
    }

    std::string getName() const override { return "baremetal"; }
    bool supportsCapability(const CapabilitySpec& spec) const override { return true; }

    void setBoard(BareMetalBoard board) { customBoard_ = std::move(board); }
    BareMetalBoard getBoard(Arch arch) const {
        if (customBoard_) return *customBoard_;
        if (arch == Arch::RISCV64) return BareMetalBoard::qemuVirtRiscV();
        if (arch == Arch::AArch64) return BareMetalBoard::qemuVirtAArch64();
        return BareMetalBoard::pcCom1();
    }
    // Returns true if no explicit board has been configured (QEMU defaults in use).
    bool isDefaultBoard() const { return !customBoard_.has_value(); }

    void emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;

    void emitHeader(CodeGen& cg) override;
    void emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) override;
};

}
