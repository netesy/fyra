#include "elf.hh"
#include "codegen/execgen/platform_utils.hh"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>
#include <regex>
#include <stdexcept>

// ELF constants
namespace {
constexpr uint32_t ELF_MAGIC = 0x464C457F;
constexpr uint16_t ET_REL = 1;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint16_t EM_RISCV = 243;
constexpr uint16_t EM_AARCH64 = 183;
constexpr uint16_t SHT_NULL = 0;
constexpr uint16_t SHT_PROGBITS = 1;
constexpr uint16_t SHT_SYMTAB = 2;
constexpr uint16_t SHT_STRTAB = 3;
constexpr uint16_t SHT_RELA = 4;
constexpr uint16_t SHT_NOBITS = 8;
constexpr uint64_t SHF_WRITE = 0x1;
constexpr uint64_t SHF_ALLOC = 0x2;
constexpr uint64_t SHF_EXECINSTR = 0x4;
constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PF_X = 0x1;
constexpr uint32_t PF_W = 0x2;
constexpr uint32_t PF_R = 0x4;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STB_WEAK = 2;
constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;
constexpr uint8_t STT_SECTION = 3;
constexpr uint8_t STT_FILE = 4;
constexpr uint16_t SHN_UNDEF = 0;
constexpr uint16_t SHN_ABS = 0xFFF1;
// Relocation types for x86-64
constexpr uint32_t R_X86_64_64 = 1;
constexpr uint32_t R_X86_64_PC32 = 2;
constexpr uint32_t R_X86_64_PLT32 = 4;
// Relocation types for RISC-V
constexpr uint32_t R_RISCV_64 = 2;
constexpr uint32_t R_RISCV_JAL = 18;
constexpr uint32_t R_RISCV_CALL = 18; // Generic call
constexpr uint32_t R_RISCV_CALL_PLT = 19;
constexpr uint32_t R_RISCV_BRANCH = 16;
// Relocation types for AArch64
constexpr uint32_t R_AARCH64_ABS64 = 257;
constexpr uint32_t R_AARCH64_ADR_PREL_PG_HI21 = 275;
constexpr uint32_t R_AARCH64_LDST64_ABS_LO12_NC = 286;
constexpr uint32_t R_AARCH64_CALL26 = 283;
constexpr uint32_t R_AARCH64_JUMP26 = 282;
constexpr uint32_t R_AARCH64_CONDBR19 = 280;
}

#pragma pack(push, 1)
struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};
struct ElfHeader64 {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
struct ProgramHeader64 {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};
struct SectionHeader64 {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};
struct Symbol64 {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};
#pragma pack(pop)

inline uint8_t ELF64_ST_INFO(uint8_t bind, uint8_t type) { return (bind << 4) + (type & 0xF); }
inline uint8_t ELF64_ST_BIND(uint8_t info) { return info >> 4; }
inline uint64_t ELF64_R_INFO(uint32_t sym, uint32_t type) { return (static_cast<uint64_t>(sym) << 32) + type; }

class ElfGenerator::Impl {
public:
    Impl(const std::string& inputFilename, bool is64Bit, uint64_t baseAddr)
        : inputFilename_(inputFilename), is64Bit_(is64Bit), baseAddress_(baseAddr),
          pageSize_(0x1000), entryPointName_("_start") {}

    bool generate(const std::string& assemblyPath, const std::string& outputPath, bool generateRelocatable);
    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                          const std::vector<ElfGenerator::Symbol>& symbols,
                          const std::vector<ElfGenerator::Relocation>& relocations,
                          const std::string& outputPath);
    void setBaseAddress(uint64_t address) { baseAddress_ = address; }
    void setPageSize(uint64_t size) { pageSize_ = size; }
    void setEntryPointName(const std::string& name) { entryPointName_ = name; }
    void setMachine(uint16_t machine) { machine_ = machine; }
    std::string getLastError() const { return lastError_; }

private:
    struct Section {
        std::string name;
        uint64_t size = 0;
        uint64_t vma = 0;
        uint64_t fileOffset = 0;
        uint64_t addralign = 1;
        std::vector<uint8_t> data;
        SectionHeader64 header;
    };
    struct Symbol {
        std::string name;
        uint64_t value = 0;
        uint64_t size = 0;
        uint8_t type = STT_NOTYPE;
        uint8_t binding = STB_LOCAL;
        std::string sectionName;
        bool isDefined = false;
    };
    struct Relocation {
        uint64_t offset;
        std::string type;
        int64_t addend;
        std::string symbolName;
        std::string sectionName;
    };

    // Main flow
    bool generateExecutable(const std::string& objFile, const std::string& outputFile);

