#include "pe.hh"
#include "platform_utils.hh"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <ctime>
#include <ios>
#include <iomanip>
#include <set>

#pragma pack(push, 1)
struct DOSHeader {
    uint16_t e_magic;
    uint16_t e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid, e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
};
struct FileHeader {
    uint16_t Machine, NumberOfSections;
    uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    uint16_t SizeOfOptionalHeader, Characteristics;
};
struct DataDirectory { uint32_t VirtualAddress, Size; };
struct OptionalHeader64 {
    uint16_t Magic;
    uint8_t MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion, MajorImageVersion, MinorImageVersion, MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint64_t SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags, NumberOfRvaAndSizes;
    DataDirectory dataDirectory[16];
};
struct SectionHeader {
    uint8_t Name[8];
    uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers;
    uint16_t NumberOfRelocations, NumberOfLinenumbers;
    uint32_t Characteristics;
};
struct ImportDescriptor {
    uint32_t OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk;
};
#pragma pack(pop)

class PEGenerator::Impl {
public:
    Impl(bool is64Bit, uint64_t baseAddr) : is64Bit_(is64Bit), baseAddress_(baseAddr) {}

    struct Section {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t virtualSize, virtualAddress, rawDataSize, rawDataPointer, characteristics;
    };

    void addSection(const std::string& name, const std::vector<uint8_t>& data,
                    uint32_t virtualSize, uint32_t characteristics) {
        Section s; s.name = name; s.data = data; s.virtualSize = virtualSize; s.characteristics = characteristics;
        sections_.push_back(s);
    }

    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections_in,
                          const std::vector<PEGenerator::Symbol>& symbols_in,
                          const std::vector<PEGenerator::Relocation>& relocs_in,
                          const std::string& outputPath) {
        sections_.clear(); imports_.clear(); modIatOffsets_.clear(); importedSymbols_.clear(); entryPoint_ = 0;

        // 1. Identify Imports
        std::set<std::string> defined;
        for (auto const& s : symbols_in) defined.insert(s.name);
        for (auto const& r : relocs_in) {
            if (defined.find(r.symbolName) == defined.end() && r.symbolName == "ExitProcess") {
                addImport("kernel32.dll", "ExitProcess");
            }
        }

        // 2. Layout Sections
        uint32_t currentRva = sectionAlignment_;
        for (auto const& entry : sections_in) {
            if (entry.second.empty()) continue;
            Section s; s.name = entry.first; s.data = entry.second; s.virtualAddress = currentRva;
            s.virtualSize = s.data.size(); s.rawDataSize = align(s.data.size(), fileAlignment_);
            if (s.name == ".text" || s.name == "CODE") s.characteristics = 0x60000020;
            else if (s.name == ".data" || s.name == "DATA") s.characteristics = 0xC0000040;
            else s.characteristics = 0x40000040;
            sections_.push_back(s);
            currentRva = align(currentRva + s.virtualSize, sectionAlignment_);
        }

        // 3. Import Section (.idata)
        if (!imports_.empty()) {
            Section idata; idata.name = ".idata"; idata.characteristics = 0xC0000040;
            idata.virtualAddress = currentRva; importDirectoryRVA_ = idata.virtualAddress;
            idata.data = generateImportSection(idata.virtualAddress);
            idata.virtualSize = idata.data.size(); idata.rawDataSize = align(idata.virtualSize, fileAlignment_);
            sections_.push_back(idata);
            currentRva = align(currentRva + idata.virtualSize, sectionAlignment_);
        }

        // 4. Trampolines for Imports
        Section* textSec = findSection(".text");
        if (textSec && !imports_.empty()) {
            for (auto const& mod : imports_) {
                for (size_t i = 0; i < mod.second.size(); ++i) {
                    uint32_t iatRva = importDirectoryRVA_ + modIatOffsets_[mod.first] + (is64Bit_ ? i * 8 : i * 4);
                    uint32_t trampolinePos = textSec->data.size();
                    textSec->data.push_back(0xFF); textSec->data.push_back(0x25);
                    textSec->data.resize(textSec->data.size() + 4, 0);
                    uint32_t nextInstrRva = textSec->virtualAddress + trampolinePos + 6;
                    int32_t rel = (int32_t)iatRva - (int32_t)nextInstrRva;
                    std::memcpy(textSec->data.data() + trampolinePos + 2, &rel, 4);
                    PEGenerator::Symbol s; s.name = mod.second[i]; s.value = trampolinePos; s.sectionName = ".text";
                    importedSymbols_.push_back(s);
                }
            }
            textSec->virtualSize = textSec->data.size();
            textSec->rawDataSize = align(textSec->virtualSize, fileAlignment_);
        }

        // 5. Relocations
        std::vector<PEGenerator::Symbol> allSymbols = symbols_in;
        allSymbols.insert(allSymbols.end(), importedSymbols_.begin(), importedSymbols_.end());
        processRelocations(relocs_in, allSymbols);

        // 6. Finalize Layout
        uint32_t headerSize = align(sizeof(DOSHeader) + 64 + 4 + sizeof(FileHeader) + sizeof(OptionalHeader64) + sections_.size() * sizeof(SectionHeader), fileAlignment_);
        uint32_t currentFilePos = headerSize;
        for (auto& s : sections_) { s.rawDataPointer = currentFilePos; currentFilePos += s.rawDataSize; }

        // 7. Write File
        std::ofstream file(outputPath, std::ios::binary); if (!file.is_open()) return false;
        writeDOSHeader(file); writeNTHeaders(file, headerSize, currentRva); writeSectionHeaders(file); writeSectionData(file);
        return true;
    }

    void addImport(const std::string& mod, const std::string& func) { imports_[mod].push_back(func); }
    void setMachine(uint16_t m) { machine_ = m; }
    void setBaseAddress(uint64_t a) { baseAddress_ = a; }
    void setSectionAlignment(uint32_t a) { sectionAlignment_ = a; }
    void setFileAlignment(uint32_t a) { fileAlignment_ = a; }
    std::string getLastError() const { return ""; }
    void setEntryPoint(uint64_t a) { entryPoint_ = (uint32_t)a; }
    void setSubsystem(uint16_t s) { subsystem_ = s; }
    void setPageSize(uint64_t s) { (void)s; }

