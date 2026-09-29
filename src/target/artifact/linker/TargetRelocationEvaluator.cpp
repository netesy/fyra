#include "target/artifact/linker/TargetRelocationEvaluator.h"
#include "target/core/TargetInfo.h"
#include <cstring>
#include <limits>

namespace target {
namespace artifact {
namespace linker {

RelocationKind TargetRelocationEvaluator::normalizeType(const std::string& typeStr) {
    if (typeStr == "R_X86_64_PC32" || typeStr == "R_X86_64_PLT32" || typeStr == "IMAGE_REL_AMD64_REL32" ||
        typeStr == "X86_64_RELOC_BRANCH" || typeStr == "X86_64_RELOC_SIGNED" ||
        typeStr == "R_AARCH64_ADR_PREL_PG_HI21" || typeStr == "IMAGE_REL_ARM64_PAGEBASE_REL21" ||
        typeStr == "R_AARCH64_ADD_ABS_LO12_NC" || typeStr == "IMAGE_REL_ARM64_PAGEOFFSET_12A" ||
        typeStr == "R_TYPE_2" || typeStr == "R_TYPE_4" || typeStr == "R_TYPE_275" || typeStr == "R_TYPE_277") {
        return RelocationKind::PcRelative;
    }
    if (typeStr == "R_X86_64_64" || typeStr == "IMAGE_REL_AMD64_ADDR64" || typeStr == "R_AARCH64_ABS64" ||
        typeStr == "IMAGE_REL_ARM64_ADDR64" || typeStr == "R_RISCV_64" || typeStr == "IMAGE_REL_RISCV_ADDR64" ||
        typeStr == "R_TYPE_1" || typeStr == "R_TYPE_257") {
        return RelocationKind::Absolute;
    }
    if (typeStr == "R_AARCH64_CALL26" || typeStr == "R_AARCH64_JUMP26" || typeStr == "IMAGE_REL_ARM64_BRANCH26" ||
        typeStr == "R_RISCV_JAL" || typeStr == "R_RISCV_CALL" || typeStr == "R_RISCV_CALL_PLT" || typeStr == "IMAGE_REL_RISCV_CALL" ||
        typeStr == "R_TYPE_283" || typeStr == "R_TYPE_282" || typeStr == "R_TYPE_17" || typeStr == "R_TYPE_18" || typeStr == "R_TYPE_19") {
        return RelocationKind::CallRelative;
    }
    if (typeStr == "R_AARCH64_CONDBR19" || typeStr == "R_RISCV_BRANCH" || typeStr == "IMAGE_REL_RISCV_BRANCH" ||
        typeStr == "R_TYPE_280" || typeStr == "R_TYPE_16") {
        return RelocationKind::BranchRelative;
    }
    return RelocationKind::Unknown;
}

bool TargetRelocationEvaluator::evaluate(RelocationKind kind,
                                          uint64_t symbolAddress,
                                          uint64_t placeAddress,
                                          int64_t addend,
                                          std::vector<uint8_t>& sectionData,
                                          uint64_t offset,
                                          std::string& errorOut,
                                          target::Arch arch) {
    if (kind == RelocationKind::PcRelative) {
        if (offset + 4 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for 32-bit PC-relative relocation";
            return false;
        }
        int64_t delta = static_cast<int64_t>(symbolAddress) + addend - static_cast<int64_t>(placeAddress);
        if (arch == target::Arch::AArch64) {
            uint32_t insn;
            std::memcpy(&insn, sectionData.data() + offset, 4);
            // Check if instruction is ADRP (bits [31], [28:24] = 1 10000)
            if ((insn & 0x9F000000) == 0x90000000) {
                int64_t pageDelta = (static_cast<int64_t>(symbolAddress & ~0xFFFULL) + addend - static_cast<int64_t>(placeAddress & ~0xFFFULL)) >> 12;
                uint32_t immlo = (pageDelta & 0x3) << 29;
                uint32_t immhi = ((pageDelta >> 2) & 0x7FFFF) << 5;
                insn = (insn & 0x9F00001F) | immlo | immhi;
                std::memcpy(sectionData.data() + offset, &insn, 4);
                return true;
            }
            // Check if instruction is ADD immediate (bits [31:22] = 0b1001000100 for 64-bit add)
            if ((insn & 0xFF800000) == 0x91000000 || (insn & 0xFF800000) == 0x11000000) {
                uint32_t imm12 = (static_cast<uint32_t>(symbolAddress + addend) & 0xFFF) << 10;
                insn = (insn & 0xFFC003FF) | imm12;
                std::memcpy(sectionData.data() + offset, &insn, 4);
                return true;
            }
        }
        if (delta < -2147483648LL || delta > 2147483647LL) {
            errorOut = "Relocation overflow for 32-bit PC-relative relocation (delta = " + std::to_string(delta) + ")";
            return false;
        }
        int32_t val32 = static_cast<int32_t>(delta);
        std::memcpy(sectionData.data() + offset, &val32, 4);
        return true;
    }

    if (kind == RelocationKind::Absolute) {
        if (offset + 8 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for 64-bit absolute relocation";
            return false;
        }
        uint64_t val64 = symbolAddress + addend;
        std::memcpy(sectionData.data() + offset, &val64, 8);
        return true;
    }

    if (kind == RelocationKind::CallRelative) {
        if (offset + 4 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for call-relative relocation";
            return false;
        }
        int64_t delta = static_cast<int64_t>(symbolAddress) + addend - static_cast<int64_t>(placeAddress);
        if (arch == target::Arch::AArch64) {
            int64_t wordDelta = delta / 4;
            uint32_t imm26 = static_cast<uint32_t>(wordDelta) & 0x03FFFFFF;
            uint32_t insn;
            std::memcpy(&insn, sectionData.data() + offset, 4);
            if ((insn & 0xFC000000) == 0) insn = 0x94000000; // default BL
            insn = (insn & 0xFC000000) | imm26;
            std::memcpy(sectionData.data() + offset, &insn, 4);
            return true;
        }
        if (arch == target::Arch::RISCV64) {
            uint32_t imm = static_cast<uint32_t>(delta);
            uint32_t j_imm = ((imm & 0x100000) << 11) | ((imm & 0x7FE) << 20) | ((imm & 0x800) << 9) | (imm & 0xFF000);
            uint32_t insn;
            std::memcpy(&insn, sectionData.data() + offset, 4);
            if ((insn & 0x7F) != 0x6F) insn = 0x000000EF; // default jal ra, 0
            insn = (insn & 0x00000FFF) | j_imm;
            std::memcpy(sectionData.data() + offset, &insn, 4);
            return true;
        }
        int32_t val32 = static_cast<int32_t>(delta);
        std::memcpy(sectionData.data() + offset, &val32, 4);
        return true;
    }

    if (kind == RelocationKind::BranchRelative) {
        if (offset + 4 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for branch-relative relocation";
            return false;
        }
        int64_t delta = static_cast<int64_t>(symbolAddress) + addend - static_cast<int64_t>(placeAddress);
        if (arch == target::Arch::AArch64) {
            int64_t wordDelta = delta / 4;
            uint32_t imm19 = static_cast<uint32_t>(wordDelta) & 0x7FFFF;
            uint32_t insn;
            std::memcpy(&insn, sectionData.data() + offset, 4);
            insn = (insn & 0xFF00001F) | (imm19 << 5);
            std::memcpy(sectionData.data() + offset, &insn, 4);
            return true;
        }
        if (arch == target::Arch::RISCV64) {
            uint32_t imm = static_cast<uint32_t>(delta);
            uint32_t b_imm = ((imm & 0x1000) << 19) | ((imm & 0x7E0) << 20) | ((imm & 0x1E) << 7) | ((imm & 0x800) >> 4);
            uint32_t insn;
            std::memcpy(&insn, sectionData.data() + offset, 4);
            insn = (insn & 0x01FFF07F) | b_imm;
            std::memcpy(sectionData.data() + offset, &insn, 4);
            return true;
        }
        int32_t val32 = static_cast<int32_t>(delta);
        std::memcpy(sectionData.data() + offset, &val32, 4);
        return true;
    }

    errorOut = "Unsupported relocation kind";
    return false;
}

std::vector<uint8_t> TargetRelocationEvaluator::getImportThunkBytes(target::Arch arch, target::OS os) {
    if (arch == target::Arch::X64 && (os == target::OS::Windows || os == target::OS::Linux || os == target::OS::MacOS)) {
        // x64 indirect jump instruction: ff 25 [disp32] (displacement patched by PE/ELF/Mach-O writer)
        return {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    }
    if (arch == target::Arch::AArch64) {
        // AArch64 indirect branch:
        // adrp x16, 0       -> 0x90000010
        // ldr  x16, [x16, 0]-> 0xF9400010
        // br   x16          -> 0xD61F0200
        // nop               -> 0xD503201F
        return {
            0x10, 0x00, 0x00, 0x90,
            0x10, 0x00, 0x40, 0xF9,
            0x00, 0x02, 0x1F, 0xD6,
            0x1F, 0x20, 0x03, 0xD5
        };
    }
    if (arch == target::Arch::RISCV64) {
        // RISC-V indirect branch:
        // auipc t0, 0       -> 0x00000297
        // ld    t0, 0(t0)   -> 0x0002B283
        // jr    t0          -> 0x00028067
        // nop               -> 0x00000013
        return {
            0x97, 0x02, 0x00, 0x00,
            0x83, 0xB2, 0x02, 0x00,
            0x67, 0x80, 0x02, 0x00,
            0x13, 0x00, 0x00, 0x00
        };
    }
    return {};
}

} // namespace linker
} // namespace artifact
} // namespace target