    // Parsing
    bool parseObjectFile(const std::string& objFile);
    void parseSections(const std::string& objdumpOutput);
    void parseSymbols(const std::string& objdumpOutput);
    void parseRelocations(const std::string& objdumpOutput);
    std::vector<uint8_t> readSectionContents(const std::string& objFile, const std::string& sectionName);

    // Executable generation
    void layoutSectionsForExecutable();
    bool applyRelocations();
    void createFinalElfStructure();
    void updateSymbolValues();
    void createProgramHeaders();
    bool writeExecutable(const std::string& outputFile);

    // Writing helpers
    void writeElfHeader(std::ofstream& file);
    void writeProgramHeaders(std::ofstream& file);
    void writeSectionData(std::ofstream& file);
    void writeSectionHeaders(std::ofstream& file);

    // Utility
    uint32_t addToStringTable(std::string& table, const std::string& str);
    Section* findSection(const std::string& name);
    uint16_t findFinalSectionIndex(const std::string& name);
    uint32_t findFinalSymbolIndex(const std::string& name);

    // Member variables
    std::string inputFilename_;
    bool is64Bit_;
    uint64_t baseAddress_;
    uint64_t pageSize_;
    std::string entryPointName_;
    uint64_t entryPointAddr_ = 0;
    uint16_t machine_ = 0;
    std::string lastError_;

    // Parsed data
    std::map<std::string, Section> sections_;
    std::map<std::string, Symbol> symbols_;
    std::vector<Relocation> relocations_;
    std::vector<std::string> sectionOrder_;

    // Final ELF data
    std::vector<SectionHeader64> finalSectionHeaders_;
    std::map<std::string, int> finalSectionIndexMap_;
    std::vector<ProgramHeader64> finalProgramHeaders_;
    std::vector<Symbol64> finalSymbols_;
    std::map<std::string, uint32_t> finalSymbolIndexMap_;
    std::string stringTable_ = "\0";
    std::string shStringTable_ = "\0";
    uint64_t sectionHeadersOffset_ = 0;
};

bool ElfGenerator::Impl::generate(const std::string& assemblyPath, const std::string& outputPath, bool generateRelocatable) {
    if (!is64Bit_) {
        lastError_ = "32-bit ELF generation is not supported.";
        return false;
    }
    std::string tempObjFile = outputPath + ".tmp.o";
    std::string cmd = "as --64 " + assemblyPath + " -o " + tempObjFile;
    if (PlatformUtils::runCommand(cmd) != 0) {
        lastError_ = "Assembler failed. Command: " + cmd;
        return false;
    }
    bool result = false;
    try {
        if (generateRelocatable) {
            if (std::rename(tempObjFile.c_str(), outputPath.c_str()) != 0) {
                lastError_ = "Failed to rename temporary object file to " + outputPath;
                result = false;
            } else {
                result = true;
            }
        } else {
            result = generateExecutable(tempObjFile, outputPath);
        }
    } catch (const std::exception& e) {
        lastError_ = "ELF generation failed: " + std::string(e.what());
        result = false;
    }
    PlatformUtils::deleteFile(tempObjFile);
    return result;
}

bool ElfGenerator::Impl::generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections_data,
                                        const std::vector<ElfGenerator::Symbol>& symbols_in,
                                        const std::vector<ElfGenerator::Relocation>& relocations_in,
                                        const std::string& outputPath) {
    if (!is64Bit_) {
        lastError_ = "32-bit ELF generation is not supported.";
        return false;
    }

    sections_.clear();
    symbols_.clear();
    relocations_.clear();
    sectionOrder_.clear();

    const std::vector<std::string> ordered_sections = {".text", ".rodata", ".data", ".bss"};
    for(const auto& name : ordered_sections) {
        if (sections_data.count(name)) {
            Section s;
            s.name = name;
            s.data = sections_data.at(name);
            s.size = s.data.size();
            s.addralign = 0x1000; // Force page alignment for all sections in executable
            std::memset(&s.header, 0, sizeof(SectionHeader64)); // Zero-initialize section header
            sections_[s.name] = s;
            sectionOrder_.push_back(s.name);
        }
    }

    for (const auto& sym_in : symbols_in) {
        Symbol s;
        s.name = sym_in.name;
        s.value = sym_in.value;
        s.size = sym_in.size;
        s.type = sym_in.type;
        s.binding = sym_in.binding;
        s.sectionName = sym_in.sectionName;
        s.isDefined = (s.sectionName != "*UND*");
        symbols_[s.name] = s;
    }

    relocations_.clear();
    for (const auto& r_in : relocations_in) {
        Relocation r;
        r.offset = r_in.offset;
        r.type = r_in.type;
        r.addend = r_in.addend;
        r.symbolName = r_in.symbolName;
        r.sectionName = r_in.sectionName;
        relocations_.push_back(r);
    }

    try {
        createFinalElfStructure();
        createProgramHeaders(); // First pass to get the number of headers
        layoutSectionsForExecutable();
        createProgramHeaders(); // Second pass to set correct addresses/offsets
        updateSymbolValues();
        if (!applyRelocations()) {
             return false;
        }
    } catch (const std::exception& e) {
        lastError_ = "In-memory ELF generation failed during layout: " + std::string(e.what());
        return false;
    }

    if (!writeExecutable(outputPath)) {
        return false;
    }

    PlatformUtils::setExecutablePermissions(outputPath);

    return true;
}

