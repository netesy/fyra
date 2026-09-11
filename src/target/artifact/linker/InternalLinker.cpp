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

bool InternalLinker::extractLazyArchiveMembers(
    std::vector<target::artifact::object::ObjectArtifact>& inOutArtifacts,
    std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>>& archives) {

    bool extractedAny = false;
    bool progress = true;

    while (progress) {
        progress = false;

        std::unordered_set<std::string> definedGlobals;
        for (const auto& art : inOutArtifacts) {
            for (const auto& sym : art.symbols) {
                if (sym.isDefined && sym.binding != target::artifact::object::SymbolBinding::Local) {
                    definedGlobals.insert(sym.name);
                }
            }
        }

        std::unordered_set<std::string> unresolvedGlobals;
        for (const auto& art : inOutArtifacts) {
            for (const auto& reloc : art.relocations) {
                if (!definedGlobals.count(reloc.symbolName)) {
                    unresolvedGlobals.insert(reloc.symbolName);
                }
            }
            for (const auto& sym : art.symbols) {
                if (!sym.isDefined && !definedGlobals.count(sym.name)) {
                    unresolvedGlobals.insert(sym.name);
                }
            }
        }

        if (unresolvedGlobals.empty()) {
            break;
        }

        for (auto& archiveMembers : archives) {
            for (auto& member : archiveMembers) {
                if (member.extracted) continue;

                bool providesNeededSymbol = false;
                for (const auto& sym : member.artifact.symbols) {
                    if (sym.isDefined && sym.binding != target::artifact::object::SymbolBinding::Local && unresolvedGlobals.count(sym.name)) {
                        providesNeededSymbol = true;
                        break;
                    }
                }

                if (providesNeededSymbol) {
                    member.extracted = true;
                    inOutArtifacts.push_back(member.artifact);
                    progress = true;
                    extractedAny = true;
                }
            }
        }
    }

    return extractedAny;
}

