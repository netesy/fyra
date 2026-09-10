#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/TargetRelocationEvaluator.h"
#include <map>
#include <unordered_set>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace target {
namespace artifact {
namespace linker {

namespace {

uint64_t alignUp(uint64_t offset, uint64_t align) {
    if (align <= 1) return offset;
    return (offset + align - 1) & ~(align - 1);
}

} // namespace

bool InternalLinker::link(const std::vector<target::artifact::object::ObjectArtifact>& artifacts,
                           LinkedImage& outImage) {
    lastError_.clear();
    outImage = LinkedImage{};

    if (artifacts.empty()) {
        lastError_ = "No input object artifacts provided to linker";
        return false;
    }

    outImage.arch = artifacts[0].arch;
    outImage.os = artifacts[0].os;

    // Check target compatibility across all artifacts
    for (size_t i = 1; i < artifacts.size(); ++i) {
        if (artifacts[i].arch != outImage.arch || artifacts[i].os != outImage.os) {
            lastError_ = "Incompatible object artifact target architectures/OS in link input";
            return false;
        }
    }

    // Base addresses
    uint64_t baseVma = (outImage.os == target::OS::Windows) ? 0x140000000ULL : 0x400000ULL;
    uint64_t pageAlign = 0x1000;

    // Merged sections tracking
    struct SectionPlacement {
        size_t artifactIndex;
        std::string sectionName;
        uint64_t outputOffset;
        uint64_t size;
    };
    std::vector<SectionPlacement> placements;

    std::vector<std::string> ordered_sections = {
        ".text", ".rodata", ".data", ".bss",
        ".debug_info", ".debug_abbrev", ".debug_line", ".debug_str", ".debug_frame", ".eh_frame"
    };

    uint64_t currentVma = baseVma + pageAlign;

    for (const auto& secName : ordered_sections) {
        LinkedSection lsec;
        lsec.name = secName;
        lsec.alignment = 16;
        lsec.virtualAddress = currentVma;

        if (secName == ".text") { lsec.isExecutable = true; lsec.isWritable = false; }
        else if (secName == ".rodata") { lsec.isExecutable = false; lsec.isWritable = false; }
        else if (secName == ".data" || secName == ".bss") { lsec.isExecutable = false; lsec.isWritable = true; }
        else { lsec.isExecutable = false; lsec.isWritable = false; lsec.isReadable = false; }

        uint64_t currentOffset = 0;

        for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
            const auto* sec = artifacts[aIdx].findSection(secName);
            if (!sec || (sec->data.empty() && sec->virtualSize == 0)) continue;

            uint64_t align = sec->alignment ? sec->alignment : 16;
            currentOffset = alignUp(currentOffset, align);

            SectionPlacement p;
            p.artifactIndex = aIdx;
            p.sectionName = secName;
            p.outputOffset = currentOffset;
            p.size = (secName == ".bss") ? sec->virtualSize : sec->data.size();
            placements.push_back(p);

            if (secName != ".bss") {
                if (currentOffset > lsec.data.size()) {
                    lsec.data.resize(currentOffset, 0);
                }
                lsec.data.insert(lsec.data.end(), sec->data.begin(), sec->data.end());
            }

            currentOffset += p.size;
        }

        if (currentOffset > 0) {
            lsec.virtualSize = currentOffset;
            outImage.sections[secName] = lsec;
            if (secName == ".text" || secName == ".rodata" || secName == ".data" || secName == ".bss") {
                currentVma = alignUp(currentVma + currentOffset, pageAlign);
            }
        }
    }

    // Global symbol resolution
    std::map<std::string, std::pair<size_t, target::artifact::object::ObjectSymbol>> resolvedGlobals;

    for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
        for (const auto& sym : artifacts[aIdx].symbols) {
            if (!sym.isDefined) continue;

            if (sym.binding != target::artifact::object::SymbolBinding::Local) {
                if (resolvedGlobals.count(sym.name)) {
                    lastError_ = "Duplicate global symbol definition: '" + sym.name + "'";
                    return false;
                }
                resolvedGlobals[sym.name] = {aIdx, sym};
            }
        }
    }