bool ElfGenerator::Impl::generateExecutable(const std::string& objFile, const std::string& outputFile) {
    if (!parseObjectFile(objFile)) return false;

    // 1. Create the final structures to know what/how many headers we will have.
    createFinalElfStructure();
    createProgramHeaders();

    // 2. Now that we know the number of headers, we can calculate the file layout.
    layoutSectionsForExecutable();

    // 3. With the final layout determined, we can calculate the correct virtual addresses for all symbols and the entry point.
    updateSymbolValues();

    // 4. With correct symbol addresses, we can apply relocations.
    if (!applyRelocations()) return false;

    // 5. Finally, write the complete, valid executable.
    return writeExecutable(outputFile);
}

// --- Parsing Implementation ---
bool ElfGenerator::Impl::parseObjectFile(const std::string& objFile) {
    std::string headersCmd = "objdump -h " + objFile;
    std::string symbolsCmd = "objdump -t " + objFile;
    std::string relocsCmd = "objdump -r " + objFile;

    std::string headerOutput = PlatformUtils::runCommandWithOutput(headersCmd);
    if (headerOutput.find("Sections:") == std::string::npos) { lastError_ = "objdump -h failed or produced no output"; return false; }

    std::string symbolOutput = PlatformUtils::runCommandWithOutput(symbolsCmd);
    if (symbolOutput.find("SYMBOL TABLE:") == std::string::npos) { lastError_ = "objdump -t failed or produced no output"; return false; }

    std::string relocOutput = PlatformUtils::runCommandWithOutput(relocsCmd);

    try {
        parseSections(headerOutput);
        parseSymbols(symbolOutput);
        parseRelocations(relocOutput);
        for (auto& pair : sections_) {
            if (pair.second.size > 0 && pair.first != ".bss") {
                 pair.second.data = readSectionContents(objFile, pair.first);
            }
        }
    } catch (const std::exception& e) {
        lastError_ = "Failed to parse objdump output: " + std::string(e.what());
        return false;
    }
    return true;
}

void ElfGenerator::Impl::parseSections(const std::string& objdumpOutput) {
    std::istringstream stream(objdumpOutput);
    std::string line;
    std::regex re("^\\s*[0-9]+\\s+(\\S+)\\s+([0-9a-f]+)\\s+[0-9a-f]+\\s+[0-9a-f]+\\s+[0-9a-f]+\\s+2\\*\\*([0-9]+)");
    bool in_sections = false;
    while (std::getline(stream, line)) {
        if (line.find("Sections:") != std::string::npos) { in_sections = true; continue; }
        if (!in_sections || line.find(" Idx") == 0) continue;
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            Section s;
            s.name = m[1].str();
            s.size = std::stoull(m[2].str(), nullptr, 16);
            s.addralign = 1 << std::stoull(m[3].str());
            sections_[s.name] = s;
            sectionOrder_.push_back(s.name);
        }
    }
}

void ElfGenerator::Impl::parseSymbols(const std::string& objdumpOutput) {
    std::istringstream stream(objdumpOutput);
    std::string line;
    std::regex re("^([0-9a-f]{16})\\s+(.{7})\\s+(\\S+)\\s+([0-9a-f]+)\\s+(.*)$");
    bool in_symbols = false;
    while (std::getline(stream, line)) {
        if (line.find("SYMBOL TABLE:") != std::string::npos) { in_symbols = true; continue; }
        if (!in_symbols) continue;
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            Symbol s;
            s.value = std::stoull(m[1].str(), nullptr, 16);
            std::string flags = m[2].str();
            s.sectionName = m[3].str();
            s.size = std::stoull(m[4].str(), nullptr, 16);
            s.name = m[5].str();
            if (s.name.empty() || s.name.find(".hidden") != std::string::npos) continue;
            s.isDefined = (s.sectionName != "*UND*");
            if (flags[0] == 'l') s.binding = STB_LOCAL;
            else if (flags[0] == 'g') s.binding = STB_GLOBAL;
            else if (flags[0] == 'w') s.binding = STB_WEAK;
            if (flags.find('F') != std::string::npos) s.type = STT_FUNC;
            else if (flags.find('O') != std::string::npos) s.type = STT_OBJECT;
            else if (flags.find('f') != std::string::npos) s.type = STT_FILE;
            symbols_[s.name] = s;
        }
    }
}