bool InternalLinker::link(const std::vector<target::artifact::object::ObjectArtifact>& artifacts,
                           LinkedImage& outImage,
                           LinkOutputKind outputKind,
                           const std::vector<DynamicImport>& dynamicImports) {
    lastError_.clear();
    outImage = LinkedImage{};
    outImage.outputKind = outputKind;

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

    // Symbol resolution and rebasing (both global and local symbols)
    for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
        for (const auto& sym : artifacts[aIdx].symbols) {
            if (!sym.isDefined) continue;

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
            lsym.name = sym.name;
            lsym.virtualAddress = secVma + outputOffset + sym.value;
            lsym.size = sym.size;
            lsym.isFunction = (sym.type == target::artifact::object::SymbolType::Function);
            lsym.isGlobal = (sym.binding != target::artifact::object::SymbolBinding::Local);
            lsym.sectionName = sym.sectionName;

            if (lsym.isGlobal) {
                if (outImage.symbols.count(sym.name) && outImage.symbols[sym.name].isGlobal) {
                    lastError_ = "Duplicate global symbol definition: '" + sym.name + "'";
                    return false;
                }
                outImage.symbols[sym.name] = lsym;
            } else {
                if (!outImage.symbols.count(sym.name)) {
                    outImage.symbols[sym.name] = lsym;
                }
            }
        }
    }

    // Add Linux x86-64 _start entry stub if main is present and _start is missing (Linux x86-64 only, Executable output kind)
    if (outImage.outputKind == LinkOutputKind::Executable && outImage.os == target::OS::Linux && outImage.arch == target::Arch::X64) {
        if (!outImage.symbols.count("_start") && (outImage.symbols.count("main") || outImage.symbols.count("$main"))) {
        std::string mainName = outImage.symbols.count("main") ? "main" : "$main";
        uint64_t mainAddr = outImage.symbols[mainName].virtualAddress;

        auto* textSec = outImage.findSection(".text");
        if (textSec) {
            uint64_t startOffset = textSec->data.size();
            uint64_t startVma = textSec->virtualAddress + startOffset;

            // x86-64 _start stub (System V ABI stack aligned)
            // 31 ed                xor %ebp, %ebp
            // 48 8b 3c 24          mov (%rsp), %rdi       (argc)
            // 48 8d 74 24 08       lea 8(%rsp), %rsi      (argv)
            // 48 83 e4 f0          and $-16, %rsp         (align stack to 16 bytes)
            // e8 [rel32]           call main
            // 48 89 c7             mov %rax, %rdi
            // b8 3c 00 00 00       mov $60, %eax
            // 0f 05                syscall
            std::vector<uint8_t> startBytes = {
                0x31, 0xED,
                0x48, 0x8B, 0x3C, 0x24,
                0x48, 0x8D, 0x74, 0x24, 0x08,
                0x48, 0x83, 0xE4, 0xF0,
                0xE8, 0x00, 0x00, 0x00, 0x00,
                0x48, 0x89, 0xC7,
                0xB8, 0x3C, 0x00, 0x00, 0x00,
                0x0F, 0x05
            };

            int64_t callRel = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 20);
            int32_t callRel32 = static_cast<int32_t>(callRel);
            std::memcpy(startBytes.data() + 16, &callRel32, 4);

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
            const DynamicImport* matchedImp = nullptr;
            for (const auto& imp : dynamicImports) {
                if (imp.symbol == reloc.symbolName) {
                    matchedImp = &imp;
                    break;
                }
            }

            if (outImage.symbols.count(reloc.symbolName)) {
                targetSymAddr = outImage.symbols[reloc.symbolName].virtualAddress;
            } else if (const auto* sec = outImage.findSection(reloc.symbolName)) {
                targetSymAddr = sec->virtualAddress;
            } else if (matchedImp != nullptr) {
                if (matchedImp->kind == DynamicImportKind::Data) {
                    // Record data import fixup to be resolved against the IAT slot VMA in PeImageWriter
                    uint64_t placeAddress = lsec->virtualAddress + outputOffset + reloc.offset;
                    uint64_t sectionDataOffset = outputOffset + reloc.offset;
                    outImage.dataImportFixups.push_back({
                        reloc.sectionName,
                        sectionDataOffset,
                        placeAddress,
                        reloc.symbolName,
                        reloc.addend,
                        reloc.type
                    });
                    continue;
                } else {
                    // Synthesize or retrieve import thunk in .text for function imports
                    std::string thunkSymName = "__imp_thunk_" + reloc.symbolName;
                    if (!outImage.symbols.count(thunkSymName)) {
                        auto* textSec = outImage.findSection(".text");
                        if (!textSec) {
                            lastError_ = "Cannot synthesize import thunk for '" + reloc.symbolName + "': .text section missing";
                            return false;
                        }
                        uint64_t thunkOffset = textSec->data.size();
                        uint64_t thunkVma = textSec->virtualAddress + thunkOffset;

                        std::vector<uint8_t> thunkBytes = TargetRelocationEvaluator::getImportThunkBytes(outImage.arch, outImage.os);
                        if (thunkBytes.empty()) {
                            lastError_ = "Target import thunk generation not supported for architecture/OS";
                            return false;
                        }
                        textSec->data.insert(textSec->data.end(), thunkBytes.begin(), thunkBytes.end());
                        textSec->virtualSize = textSec->data.size();

                        LinkedSymbol thunkSym;
                        thunkSym.name = thunkSymName;
                        thunkSym.virtualAddress = thunkVma;
                        thunkSym.size = thunkBytes.size();
                        thunkSym.isFunction = true;
                        thunkSym.isGlobal = false;
                        thunkSym.sectionName = ".text";
                        outImage.symbols[thunkSymName] = thunkSym;
                        outImage.importThunkVmas[reloc.symbolName] = thunkVma;
                    }
                    targetSymAddr = outImage.symbols[thunkSymName].virtualAddress;
                }
            } else {
                lastError_ = "Unresolved undefined symbol reference: '" + reloc.symbolName + "'";
                return false;
            }

            uint64_t placeAddress = lsec->virtualAddress + outputOffset + reloc.offset;
            uint64_t sectionDataOffset = outputOffset + reloc.offset;

            RelocationKind kind = TargetRelocationEvaluator::normalizeType(reloc.type);
            if (kind == RelocationKind::Unknown) {
                lastError_ = "Relocation evaluation failed for symbol '" + reloc.symbolName + "' in section '" + reloc.sectionName + "': unknown/unsupported relocation type '" + reloc.type + "'";
                return false;
            }
            std::string evalError;
            if (!TargetRelocationEvaluator::evaluate(kind, targetSymAddr, placeAddress, reloc.addend, lsec->data, sectionDataOffset, evalError)) {
                lastError_ = "Relocation evaluation failed for '" + reloc.symbolName + "': " + evalError;
                return false;
            }

            if (kind == RelocationKind::Absolute) {
                outImage.relocationFixupVmas.push_back(placeAddress);
            }
        }
    }

    return true;
}

} // namespace linker
} // namespace artifact
} // namespace target
