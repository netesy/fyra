#include "target/artifact/executable/PeImage.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace target::artifact::executable {
namespace {

#pragma pack(push, 1)
struct DosHeader {
    uint16_t magic;
    uint8_t unused[58];
    uint32_t peOffset;
};
struct CoffHeader {
    uint16_t machine, sectionCount;
    uint32_t timestamp, symbolTable, symbolCount;
    uint16_t optionalHeaderSize, characteristics;
};
struct Directory { uint32_t rva, size; };
struct OptionalHeader64 {
    uint16_t magic;
    uint8_t linkerMajor, linkerMinor;
    uint32_t codeSize, initializedSize, uninitializedSize, entryRva, codeBase;
    uint64_t imageBase;
    uint32_t sectionAlignment, fileAlignment;
    uint16_t osMajor, osMinor, imageMajor, imageMinor, subsystemMajor, subsystemMinor;
    uint32_t win32Version, imageSize, headersSize, checksum;
    uint16_t subsystem, dllCharacteristics;
    uint64_t stackReserve, stackCommit, heapReserve, heapCommit;
    uint32_t loaderFlags, directoryCount;
    Directory directories[16];
};
struct SectionHeader {
    char name[8];
    uint32_t virtualSize, rva, rawSize, rawOffset, relocOffset, lineOffset;
    uint16_t relocCount, lineCount;
    uint32_t characteristics;
};
struct ExportDirectory {
    uint32_t characteristics, timestamp;
    uint16_t major, minor;
    uint32_t name, ordinalBase, functionCount, nameCount;
    uint32_t functions, names, ordinals;
};
struct ImportDescriptor {
    uint32_t originalFirstThunk;
    uint32_t timeDateStamp;
    uint32_t forwarderChain;
    uint32_t name;
    uint32_t firstThunk;
};
#pragma pack(pop)

struct LaidOutSection {
    PeSection section;
    uint32_t rva = 0;
    uint32_t rawSize = 0;
    uint32_t rawOffset = 0;
};

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t characteristicsFor(const linker::LinkedSection& section) {
    uint32_t value = section.name == ".bss" ? 0x80 : (section.isExecutable ? 0x20 : 0x40);
    if (section.isExecutable) value |= 0x20000000;
    if (section.isReadable) value |= 0x40000000;
    if (section.isWritable) value |= 0x80000000;
    return value;
}

PeSection convertSection(const linker::LinkedSection& section) {
    return {section.name, section.data, static_cast<uint32_t>(section.virtualSize), characteristicsFor(section)};
}

template <typename SectionMap>
void appendLinkedSections(const SectionMap& input, std::vector<PeSection>& output) {
    std::vector<const linker::LinkedSection*> ordered;
    for (const auto& [name, section] : input) ordered.push_back(&section);
    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->virtualAddress < rhs->virtualAddress;
    });
    for (const auto* section : ordered) output.push_back(convertSection(*section));
}

} // namespace