void ElfGenerator::Impl::parseRelocations(const std::string& objdumpOutput) {
    std::istringstream stream(objdumpOutput);
    std::string line, current_section;
    std::regex re("^([0-9a-f]+)\\s+[0-9a-f]+\\s+(\\S+)\\s+(\\S+)(?:\\s*([+-]0x[0-9a-f]+|[+-]?[0-9]+))?");
    while (std::getline(stream, line)) {
        if (line.find("RELOCATION RECORDS FOR") != std::string::npos) {
            size_t start = line.find('[') + 1;
            size_t end = line.find(']');
            current_section = line.substr(start, end - start);
        }
        if (current_section.empty()) continue;
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            Relocation r;
            r.sectionName = current_section;
            r.offset = std::stoull(m[1].str(), nullptr, 16);
            r.type = m[2].str();
            r.symbolName = m[3].str();
            r.addend = m[4].matched ? std::stoll(m[4].str(), nullptr, 0) : 0;
            relocations_.push_back(r);
        }
    }
}

std::vector<uint8_t> ElfGenerator::Impl::readSectionContents(const std::string& objFile, const std::string& sectionName) {
    std::string tempFile = objFile + ".section.bin";
    std::string cmd = "objcopy -O binary --only-section=" + sectionName + " " + objFile + " " + tempFile;
    if (PlatformUtils::runCommand(cmd) != 0) return {};
    std::ifstream file(tempFile, std::ios::binary | std::ios::ate);
    if (!file) { PlatformUtils::deleteFile(tempFile); return {}; }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();
    PlatformUtils::deleteFile(tempFile);
    return buffer;
}

// --- Executable Generation ---
void ElfGenerator::Impl::layoutSectionsForExecutable() {
    auto align_func = [&](uint64_t val, uint64_t a) { return a == 0 ? val : (val + a - 1) & ~(a - 1); };

    uint64_t fileOffset = align_func(sizeof(ElfHeader64) + finalProgramHeaders_.size() * sizeof(ProgramHeader64), pageSize_);
    uint64_t memOffset = baseAddress_ + fileOffset;

    // Layout SHF_ALLOC sections. These are loaded into memory.
    const std::vector<std::string> alloc_order = {".text", ".rodata", ".data", ".bss"};
    for (const auto& name : alloc_order) {
        Section* s = findSection(name);
        if (!s) continue;
        if (s->data.empty() && s->name != ".bss") continue;

        // Align each segment to page size to ensure correct permission separation on Linux
        memOffset = align_func(memOffset, pageSize_);
        fileOffset = align_func(fileOffset, pageSize_);

        s->header.sh_addr = memOffset;
        s->header.sh_offset = fileOffset;

        uint16_t idx = findFinalSectionIndex(s->name);
        if (idx != SHN_UNDEF) {
            finalSectionHeaders_[idx].sh_addr = memOffset;
            finalSectionHeaders_[idx].sh_offset = fileOffset;
        }

        if (s->name != ".bss") {
            fileOffset += s->data.size();
            memOffset += align_func(s->data.size(), 1);
        } else {
            memOffset += align_func(s->size, 1);
        }
    }

    // After the loadable sections, place any non-allocatable sections.
    uint64_t data_offset = align_func(fileOffset, 8);

    // Layout the data for non-allocatable sections (.symtab, .strtab, etc.)
    // These have data in the file but are not loaded into memory.
    // We must update the final section headers directly, as they are the source of truth for writing.
    for (auto& shdr : finalSectionHeaders_) {
        if (!(shdr.sh_flags & SHF_ALLOC) && shdr.sh_type != SHT_NULL && shdr.sh_type != SHT_NOBITS) {
            data_offset = align_func(data_offset, shdr.sh_addralign ? shdr.sh_addralign : 1);
            shdr.sh_offset = data_offset;
            data_offset += shdr.sh_size;
        }
    }

    // Place section header table at the very end
    sectionHeadersOffset_ = align_func(data_offset, 8);
}