    // Rebase symbol addresses
    for (const auto& [symName, pair] : resolvedGlobals) {
        size_t aIdx = pair.first;
        const auto& sym = pair.second;

        uint64_t outputOffset = 0;
        for (const auto& p : placements) {
            if (p.artifactIndex == aIdx && p.sectionName == sym.sectionName) {
                outputOffset = p.outputOffset;
                break;
            }
        }

        const auto* lsec = outImage.findSection(sym.sectionName);
        uint64_t secVma = lsec ? lsec->virtualAddress : baseVma;

        LinkedSymbol lsym;
        lsym.name = symName;
        lsym.virtualAddress = secVma + outputOffset + sym.value;
        lsym.size = sym.size;
        lsym.isFunction = (sym.type == target::artifact::object::SymbolType::Function);
        lsym.isGlobal = true;
        lsym.sectionName = sym.sectionName;

        outImage.symbols[symName] = lsym;
    }

    // Add Linux _start entry stub if main is present and _start is missing
    if (!outImage.symbols.count("_start") && (outImage.symbols.count("main") || outImage.symbols.count("$main"))) {
        std::string mainName = outImage.symbols.count("main") ? "main" : "$main";
        uint64_t mainAddr = outImage.symbols[mainName].virtualAddress;

        auto* textSec = outImage.findSection(".text");
        if (textSec) {
            uint64_t startOffset = textSec->data.size();
            uint64_t startVma = textSec->virtualAddress + startOffset;

            // x86-64 _start stub
            // 31 ed             xor %ebp, %ebp
            // 5f                pop %rdi
            // 48 89 e6          mov %rsp, %rsi
            // e8 [rel32]        call main
            // 48 89 c7          mov %rax, %rdi
            // b8 3c 00 00 00    mov $60, %eax
            // 0f 05             syscall
            std::vector<uint8_t> startBytes = {
                0x31, 0xED,
                0x5F,
                0x48, 0x89, 0xE6,
                0xE8, 0x00, 0x00, 0x00, 0x00,
                0x48, 0x89, 0xC7,
                0xB8, 0x3C, 0x00, 0x00, 0x00,
                0x0F, 0x05
            };

            int64_t callRel = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 11);
            int32_t callRel32 = static_cast<int32_t>(callRel);
            std::memcpy(startBytes.data() + 7, &callRel32, 4);

            textSec->data.insert(textSec->data.end(), startBytes.begin(), startBytes.end());
            textSec->virtualSize = textSec->data.size();

            LinkedSymbol startSym;
            startSym.name = "_start";
            startSym.virtualAddress = startVma;
            startSym.size = startBytes.size();
            startSym.isFunction = true;
            startSym.isGlobal = true;
            startSym.sectionName = ".text";

            outImage.symbols["_start"] = startSym;
        }
    }

    // Set entry point
    if (outImage.symbols.count("_start")) {
        outImage.entryAddress = outImage.symbols["_start"].virtualAddress;
        outImage.entrySymbolName = "_start";
    } else if (outImage.symbols.count("main")) {
        outImage.entryAddress = outImage.symbols["main"].virtualAddress;
        outImage.entrySymbolName = "main";
    }

    // Evaluate relocations
    for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
        for (const auto& reloc : artifacts[aIdx].relocations) {
            uint64_t outputOffset = 0;
            for (const auto& p : placements) {
                if (p.artifactIndex == aIdx && p.sectionName == reloc.sectionName) {
                    outputOffset = p.outputOffset;
                    break;
                }
            }

            auto* lsec = outImage.findSection(reloc.sectionName);
            if (!lsec) {
                continue;
            }

            uint64_t targetSymAddr = 0;
            if (outImage.symbols.count(reloc.symbolName)) {
                targetSymAddr = outImage.symbols[reloc.symbolName].virtualAddress;
            } else if (const auto* sec = outImage.findSection(reloc.symbolName)) {
                targetSymAddr = sec->virtualAddress;
            } else {
                lastError_ = "Unresolved undefined symbol reference: '" + reloc.symbolName + "'";
                return false;
            }

            uint64_t placeAddress = lsec->virtualAddress + outputOffset + reloc.offset;
            uint64_t sectionDataOffset = outputOffset + reloc.offset;

            RelocationKind kind = TargetRelocationEvaluator::normalizeType(reloc.type);
            std::string evalError;
            if (!TargetRelocationEvaluator::evaluate(kind, targetSymAddr, placeAddress, reloc.addend, lsec->data, sectionDataOffset, evalError)) {
                lastError_ = "Relocation evaluation failed for '" + reloc.symbolName + "': " + evalError;
                return false;
            }
        }
    }

    return true;
}

} // namespace linker
} // namespace artifact
} // namespace target