bool PeImageWriter::write(PeImage image, const std::string& outputPath) {
    lastError_.clear();
    if (image.machine != 0x8664) {
        lastError_ = "PE image writer currently supports AMD64 PE32+ only";
        return false;
    }
    if (!image.sectionAlignment || !image.fileAlignment) {
        lastError_ = "PE alignment values must be non-zero";
        return false;
    }

    // Import table (.idata) construction if imports exist
    if (!image.imports.empty()) {
        const uint32_t idataRva = alignUp(image.sectionAlignment + [&] {
            uint32_t end = 0;
            for (const auto& s : image.sections)
                end = alignUp(end + std::max<uint32_t>(s.virtualSize, s.data.size()), image.sectionAlignment);
            return end;
        }(), image.sectionAlignment);

        uint32_t numDlls = static_cast<uint32_t>(image.imports.size());
        uint32_t descTableSize = (numDlls + 1) * sizeof(ImportDescriptor);

        uint32_t totalThunks = 0;
        for (const auto& imp : image.imports) {
            totalThunks += static_cast<uint32_t>(imp.symbols.size() + 1); // +1 for null terminator
        }

        uint32_t iltOffset = descTableSize;
        uint32_t iatOffset = iltOffset + totalThunks * sizeof(uint64_t);
        uint32_t namesOffset = iatOffset + totalThunks * sizeof(uint64_t);

        std::vector<uint8_t> idataBytes(namesOffset, 0);

        auto appendString = [&](const std::string& str) -> uint32_t {
            uint32_t off = static_cast<uint32_t>(idataBytes.size());
            idataBytes.insert(idataBytes.end(), str.begin(), str.end());
            idataBytes.push_back(0);
            return off;
        };

        auto appendImportByName = [&](uint16_t hint, const std::string& name) -> uint32_t {
            uint32_t off = static_cast<uint32_t>(idataBytes.size());
            idataBytes.push_back(static_cast<uint8_t>(hint & 0xFF));
            idataBytes.push_back(static_cast<uint8_t>((hint >> 8) & 0xFF));
            idataBytes.insert(idataBytes.end(), name.begin(), name.end());
            idataBytes.push_back(0);
            if (idataBytes.size() % 2 != 0) idataBytes.push_back(0); // align to 2 bytes
            return off;
        };

        std::map<std::string, uint64_t> symbolIatVma;
        uint32_t currentThunkIdx = 0;
        for (size_t d = 0; d < image.imports.size(); ++d) {
            const auto& imp = image.imports[d];
            uint32_t dllNameOff = appendString(imp.dllName);

            uint32_t dllIltRva = idataRva + iltOffset + currentThunkIdx * sizeof(uint64_t);
            uint32_t dllIatRva = idataRva + iatOffset + currentThunkIdx * sizeof(uint64_t);

            ImportDescriptor desc{};
            desc.originalFirstThunk = dllIltRva;
            desc.timeDateStamp = 0;
            desc.forwarderChain = 0;
            desc.name = idataRva + dllNameOff;
            desc.firstThunk = dllIatRva;

            std::memcpy(idataBytes.data() + d * sizeof(ImportDescriptor), &desc, sizeof(desc));

            for (size_t s = 0; s < imp.symbols.size(); ++s) {
                uint64_t thunkValue = 0;
                if (imp.symbols[s].isOrdinal) {
                    if (imp.symbols[s].ordinal == 0 || imp.symbols[s].ordinal > 0xFFFF) {
                        lastError_ = "PE import ordinal out of range [1..65535]: " + std::to_string(imp.symbols[s].ordinal);
                        return false;
                    }
                    thunkValue = (1ULL << 63) | (imp.symbols[s].ordinal & 0xFFFF);
                } else {
                    uint32_t ibnOff = appendImportByName(imp.symbols[s].hint, imp.symbols[s].name);
                    thunkValue = idataRva + ibnOff;
                }

                std::memcpy(idataBytes.data() + iltOffset + (currentThunkIdx + s) * sizeof(uint64_t), &thunkValue, sizeof(uint64_t));
                std::memcpy(idataBytes.data() + iatOffset + (currentThunkIdx + s) * sizeof(uint64_t), &thunkValue, sizeof(uint64_t));

                symbolIatVma[imp.symbols[s].name] = image.imageBase + dllIatRva + s * sizeof(uint64_t);
            }

            currentThunkIdx += static_cast<uint32_t>(imp.symbols.size() + 1);
        }

        // Patch synthesized import thunks in .text (ff 25 [disp32]) directly using exact thunk VMA
        auto textIt = std::find_if(image.sections.begin(), image.sections.end(), [](const PeSection& s) { return s.name == ".text"; });
        if (textIt != image.sections.end()) {
            const uint32_t textRva = image.sectionAlignment;
            for (const auto& [symName, iatVma] : symbolIatVma) {
                auto thunkIt = image.importThunkVmas.find(symName);
                if (thunkIt != image.importThunkVmas.end()) {
                    uint64_t thunkVma = thunkIt->second;
                    if (thunkVma >= image.imageBase + textRva) {
                        uint64_t thunkOffsetInText = thunkVma - (image.imageBase + textRva);
                        if (thunkOffsetInText + 6 <= textIt->data.size()) {
                            int64_t disp = static_cast<int64_t>(iatVma) - static_cast<int64_t>(thunkVma + 6);
                            int32_t disp32 = static_cast<int32_t>(disp);
                            std::memcpy(textIt->data.data() + thunkOffsetInText + 2, &disp32, 4);
                        }
                    }
                }
            }
        }

        image.dataDirectories[1] = {idataRva, static_cast<uint32_t>(descTableSize)}; // IMAGE_DIRECTORY_ENTRY_IMPORT
        image.dataDirectories[12] = {idataRva + iatOffset, static_cast<uint32_t>(totalThunks * sizeof(uint64_t))}; // IMAGE_DIRECTORY_ENTRY_IAT

        // .idata section flags: Read + Write + Initialized Data
        image.sections.push_back({".idata", std::move(idataBytes), 0, 0xC0000040});
    }

    // Base relocations (.reloc) construction if fixups exist
    if (!image.relocationFixupVmas.empty()) {
        std::vector<uint32_t> fixupRvas;
        for (uint64_t vma : image.relocationFixupVmas) {
            if (vma < image.imageBase) {
                lastError_ = "Invalid base relocation VMA below image base: " + std::to_string(vma);
                return false;
            }
            fixupRvas.push_back(static_cast<uint32_t>(vma - image.imageBase));
        }
        std::sort(fixupRvas.begin(), fixupRvas.end());
        fixupRvas.erase(std::unique(fixupRvas.begin(), fixupRvas.end()), fixupRvas.end());

        if (!fixupRvas.empty()) {
            const uint32_t relocRva = alignUp(image.sectionAlignment + [&] {
                uint32_t end = 0;
                for (const auto& s : image.sections)
                    end = alignUp(end + std::max<uint32_t>(s.virtualSize, s.data.size()), image.sectionAlignment);
                return end;
            }(), image.sectionAlignment);

            std::map<uint32_t, std::vector<uint16_t>> pageGroups;
            for (uint32_t rvaVal : fixupRvas) {
                uint32_t pageRva = rvaVal & ~0xFFFU;
                uint16_t offsetInPage = static_cast<uint16_t>(rvaVal & 0xFFFU);
                pageGroups[pageRva].push_back(offsetInPage);
            }

            std::vector<uint8_t> relocBytes;
            for (const auto& [pageRva, offsets] : pageGroups) {
                uint32_t entryCount = static_cast<uint32_t>(offsets.size());
                bool needsPadding = (entryCount % 2 != 0);
                uint32_t totalEntries = entryCount + (needsPadding ? 1 : 0);
                uint32_t blockSize = static_cast<uint32_t>(sizeof(uint32_t) * 2 + totalEntries * sizeof(uint16_t));

                auto append32 = [&](uint32_t val) {
                    uint8_t b[4];
                    std::memcpy(b, &val, 4);
                    relocBytes.insert(relocBytes.end(), b, b + 4);
                };
                auto append16 = [&](uint16_t val) {
                    uint8_t b[2];
                    std::memcpy(b, &val, 2);
                    relocBytes.insert(relocBytes.end(), b, b + 2);
                };

                append32(pageRva);
                append32(blockSize);

                for (uint16_t off : offsets) {
                    uint16_t entry = (10 << 12) | (off & 0x0FFF); // 10 = IMAGE_REL_BASED_DIR64
                    append16(entry);
                }
                if (needsPadding) {
                    append16(0); // 0 = IMAGE_REL_BASED_ABSOLUTE
                }
            }

            image.dataDirectories[5] = {relocRva, static_cast<uint32_t>(relocBytes.size())}; // IMAGE_DIRECTORY_ENTRY_BASERELOC
            image.dllCharacteristics |= 0x0040; // IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
            // .reloc section flags: Read + Initialized Data + Discardable (0x42000040)
            image.sections.push_back({".reloc", std::move(relocBytes), 0, 0x42000040});
        }
    }

    // Export records are PE semantics; construct .edata here after ordinary linking.
    if (!image.exports.empty()) {
        const uint32_t edataRva = alignUp(image.sectionAlignment + [&] {
            uint32_t end = 0;
            for (const auto& s : image.sections)
                end = alignUp(end + std::max<uint32_t>(s.virtualSize, s.data.size()), image.sectionAlignment);
            return end;
        }(), image.sectionAlignment);
        std::vector<uint8_t> data(sizeof(ExportDirectory) + image.exports.size() * 4 * 2 + image.exports.size() * 2, 0);
        const uint32_t eatOffset = sizeof(ExportDirectory);
        const uint32_t namesOffset = eatOffset + image.exports.size() * 4;
        const uint32_t ordinalsOffset = namesOffset + image.exports.size() * 4;
        auto appendString = [&](const std::string& value) {
            uint32_t offset = static_cast<uint32_t>(data.size());
            data.insert(data.end(), value.begin(), value.end());
            data.push_back(0);
            return offset;
        };
        uint32_t dllNameOffset = appendString(image.imageName.empty() ? "image.dll" : image.imageName);
        std::vector<uint32_t> exportNameOffsets;
        for (const auto& exp : image.exports) exportNameOffsets.push_back(appendString(exp.name));

        ExportDirectory directory{};
        directory.name = edataRva + dllNameOffset;
        directory.ordinalBase = 1;
        directory.functionCount = static_cast<uint32_t>(image.exports.size());
        directory.nameCount = directory.functionCount;
        directory.functions = edataRva + eatOffset;
        directory.names = edataRva + namesOffset;
        directory.ordinals = edataRva + ordinalsOffset;
        std::memcpy(data.data(), &directory, sizeof(directory));
        for (size_t i = 0; i < image.exports.size(); ++i) {
            uint32_t functionRva = 0;
            uint32_t currentRva = image.sectionAlignment;
            for (const auto& section : image.sections) {
                if (section.name == image.exports[i].sectionName) functionRva = currentRva + image.exports[i].sectionOffset;
                currentRva = alignUp(currentRva + std::max<uint32_t>(section.virtualSize, section.data.size()), image.sectionAlignment);
            }
            uint32_t nameRva = edataRva + exportNameOffsets[i];
            uint16_t ordinal = static_cast<uint16_t>(i);
            std::memcpy(data.data() + eatOffset + i * 4, &functionRva, 4);
            std::memcpy(data.data() + namesOffset + i * 4, &nameRva, 4);
            std::memcpy(data.data() + ordinalsOffset + i * 2, &ordinal, 2);
        }
        image.dataDirectories[0] = {edataRva, static_cast<uint32_t>(data.size())};
        image.sections.push_back({".edata", std::move(data), 0, 0x40000040});
    }

    const uint32_t headersSize = alignUp(128 + 4 + sizeof(CoffHeader) + sizeof(OptionalHeader64) +
                                             image.sections.size() * sizeof(SectionHeader),
                                         image.fileAlignment);
    std::vector<LaidOutSection> sections;
    uint32_t rva = image.sectionAlignment;
    uint32_t rawOffset = headersSize;
    for (auto& section : image.sections) {
        LaidOutSection out;
        out.section = std::move(section);
        out.section.virtualSize = std::max<uint32_t>(out.section.virtualSize, out.section.data.size());
        out.rva = rva;
        const bool uninitialized = (out.section.characteristics & 0x80) != 0;
        out.rawSize = uninitialized ? 0 : alignUp(static_cast<uint32_t>(out.section.data.size()), image.fileAlignment);
        out.rawOffset = out.rawSize ? rawOffset : 0;
        rawOffset += out.rawSize;
        rva = alignUp(rva + out.section.virtualSize, image.sectionAlignment);
        sections.push_back(std::move(out));
    }

    DosHeader dos{};
    dos.magic = 0x5a4d;
    dos.peOffset = 128;
    CoffHeader coff{};
    coff.machine = image.machine;
    coff.sectionCount = static_cast<uint16_t>(sections.size());
    coff.optionalHeaderSize = sizeof(OptionalHeader64);
    coff.characteristics = 0x0002 | 0x0020 | (image.kind == PeImageKind::Dll ? 0x2000 : 0);
    OptionalHeader64 optional{};
    optional.magic = 0x20b;
    optional.entryRva = image.kind == PeImageKind::Dll ? 0 : image.entryRva;
    optional.imageBase = image.imageBase;
    optional.sectionAlignment = image.sectionAlignment;
    optional.fileAlignment = image.fileAlignment;
    optional.osMajor = 6;
    optional.subsystemMajor = 6;
    optional.imageSize = rva;
    optional.headersSize = headersSize;
    optional.subsystem = image.subsystem;
    optional.dllCharacteristics = image.dllCharacteristics;
    optional.stackReserve = optional.heapReserve = 0x100000;
    optional.stackCommit = optional.heapCommit = 0x1000;
    optional.directoryCount = 16;
    for (size_t i = 0; i < 16; ++i)
        optional.directories[i] = {image.dataDirectories[i].virtualAddress, image.dataDirectories[i].size};
    for (const auto& section : sections) {
        if (section.section.characteristics & 0x20) {
            optional.codeSize += section.rawSize;
            if (!optional.codeBase) optional.codeBase = section.rva;
        } else if (section.section.characteristics & 0x40) optional.initializedSize += section.rawSize;
        if (section.section.characteristics & 0x80) optional.uninitializedSize += section.section.virtualSize;
    }

    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) { lastError_ = "failed to open PE output: " + outputPath; return false; }
    file.write(reinterpret_cast<const char*>(&dos), sizeof(dos));
    static const uint8_t stub[64] = {0x0e, 0x1f, 0xba, 0x0e, 0, 0xb4, 9, 0xcd, 0x21};
    file.write(reinterpret_cast<const char*>(stub), sizeof(stub));
    const uint32_t signature = 0x00004550;
    file.write(reinterpret_cast<const char*>(&signature), 4);
    file.write(reinterpret_cast<const char*>(&coff), sizeof(coff));
    file.write(reinterpret_cast<const char*>(&optional), sizeof(optional));
    for (const auto& section : sections) {
        SectionHeader header{};
        std::memcpy(header.name, section.section.name.data(), std::min<size_t>(8, section.section.name.size()));
        header.virtualSize = section.section.virtualSize;
        header.rva = section.rva;
        header.rawSize = section.rawSize;
        header.rawOffset = section.rawOffset;
        header.characteristics = section.section.characteristics;
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    for (const auto& section : sections) {
        if (!section.rawSize) continue;
        file.seekp(section.rawOffset);
        file.write(reinterpret_cast<const char*>(section.section.data.data()), section.section.data.size());
        std::vector<char> padding(section.rawSize - section.section.data.size(), 0);
        file.write(padding.data(), padding.size());
    }
    if (!file) { lastError_ = "failed while writing PE output: " + outputPath; return false; }
    return true;
}