bool ElfGenerator::Impl::applyRelocations() {
    for (const auto& reloc : relocations_) {
        Section* p_section = findSection(reloc.sectionName);
        if (!p_section) { lastError_ = "Relocation in unknown section " + reloc.sectionName; return false; }

        auto sym_it = symbols_.find(reloc.symbolName);
        if (sym_it == symbols_.end()) {
            // Check for local label: search for SectionName_LabelName
            std::string localName = reloc.sectionName + "_" + reloc.symbolName;
            sym_it = symbols_.find(localName);
            if (sym_it == symbols_.end()) {
                // Try funcName_labelName
                Section* s = findSection(reloc.sectionName);
                if (s) {
                    // This is a bit of a hack, we don't easily know the function name here
                    // In Fyra, labels are often prefixed with function name in textual asm,
                    // but in in-memory cg, they might just be the label name.
                }
                lastError_ = "Relocation against unknown symbol " + reloc.symbolName;
                return false;
            }
        }
        Symbol& symbol = sym_it->second;

        uint64_t S = 0;
        if(symbol.isDefined) {
            Section* s_section = findSection(symbol.sectionName);
            if (!s_section) { lastError_ = "Symbol " + symbol.name + " in unknown section " + symbol.sectionName; return false; }
            S = s_section->header.sh_addr + symbol.value;
        }

        uint64_t P = p_section->header.sh_addr + reloc.offset;
        int64_t A = reloc.addend;

        if (reloc.type == "R_X86_64_64" || reloc.type == "R_RISCV_64") {
            uint64_t value = S + A;
            if (reloc.offset + 8 <= p_section->data.size())
                *reinterpret_cast<uint64_t*>(&p_section->data[reloc.offset]) = value;
        } else if (reloc.type == "R_X86_64_PC32" || reloc.type == "R_X86_64_PLT32") {
            uint32_t value = static_cast<uint32_t>(S + A - P);
            if (reloc.offset + 4 <= p_section->data.size())
                *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = value;
        } else if (reloc.type == "R_RISCV_JAL" || reloc.type == "R_RISCV_CALL" || reloc.type == "R_RISCV_CALL_PLT") {
            int64_t offset = S + A - P;
            if (reloc.type == "R_RISCV_JAL") {
                // J-type instruction (jal)
                uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
                uint32_t imm = static_cast<uint32_t>(offset);
                inst |= (imm & 0x100000) << 11;
                inst |= (imm & 0x7FE) << 20;
                inst |= (imm & 0x800) << 9;
                inst |= (imm & 0xFF000);
                *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
            } else {
                // R_RISCV_CALL expects auipc + jalr pair.
                // auipc ra, imm[31:12]
                // jalr ra, ra, imm[11:0]
                int32_t hi = (static_cast<int32_t>(offset) + 0x800) & 0xFFFFF000;
                int32_t lo = static_cast<int32_t>(offset) - hi;

                uint32_t auipc = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset - 4]);
                uint32_t jalr = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);

                auipc |= (hi & 0xFFFFF000);
                jalr |= ((lo & 0xFFF) << 20);

                *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset - 4]) = auipc;
                *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = jalr;
            }
        } else if (reloc.type == "R_RISCV_BRANCH") {
            int32_t offset = static_cast<int32_t>(S + A - P);
            uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
            inst |= ((offset >> 12) & 0x1) << 31;
            inst |= ((offset >> 5) & 0x3F) << 25;
            inst |= ((offset >> 1) & 0xF) << 8;
            inst |= ((offset >> 11) & 0x1) << 7;
            *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
        } else if (reloc.type == "R_AARCH64_ABS64") {
            uint64_t value = S + A;
            if (reloc.offset + 8 <= p_section->data.size())
                *reinterpret_cast<uint64_t*>(&p_section->data[reloc.offset]) = value;
        } else if (reloc.type == "R_AARCH64_CALL26" || reloc.type == "R_AARCH64_JUMP26") {
            int64_t offset = S + A - P;
            uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
            inst |= (static_cast<uint32_t>(offset >> 2) & 0x03FFFFFF);
            *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
        } else if (reloc.type == "R_AARCH64_CONDBR19") {
            int64_t offset = S + A - P;
            uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
            inst |= (static_cast<uint32_t>(offset >> 2) & 0x7FFFF) << 5;
            *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
        } else if (reloc.type == "R_AARCH64_ADR_PREL_PG_HI21") {
            int64_t offset = (S & ~0xFFFLL) - (P & ~0xFFFLL);
            uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
            uint32_t imm = static_cast<uint32_t>(offset >> 12);
            inst |= (imm & 0x3) << 29;
            inst |= (imm & 0x1FFFFC) << 3;
            *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
        } else if (reloc.type == "R_AARCH64_LDST64_ABS_LO12_NC") {
            uint64_t addr = S + A;
            uint32_t inst = *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]);
            uint32_t imm = static_cast<uint32_t>(addr & 0xFFF);
            inst |= (imm >> 3) << 10; // For 64-bit ldr/str, imm is scaled by 8
            *reinterpret_cast<uint32_t*>(&p_section->data[reloc.offset]) = inst;
        } else {
            lastError_ = "Unsupported relocation type: " + reloc.type;
            return false;
        }
    }
    return true;
}