private:
    bool is64Bit_;
    uint64_t baseAddress_ = 0x140000000;
    uint16_t machine_ = 0x8664, subsystem_ = 3;
    uint32_t sectionAlignment_ = 0x1000, fileAlignment_ = 0x200, entryPoint_ = 0, importDirectoryRVA_ = 0;
    std::vector<Section> sections_;
    std::map<std::string, std::vector<std::string>> imports_;
    std::map<std::string, uint32_t> modIatOffsets_;
    std::vector<PEGenerator::Symbol> importedSymbols_;

    uint32_t align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
    Section* findSection(const std::string& n) { for (auto& s : sections_) if (s.name == n) return &s; return nullptr; }

    std::vector<uint8_t> generateImportSection(uint32_t baseRva) {
        std::vector<uint8_t> data;
        uint32_t descSize = (imports_.size() + 1) * sizeof(ImportDescriptor);
        data.resize(descSize, 0);
        std::map<std::string, uint32_t> dllNames;
        for (auto const& m : imports_) {
            dllNames[m.first] = baseRva + data.size();
            for (char c : m.first) data.push_back(c);
            data.push_back(0);
        }
        struct Layout { uint32_t ilt, iat; };
        std::map<std::string, Layout> layouts;
        for (auto const& m : imports_) {
            data.resize(align(data.size(), 8), 0);
            layouts[m.first].ilt = baseRva + data.size();
            data.resize(data.size() + (m.second.size() + 1) * 8, 0);
            layouts[m.first].iat = baseRva + data.size();
            modIatOffsets_[m.first] = data.size();
            data.resize(data.size() + (m.second.size() + 1) * 8, 0);
        }
        for (auto const& m : imports_) {
            for (size_t i = 0; i < m.second.size(); ++i) {
                uint32_t hintRva = baseRva + data.size();
                data.push_back(0); data.push_back(0);
                for (char c : m.second[i]) data.push_back(c);
                data.push_back(0); if (data.size() % 2) data.push_back(0);
                uint64_t val = hintRva;
                std::memcpy(data.data() + (layouts[m.first].ilt - baseRva) + i * 8, &val, 8);
                std::memcpy(data.data() + (layouts[m.first].iat - baseRva) + i * 8, &val, 8);
            }
        }
        uint32_t idx = 0;
        for (auto const& m : imports_) {
            ImportDescriptor id = { layouts[m.first].ilt, 0, 0, dllNames[m.first], layouts[m.first].iat };
            std::memcpy(data.data() + idx++ * sizeof(ImportDescriptor), &id, sizeof(id));
        }
        return data;
    }

    void processRelocations(const std::vector<PEGenerator::Relocation>& relocs, const std::vector<PEGenerator::Symbol>& symbols) {
        std::map<std::string, uint32_t> symbolRva;
        for (auto const& s : symbols) {
            Section* sec = findSection(s.sectionName);
            if (sec) symbolRva[s.name] = sec->virtualAddress + (uint32_t)s.value;
        }
        if (!entryPoint_) {
            if (symbolRva.count("_start")) entryPoint_ = symbolRva["_start"];
            else if (symbolRva.count("main")) entryPoint_ = symbolRva["main"];
        }
        for (auto const& r : relocs) {
            Section* target = findSection(r.sectionName);
            if (!target || !symbolRva.count(r.symbolName)) continue;
            uint32_t S = symbolRva[r.symbolName], P = target->virtualAddress + (uint32_t)r.offset;
            if (r.type == "R_X86_64_PC32" || r.type == "R_X86_64_PLT32") {
                int32_t delta = (int32_t)S + (int32_t)r.addend - (int32_t)P;
                std::memcpy(target->data.data() + r.offset, &delta, 4);
            } else if (r.type == "R_X86_64_64") {
                uint64_t val = baseAddress_ + S + r.addend;
                std::memcpy(target->data.data() + r.offset, &val, 8);
            }
        }
    }

    void writeDOSHeader(std::ofstream& f) {
        DOSHeader h = {0x5A4D, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0, {0}, 128};
        f.write((char*)&h, sizeof(h));
        uint8_t stub[64] = {0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68, 0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F, 0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20, 0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24};
        f.write((char*)stub, 64);
    }

    void writeNTHeaders(std::ofstream& f, uint32_t headSize, uint32_t imgSize) {
        uint32_t sig = 0x00004550; f.write((char*)&sig, 4);
        FileHeader fh = { (uint16_t)machine_, (uint16_t)sections_.size(), (uint32_t)time(0), 0, 0, (uint16_t)sizeof(OptionalHeader64), 0x22 };
        f.write((char*)&fh, sizeof(fh));
        OptionalHeader64 oh = { 0x20b, 0, 0, 0, 0, 0, entryPoint_, 0x1000, baseAddress_, sectionAlignment_, fileAlignment_, 6, 0, 0, 0, 6, 0, 0, align(imgSize, sectionAlignment_), headSize, 0, subsystem_, 0x8160, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16, {{0, 0}} };
        if (importDirectoryRVA_) { oh.dataDirectory[1] = { importDirectoryRVA_, (uint32_t)(imports_.size() * sizeof(ImportDescriptor)) }; }
        f.write((char*)&oh, sizeof(oh));
    }

    void writeSectionHeaders(std::ofstream& f) {
        for (auto const& s : sections_) {
            SectionHeader sh = { {0}, s.virtualSize, s.virtualAddress, s.rawDataSize, s.rawDataPointer, 0, 0, 0, 0, s.characteristics };
            std::strncpy((char*)sh.Name, s.name.c_str(), 8);
            f.write((char*)&sh, sizeof(sh));
        }
    }

    void writeSectionData(std::ofstream& f) {
        for (auto const& s : sections_) {
            f.seekp(s.rawDataPointer); f.write((char*)s.data.data(), s.data.size());
            std::vector<char> pad(s.rawDataSize - s.data.size(), 0); f.write(pad.data(), pad.size());
        }
    }
};