bool PeExecutableImageBuilder::build(const linker::LinkedImage& image, const std::string& outputPath) {
    lastError_.clear();
    if (image.os != target::OS::Windows || image.arch != target::Arch::X64 ||
        image.outputKind != linker::LinkOutputKind::Executable) {
        lastError_ = "PE executable builder requires a Windows x64 executable LinkedImage";
        return false;
    }
    PeImage pe;
    pe.kind = PeImageKind::Executable;
    pe.relocationFixupVmas = image.relocationFixupVmas;
    pe.importThunkVmas = image.importThunkVmas;
    appendLinkedSections(image.sections, pe.sections);
    if (image.entryAddress >= pe.imageBase && image.entryAddress - pe.imageBase <= std::numeric_limits<uint32_t>::max())
        pe.entryRva = static_cast<uint32_t>(image.entryAddress - pe.imageBase);
    PeImageWriter writer;
    if (!writer.write(std::move(pe), outputPath)) { lastError_ = writer.getLastError(); return false; }
    return true;
}

bool PeExecutableImageBuilder::buildWithPlan(const linker::DynamicLinkPlan& plan, const std::string& outputPath) {
    lastError_.clear();
    if (plan.os != target::OS::Windows || plan.arch != target::Arch::X64) {
        lastError_ = "PE executable builder requires a Windows x64 plan";
        return false;
    }
    PeImage pe;
    pe.kind = PeImageKind::Executable;
    appendLinkedSections(plan.sections, pe.sections);
    if (plan.entryAddress >= pe.imageBase && plan.entryAddress - pe.imageBase <= std::numeric_limits<uint32_t>::max())
        pe.entryRva = static_cast<uint32_t>(plan.entryAddress - pe.imageBase);

    std::map<std::string, std::vector<PeImportSymbol>> importMap;
    for (const auto& imp : plan.imports) {
        PeImportSymbol peSym;
        peSym.name = imp.symbol;
        peSym.hint = 0;
        peSym.isOrdinal = imp.isOrdinal;
        peSym.ordinal = imp.ordinal;
        peSym.isData = (imp.kind == linker::DynamicImportKind::Data);
        importMap[imp.dependencyLibrary].push_back(peSym);
    }
    for (const auto& [dll, syms] : importMap) {
        pe.imports.push_back({dll, syms});
    }

    PeImageWriter writer;
    if (!writer.write(std::move(pe), outputPath)) { lastError_ = writer.getLastError(); return false; }
    return true;
}

PeImage createPeImageFromDynamicPlan(const linker::DynamicLinkPlan& plan, const std::string& imageName) {
    PeImage pe;
    pe.kind = PeImageKind::Dll;
    pe.imageName = imageName;
    pe.dllCharacteristics = 0;
    pe.relocationFixupVmas = plan.relocations.empty() ? std::vector<uint64_t>{} : [&] {
        std::vector<uint64_t> vmas;
        for (const auto& r : plan.relocations) vmas.push_back(r.offset);
        return vmas;
    }();
    appendLinkedSections(plan.sections, pe.sections);
    for (const auto& exp : plan.exports) {
        const auto* section = plan.findSection(exp.sectionName);
        if (!section || exp.address < section->virtualAddress) continue;
        pe.exports.push_back({exp.symbol, exp.sectionName,
                              static_cast<uint32_t>(exp.address - section->virtualAddress)});
    }
    return pe;
}

} // namespace target::artifact::executable