void ElfGenerator::Impl::createFinalElfStructure() {
    // 0: NULL section
    finalSectionHeaders_.push_back({});
    finalSectionIndexMap_[""] = 0;
    shStringTable_ += '\0';

    // Add sections from object file
    for (const auto& name : sectionOrder_) {
        Section* s = findSection(name);
        if (!s) continue;
        s->header.sh_name = addToStringTable(shStringTable_, s->name);
        s->header.sh_type = (s->name == ".bss") ? SHT_NOBITS : SHT_PROGBITS;
        s->header.sh_flags = 0;
        if (s->name == ".text") s->header.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        else if (s->name == ".rodata") s->header.sh_flags = SHF_ALLOC;
        else if (s->name == ".data" || s->name == ".bss") s->header.sh_flags = SHF_ALLOC | SHF_WRITE;
        s->header.sh_size = (s->name == ".bss") ? s->size : s->data.size();
        s->header.sh_addralign = s->addralign;
        finalSectionHeaders_.push_back(s->header);
        finalSectionIndexMap_[s->name] = finalSectionHeaders_.size() - 1;
    }

    // Add .shstrtab, .symtab, .strtab
    auto add_meta_section = [&](const std::string& name, uint32_t type, uint64_t entsize){
        SectionHeader64 h = {};
        h.sh_name = addToStringTable(shStringTable_, name);
        h.sh_type = type;
        h.sh_addralign = (name == ".symtab") ? 8 : 1;
        h.sh_entsize = entsize;
        finalSectionHeaders_.push_back(h);
        finalSectionIndexMap_[name] = finalSectionHeaders_.size() - 1;
    };
    add_meta_section(".shstrtab", SHT_STRTAB, 0);
    add_meta_section(".symtab", SHT_SYMTAB, sizeof(Symbol64));
    add_meta_section(".strtab", SHT_STRTAB, 0);

    // Create symbol table (values are section offsets at this point)
    finalSymbols_.push_back({}); // NULL symbol
    finalSymbolIndexMap_[""] = 0;
    addToStringTable(stringTable_, "");

    std::vector<Symbol64> locals, globals;
    for(auto const& [name, sym] : symbols_) {
        Symbol64 s64 = {};
        s64.st_name = addToStringTable(stringTable_, name);
        s64.st_size = sym.size;
        s64.st_info = ELF64_ST_INFO(sym.binding, sym.type);
        if(sym.isDefined) {
            s64.st_shndx = findFinalSectionIndex(sym.sectionName);
            s64.st_value = sym.value; // NOTE: This is an offset, not the final address
        } else {
            s64.st_shndx = SHN_UNDEF;
            s64.st_value = 0;
        }
        if (sym.binding == STB_LOCAL) locals.push_back(s64);
        else globals.push_back(s64);
    }
    uint32_t first_global_idx = 1 + locals.size();
    for(const auto& s : locals) finalSymbols_.push_back(s);
    for(const auto& s : globals) finalSymbols_.push_back(s);
    for(size_t i=0; i<finalSymbols_.size(); ++i) {
        // Correctly use the string from the string table as key
        std::string symName = &stringTable_[finalSymbols_[i].st_name];
        finalSymbolIndexMap_[symName] = i;
    }

    // Finalize meta section header SIZES
    finalSectionHeaders_[findFinalSectionIndex(".shstrtab")].sh_size = shStringTable_.size();
    finalSectionHeaders_[findFinalSectionIndex(".strtab")].sh_size = stringTable_.size();
    finalSectionHeaders_[findFinalSectionIndex(".symtab")].sh_size = finalSymbols_.size() * sizeof(Symbol64);
    finalSectionHeaders_[findFinalSectionIndex(".symtab")].sh_link = findFinalSectionIndex(".strtab");
    finalSectionHeaders_[findFinalSectionIndex(".symtab")].sh_info = first_global_idx;
}

void ElfGenerator::Impl::updateSymbolValues() {
    // Now that section addresses are known, update the final symbol values.
    for (size_t i = 0; i < finalSymbols_.size(); ++i) {
        Symbol64& s64 = finalSymbols_[i];
        if (s64.st_shndx != SHN_UNDEF && s64.st_shndx != SHN_ABS) {
            const SectionHeader64& shdr = finalSectionHeaders_[s64.st_shndx];
            s64.st_value += shdr.sh_addr; // Add the section base address to the offset
        }
    }

    // Set entry point address now that all symbol values are finalized
    auto it = finalSymbolIndexMap_.find(entryPointName_);
    if (it != finalSymbolIndexMap_.end()) {
        entryPointAddr_ = finalSymbols_[it->second].st_value;
    } else {
        // Try common entry point names
        const std::vector<std::string> alternates = {"main", "$main", "_main"};
        bool found = false;
        for (const auto& alt : alternates) {
            auto alt_it = finalSymbolIndexMap_.find(alt);
            if (alt_it != finalSymbolIndexMap_.end()) {
                entryPointAddr_ = finalSymbols_[alt_it->second].st_value;
                found = true;
                break;
            }
        }
        if (!found) {
            entryPointAddr_ = 0;
            lastError_ = "Could not find entry point ('" + entryPointName_ + "' or 'main')";
        }
    }
}