PEGenerator::PEGenerator(bool i, uint64_t b) : pImpl_(std::make_unique<Impl>(i, b)) {}
PEGenerator::~PEGenerator() = default;
bool PEGenerator::generateFromCode(const std::map<std::string, std::vector<uint8_t>>& s, const std::vector<Symbol>& sy, const std::vector<Relocation>& r, const std::string& o) { return pImpl_->generateFromCode(s, sy, r, o); }
void PEGenerator::addSection(const std::string& n, const std::vector<uint8_t>& d, uint32_t v, uint32_t c) { pImpl_->addSection(n, d, v, c); }
void PEGenerator::addImport(const std::string& m, const std::string& f) { pImpl_->addImport(m, f); }
void PEGenerator::setMachine(uint16_t m) { pImpl_->setMachine(m); }
void PEGenerator::setBaseAddress(uint64_t a) { pImpl_->setBaseAddress(a); }
void PEGenerator::setPageSize(uint64_t s) { pImpl_->setPageSize(s); }
void PEGenerator::setSectionAlignment(uint32_t a) { pImpl_->setSectionAlignment(a); }
void PEGenerator::setFileAlignment(uint32_t a) { pImpl_->setFileAlignment(a); }
void PEGenerator::setEntryPoint(uint64_t a) { pImpl_->setEntryPoint(a); }
void PEGenerator::setSubsystem(uint16_t s) { pImpl_->setSubsystem(s); }
std::string PEGenerator::getLastError() const { return pImpl_->getLastError(); }