void ElfGenerator::Impl::createProgramHeaders() {
    finalProgramHeaders_.clear();

    // Create a segment for headers and executable code (.text)
    Section* text_sec = findSection(".text");
    if (text_sec) {
        ProgramHeader64 text_phdr = {};
        text_phdr.p_type = PT_LOAD;
        text_phdr.p_flags = PF_R | PF_X;
        text_phdr.p_offset = 0; // Include headers
        text_phdr.p_vaddr = baseAddress_;
        text_phdr.p_paddr = baseAddress_;
        // p_filesz should be total size of headers + text section
        text_phdr.p_filesz = text_sec->header.sh_offset + text_sec->data.size();
        // p_memsz should be the same for the first segment
        text_phdr.p_memsz = text_phdr.p_filesz;
        text_phdr.p_align = pageSize_;
        finalProgramHeaders_.push_back(text_phdr);
    }

    // Create a segment for read-only data (.rodata)
    Section* rodata_sec = findSection(".rodata");
    if (rodata_sec && !rodata_sec->data.empty()) {
        ProgramHeader64 rodata_phdr = {};
        rodata_phdr.p_type = PT_LOAD;
        rodata_phdr.p_flags = PF_R;
        rodata_phdr.p_offset = rodata_sec->header.sh_offset;
        rodata_phdr.p_vaddr = rodata_sec->header.sh_addr;
        rodata_phdr.p_paddr = rodata_sec->header.sh_addr;
        rodata_phdr.p_filesz = rodata_sec->data.size();
        rodata_phdr.p_memsz = rodata_sec->data.size();
        rodata_phdr.p_align = pageSize_;
        finalProgramHeaders_.push_back(rodata_phdr);
    }

    // Create a single segment for all writable data (.data and .bss)
    Section* data_sec = findSection(".data");
    Section* bss_sec = findSection(".bss");
    if (data_sec || bss_sec) {
        ProgramHeader64 data_phdr = {};
        data_phdr.p_type = PT_LOAD;
        data_phdr.p_flags = PF_R | PF_W;
        data_phdr.p_align = pageSize_;

        // The segment starts where the first available section starts.
        Section* first_writable = data_sec ? data_sec : bss_sec;
        data_phdr.p_offset = first_writable->header.sh_offset;
        data_phdr.p_vaddr = first_writable->header.sh_addr;
        data_phdr.p_paddr = first_writable->header.sh_addr;

        uint64_t total_file_size = 0;
        uint64_t last_vaddr = first_writable->header.sh_addr;

        if (data_sec) {
            total_file_size = (data_sec->header.sh_offset + data_sec->data.size()) - first_writable->header.sh_offset;
            last_vaddr = data_sec->header.sh_addr + data_sec->data.size();
        }
        if (bss_sec) {
            last_vaddr = bss_sec->header.sh_addr + bss_sec->size;
        }

        data_phdr.p_filesz = total_file_size;
        data_phdr.p_memsz = last_vaddr - first_writable->header.sh_addr;

        finalProgramHeaders_.push_back(data_phdr);
    }
}

void ElfGenerator::Impl::writeSectionData(std::ofstream& file) {
    for (const auto& name : sectionOrder_) {
        Section* s = findSection(name);
        if (s && s->header.sh_type != SHT_NOBITS && !s->data.empty()) {
            file.seekp(s->header.sh_offset);
            if (file.fail()) std::cerr << "ELF Error: seekp failed for section " << s->name << " at offset " << s->header.sh_offset << std::endl;
            file.write(reinterpret_cast<const char*>(s->data.data()), s->data.size());
            if (file.fail()) std::cerr << "ELF Error: write failed for section " << s->name << " size " << s->data.size() << std::endl;
            
        }
    }
    // Write meta sections
    SectionHeader64& shstrtab = finalSectionHeaders_[findFinalSectionIndex(".shstrtab")];
    file.seekp(shstrtab.sh_offset);
    file.write(shStringTable_.c_str(), shStringTable_.size());

    SectionHeader64& symtab = finalSectionHeaders_[findFinalSectionIndex(".symtab")];
    file.seekp(symtab.sh_offset);
    file.write(reinterpret_cast<const char*>(finalSymbols_.data()), finalSymbols_.size() * sizeof(Symbol64));

    SectionHeader64& strtab = finalSectionHeaders_[findFinalSectionIndex(".strtab")];
    file.seekp(strtab.sh_offset);
    file.write(stringTable_.c_str(), stringTable_.size());
}

bool ElfGenerator::Impl::writeExecutable(const std::string& outputFile) {
    std::ofstream file(outputFile, std::ios::binary | std::ios::trunc);
    if (!file) {
        lastError_ = "Cannot open output file: " + outputFile;
        return false;
    }
    writeElfHeader(file);
    writeProgramHeaders(file);
    writeSectionData(file);
    writeSectionHeaders(file);
    file.close();
    return !file.fail();
}

// --- Writing Helpers ---
void ElfGenerator::Impl::writeElfHeader(std::ofstream& file) {
    ElfHeader64 h = {};
    memcpy(h.e_ident, "\x7f""ELF", 4);
    h.e_ident[4] = 2; h.e_ident[5] = 1; h.e_ident[6] = 1; // 64, LSB, v1
    h.e_ident[7] = 0; // System V ABI
    h.e_type = ET_EXEC;

    if (machine_ != 0) {
        h.e_machine = machine_;
    } else {
        // Detect machine type from relocations
        h.e_machine = EM_X86_64;
        for (const auto& reloc : relocations_) {
            if (reloc.type.find("RISCV") != std::string::npos) {
                h.e_machine = EM_RISCV;
                break;
            } else if (reloc.type.find("AARCH64") != std::string::npos) {
                h.e_machine = EM_AARCH64;
                break;
            }
        }
    }

    h.e_version = 1;
    h.e_entry = entryPointAddr_;
    h.e_phoff = sizeof(ElfHeader64);
    h.e_shoff = sectionHeadersOffset_;
    h.e_flags = 0;
    h.e_ehsize = sizeof(ElfHeader64);
    h.e_phentsize = sizeof(ProgramHeader64);
    h.e_phnum = static_cast<uint16_t>(finalProgramHeaders_.size());
    h.e_shentsize = sizeof(SectionHeader64);
    h.e_shnum = static_cast<uint16_t>(finalSectionHeaders_.size());
    h.e_shstrndx = findFinalSectionIndex(".shstrtab");
    file.write(reinterpret_cast<const char*>(&h), sizeof(h));
}

void ElfGenerator::Impl::writeProgramHeaders(std::ofstream& file) {
    file.seekp(sizeof(ElfHeader64));
    file.write(reinterpret_cast<const char*>(finalProgramHeaders_.data()), finalProgramHeaders_.size() * sizeof(ProgramHeader64));
}


void ElfGenerator::Impl::writeSectionHeaders(std::ofstream& file) {
    file.seekp(sectionHeadersOffset_);
    file.write(reinterpret_cast<const char*>(finalSectionHeaders_.data()), finalSectionHeaders_.size() * sizeof(SectionHeader64));
}

// --- Utility Implementations ---
uint32_t ElfGenerator::Impl::addToStringTable(std::string& table, const std::string& str) {
    uint32_t offset = table.size();
    table.append(str).append(1, '\0');
    return offset;
}

ElfGenerator::Impl::Section* ElfGenerator::Impl::findSection(const std::string& name) {
    auto it = sections_.find(name);
    return it != sections_.end() ? &it->second : nullptr;
}

uint16_t ElfGenerator::Impl::findFinalSectionIndex(const std::string& name) {
    auto it = finalSectionIndexMap_.find(name);
    if (it != finalSectionIndexMap_.end()) return it->second;
    return SHN_UNDEF;
}

uint32_t ElfGenerator::Impl::findFinalSymbolIndex(const std::string& name) {
    auto it = finalSymbolIndexMap_.find(name);
    if (it != finalSymbolIndexMap_.end()) return it->second;
    return 0; // Not found, return index of NULL symbol
}

// --- Public Interface Implementation ---
ElfGenerator::ElfGenerator(const std::string& inputFilename, bool is64Bit, uint64_t baseAddress)
    : pImpl(std::make_unique<Impl>(inputFilename, is64Bit, baseAddress)) {}
ElfGenerator::~ElfGenerator() = default;
bool ElfGenerator::generate(const std::string& assemblyPath, const std::string& outputPath, bool generateRelocatable) {
    return pImpl->generate(assemblyPath, outputPath, generateRelocatable);
}

bool ElfGenerator::generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                                  const std::vector<ElfGenerator::Symbol>& symbols,
                                  const std::vector<ElfGenerator::Relocation>& relocations,
                                  const std::string& outputPath) {
    return pImpl->generateFromCode(sections, symbols, relocations, outputPath);
}
void ElfGenerator::setBaseAddress(uint64_t address) { pImpl->setBaseAddress(address); }
void ElfGenerator::setPageSize(uint64_t size) { pImpl->setPageSize(size); }
void ElfGenerator::setEntryPointName(const std::string& name) { pImpl->setEntryPointName(name); }
void ElfGenerator::setMachine(uint16_t machine) { pImpl->setMachine(machine); }
std::string ElfGenerator::getLastError() const { return pImpl->getLastError(); }