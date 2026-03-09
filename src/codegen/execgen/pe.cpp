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

#pragma pack(push, 1)
struct DOSHeader {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
};

struct FileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct DataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct OptionalHeader32 {
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory dataDirectory[16];
};

struct OptionalHeader64 {
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory dataDirectory[16];
};

struct SectionHeader {
    char Name[8];
    union {
        uint32_t PhysicalAddress;
        uint32_t VirtualSize;
    } Misc;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct ImportDirectoryTable {
    uint32_t ImportLookupTableRVA;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t NameRVA;
    uint32_t ImportAddressTableRVA;
};

struct ImportLookupEntry64 {
    uint64_t Data;
};

struct ImportByName {
    uint16_t Hint;
    char Name[1];
};

struct BaseRelocationBlock {
    uint32_t VirtualAddress;  // RVA of the block
    uint32_t SizeOfBlock;     // Size of the block including this header
};

struct BaseRelocationEntry {
    uint16_t offset : 12;     // Offset within the page
    uint16_t type : 4;        // Relocation type
};

// Base relocation types
constexpr uint16_t IMAGE_REL_BASED_HIGHLOW = 3;
// IMAGE_REL_BASED_DIR64 is already defined in pe.hh namespace scope but let's use it from there if possible.
// Actually they were in anonymous namespace in pe.hh.

struct COFFSymbol {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};

#pragma pack(pop)

class PEGenerator::Impl {
public:
    Impl(bool is64Bit, uint64_t baseAddr)
        : is64Bit_(is64Bit)
        , baseAddress_(baseAddr != 0 ? baseAddr : (is64Bit ? 0x140000000ULL : DEFAULT_IMAGE_BASE_X86))
        , machine_(is64Bit ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386)
        , pageSize_(PAGE_SIZE)
        , sectionAlignment_(0x1000)
        , fileAlignment_(0x200)
        , entryPoint_(0) {}

    struct Section {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t virtualAddress = 0;
        uint32_t virtualSize = 0;
        uint32_t characteristics = 0;
        uint32_t rawDataPointer = 0;
        uint32_t rawDataSize = 0;
    };

    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections_in,
                          const std::vector<PEGenerator::Symbol>& symbols_in,
                          const std::vector<PEGenerator::Relocation>& relocations_in,
                          const std::string& outputPath) {
        try {
            sections_.clear();
            imports_.clear();

            // 1. Process incoming sections
            for (auto const& [name, data] : sections_in) {
                uint32_t chars = 0;
                if (name == ".text") chars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
                else if (name == ".data") chars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
                else if (name == ".rdata" || name == ".rodata") chars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

                Section s;
                s.name = (name == ".rodata") ? ".rdata" : name;
                s.data = data;
                s.virtualSize = data.size();
                s.characteristics = chars;
                sections_.push_back(std::move(s));
            }

            // 2. Ensure mandatory sections
            if (!findSection(".idata")) {
                addSection(".idata", {}, 0, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
            }
            if (!findSection(".reloc")) {
                addSection(".reloc", {}, 0, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE);
            }

            // 3. Ensure KERNEL32.dll ExitProcess import for compliance
            ensureKernel32Import();

            // 4. Calculate preliminary import directory size
            uint32_t importDirSize = calculateImportDirectorySize();
            Section* idata = findSection(".idata");
            if (idata) {
                idata->virtualSize = align(idata->data.size(), 16) + importDirSize;
            }

            // 5. Layout sections to determine RVAs
            layoutSections();

            // 6. Generate import directory with final RVAs
            setupImports();

            // 7. Process relocations (simplified)
            processRelocations(relocations_in, symbols_in);

            // 8. Generate base relocations
            generateBaseRelocations();

            // 9. Write the final file
            std::ofstream file(outputPath, std::ios::binary);
            if (!file) {
                lastError_ = "Cannot create output file: " + outputPath;
                return false;
            }

            writeDOSHeader(file);
            writeNTHeaders(file);
            writeSectionHeaders(file);
            writeSectionData(file);

            file.close();
            return true;
        } catch (const std::exception& e) {
            lastError_ = std::string("PE generation failed: ") + e.what();
            return false;
        }
    }

    void addSection(const std::string& name, const std::vector<uint8_t>& data,
                    uint32_t virtualSize, uint32_t characteristics) {
        Section section;
        section.name = name;
        section.data = data;
        section.characteristics = characteristics;
        section.virtualSize = virtualSize != 0 ? virtualSize : data.size();
        sections_.push_back(std::move(section));
    }

    void addImport(const std::string& moduleName, const std::string& functionName) {
        imports_[moduleName].push_back(functionName);
    }

    void setBaseAddress(uint64_t addr) { baseAddress_ = addr; }
    void setPageSize(uint64_t size) { pageSize_ = size; }
    void setSectionAlignment(uint32_t align) { sectionAlignment_ = align; }
    void setFileAlignment(uint32_t align) { fileAlignment_ = align; }
    void setEntryPoint(uint64_t addr) { entryPoint_ = addr; }
    void setSubsystem(uint16_t subsystem) { subsystem_ = subsystem; }
    void setMachine(uint16_t machine) { machine_ = machine; }
    std::string getLastError() const { return lastError_; }

private:
    bool is64Bit_;
    uint64_t baseAddress_;
    uint16_t machine_;
    uint64_t pageSize_;
    uint32_t sectionAlignment_;
    uint32_t fileAlignment_;
    uint64_t entryPoint_;
    uint16_t subsystem_ = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    std::string lastError_;

    std::vector<Section> sections_;
    std::unordered_map<std::string, std::vector<std::string>> imports_;
    uint32_t importDirectoryRVA_ = 0;

    Section* findSection(const std::string& name) {
        for(auto& s : sections_) {
            if(s.name == name) return &s;
        }
        return nullptr;
    }

    uint32_t align(uint32_t value, uint32_t alignment) {
        if (alignment == 0) return value;
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void ensureKernel32Import() {
        bool hasKernel32 = false;
        for (auto const& pair : imports_) {
            std::string name = pair.first;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name == "kernel32.dll") { hasKernel32 = true; break; }
        }
        if (!hasKernel32) {
            addImport("kernel32.dll", "ExitProcess");
        }
    }

    void layoutSections() {
        uint32_t headerSize = sizeof(DOSHeader) + DOS_STUB_SIZE + 4 + sizeof(FileHeader) +
                             (is64Bit_ ? sizeof(OptionalHeader64) : sizeof(OptionalHeader32)) + 
                             (sections_.size() * sizeof(SectionHeader));
        
        uint32_t currentRVA = align(headerSize, sectionAlignment_);
        uint32_t currentRawPtr = align(headerSize, fileAlignment_);

        for (auto& section : sections_) {
            section.virtualAddress = currentRVA;
            section.rawDataPointer = currentRawPtr;
            section.rawDataSize = align(section.data.size(), fileAlignment_);
            if (section.virtualSize == 0) section.virtualSize = section.data.size();
            
            currentRVA += align(section.virtualSize, sectionAlignment_);
            currentRawPtr += section.rawDataSize;
        }
    }

    uint32_t calculateImportDirectorySize() {
        if (imports_.empty()) return 0;
        uint32_t thunkSize = is64Bit_ ? 8 : 4;
        uint32_t idtSize = (imports_.size() + 1) * sizeof(ImportDirectoryTable);
        uint32_t totalIltSize = 0;
        uint32_t totalNamesSize = 0;

        for (auto const& pair : imports_) {
            totalIltSize += (pair.second.size() + 1) * thunkSize;
            totalNamesSize += pair.first.size() + 1;
            for (auto const& func : pair.second) {
                totalNamesSize += 2 + func.size() + 1;
                if (totalNamesSize % 2 != 0) totalNamesSize++;
            }
        }
        return idtSize + totalIltSize * 2 + totalNamesSize;
    }

    void setupImports() {
        if (imports_.empty()) return;
        Section* idata = findSection(".idata");
        if (!idata) return;

        uint32_t importDataOffset = align(idata->data.size(), 16);
        importDirectoryRVA_ = idata->virtualAddress + importDataOffset;
        
        std::vector<uint8_t> dirData = createImportDirectoryData();
        if (idata->data.size() < importDataOffset) idata->data.resize(importDataOffset, 0);
        idata->data.insert(idata->data.end(), dirData.begin(), dirData.end());
        idata->virtualSize = idata->data.size();
        idata->rawDataSize = align(idata->data.size(), fileAlignment_);
    }

    std::vector<uint8_t> createImportDirectoryData() {
        uint32_t thunkSize = is64Bit_ ? 8 : 4;
        uint32_t idtSize = (imports_.size() + 1) * sizeof(ImportDirectoryTable);
        
        uint32_t totalIltSize = 0;
        for (auto const& pair : imports_) totalIltSize += (pair.second.size() + 1) * thunkSize;

        uint32_t iltsOffset = idtSize;
        uint32_t iatsOffset = iltsOffset + totalIltSize;
        uint32_t namesOffset = iatsOffset + totalIltSize;

        std::vector<uint8_t> data(calculateImportDirectorySize(), 0);
        uint32_t currentIdt = 0;
        uint32_t currentIlt = iltsOffset;
        uint32_t currentIat = iatsOffset;
        uint32_t currentName = namesOffset;

        for (auto const& pair : imports_) {
            ImportDirectoryTable idt = {};
            idt.ImportLookupTableRVA = importDirectoryRVA_ + currentIlt;
            idt.ImportAddressTableRVA = importDirectoryRVA_ + currentIat;
            idt.NameRVA = importDirectoryRVA_ + currentName;
            
            memcpy(data.data() + currentIdt, &idt, sizeof(idt));
            currentIdt += sizeof(idt);

            memcpy(data.data() + currentName, pair.first.c_str(), pair.first.size() + 1);
            currentName += pair.first.size() + 1;

            for (auto const& func : pair.second) {
                uint32_t hintNameRva = importDirectoryRVA_ + currentName;
                uint16_t hint = 0;
                memcpy(data.data() + currentName, &hint, 2);
                memcpy(data.data() + currentName + 2, func.c_str(), func.size() + 1);
                currentName += 2 + func.size() + 1;
                if (currentName % 2 != 0) currentName++;

                if (is64Bit_) {
                    uint64_t rva64 = hintNameRva;
                    memcpy(data.data() + currentIlt, &rva64, 8);
                    memcpy(data.data() + currentIat, &rva64, 8);
                    currentIlt += 8; currentIat += 8;
                } else {
                    uint32_t rva32 = hintNameRva;
                    memcpy(data.data() + currentIlt, &rva32, 4);
                    memcpy(data.data() + currentIat, &rva32, 4);
                    currentIlt += 4; currentIat += 4;
                }
            }
            currentIlt += thunkSize;
            currentIat += thunkSize;
        }
        return data;
    }

    void processRelocations(const std::vector<PEGenerator::Relocation>& relocs,
                            const std::vector<PEGenerator::Symbol>& symbols) {
        Section* text = findSection(".text");
        if (!text) return;

        std::map<std::string, uint32_t> symbolRva;
        // In a real compiler, we would use the symbols to resolve addresses.
        // For this implementation, we'll focus on the structure.
    }

    void generateBaseRelocations() {
        Section* relocSec = findSection(".reloc");
        if (!relocSec) return;
        
        std::vector<uint8_t> data;
        BaseRelocationBlock block = {};
        block.VirtualAddress = 0; // Terminating block
        block.SizeOfBlock = sizeof(BaseRelocationBlock);
        
        data.resize(sizeof(block));
        memcpy(data.data(), &block, sizeof(block));
        
        relocSec->data = data;
        relocSec->virtualSize = data.size();
        relocSec->rawDataSize = align(data.size(), fileAlignment_);
    }

    void writeDOSHeader(std::ofstream& file) {
        DOSHeader h = {};
        h.e_magic = IMAGE_DOS_SIGNATURE;
        h.e_lfanew = sizeof(DOSHeader) + DOS_STUB_SIZE;
        file.write(reinterpret_cast<const char*>(&h), sizeof(h));
        
        const uint8_t dosStub[] = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68,
            0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
            0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20,
            0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        file.write(reinterpret_cast<const char*>(dosStub), sizeof(dosStub));
    }

    void writeNTHeaders(std::ofstream& file) {
        uint32_t sig = IMAGE_NT_SIGNATURE;
        file.write(reinterpret_cast<const char*>(&sig), 4);

        FileHeader fh = {};
        fh.Machine = machine_;
        fh.NumberOfSections = sections_.size();
        fh.TimeDateStamp = time(nullptr);
        fh.SizeOfOptionalHeader = is64Bit_ ? sizeof(OptionalHeader64) : sizeof(OptionalHeader32);
        fh.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
        file.write(reinterpret_cast<const char*>(&fh), sizeof(fh));

        if (is64Bit_) {
            OptionalHeader64 oh = {};
            oh.Magic = 0x20b;
            oh.AddressOfEntryPoint = findSection(".text") ? findSection(".text")->virtualAddress : 0;
            if (entryPoint_ != 0) oh.AddressOfEntryPoint = entryPoint_;
            oh.ImageBase = baseAddress_;
            oh.SectionAlignment = sectionAlignment_;
            oh.FileAlignment = fileAlignment_;
            oh.MajorOperatingSystemVersion = 6;
            oh.MajorSubsystemVersion = 6;
            oh.Subsystem = subsystem_;
            oh.DllCharacteristics = 0x8160; // DYNAMIC_BASE | NX_COMPAT | NO_SEH | TERMINAL_SERVER_AWARE
            oh.SizeOfStackReserve = 0x100000;
            oh.SizeOfStackCommit = 0x1000;
            oh.SizeOfHeapReserve = 0x100000;
            oh.SizeOfHeapCommit = 0x1000;
            oh.NumberOfRvaAndSizes = 16;
            
            Section* idata = findSection(".idata");
            if (idata) {
                oh.dataDirectory[1].VirtualAddress = importDirectoryRVA_;
                oh.dataDirectory[1].Size = calculateImportDirectorySize();
            }
            Section* reloc = findSection(".reloc");
            if (reloc) {
                oh.dataDirectory[5].VirtualAddress = reloc->virtualAddress;
                oh.dataDirectory[5].Size = reloc->virtualSize;
            }

            uint32_t sizeOfHeaders = align(sizeof(DOSHeader) + DOS_STUB_SIZE + 4 + sizeof(FileHeader) + sizeof(OptionalHeader64) + sections_.size() * sizeof(SectionHeader), fileAlignment_);
            oh.SizeOfHeaders = sizeOfHeaders;
            
            uint32_t sizeOfImage = sizeOfHeaders;
            for (auto const& s : sections_) sizeOfImage = std::max(sizeOfImage, s.virtualAddress + align(s.virtualSize, sectionAlignment_));
            oh.SizeOfImage = align(sizeOfImage, sectionAlignment_);

            file.write(reinterpret_cast<const char*>(&oh), sizeof(oh));
        } else {
            // 32-bit implementation similarly...
            OptionalHeader32 oh = {};
            oh.Magic = 0x10b;
            oh.ImageBase = (uint32_t)baseAddress_;
            oh.SectionAlignment = sectionAlignment_;
            oh.FileAlignment = fileAlignment_;
            oh.Subsystem = subsystem_;
            oh.NumberOfRvaAndSizes = 16;
            file.write(reinterpret_cast<const char*>(&oh), sizeof(oh));
        }
    }

    void writeSectionHeaders(std::ofstream& file) {
        for (auto const& s : sections_) {
            SectionHeader sh = {};
            memset(sh.Name, 0, 8);
            strncpy(sh.Name, s.name.c_str(), 8);
            sh.Misc.VirtualSize = s.virtualSize;
            sh.VirtualAddress = s.virtualAddress;
            sh.SizeOfRawData = s.rawDataSize;
            sh.PointerToRawData = s.rawDataPointer;
            sh.Characteristics = s.characteristics;
            file.write(reinterpret_cast<const char*>(&sh), sizeof(sh));
        }
    }

    void writeSectionData(std::ofstream& file) {
        for (auto const& s : sections_) {
            if (s.rawDataSize == 0) continue;
            file.seekp(s.rawDataPointer);
            if (!s.data.empty()) file.write(reinterpret_cast<const char*>(s.data.data()), s.data.size());
            
            uint32_t padding = s.rawDataSize - s.data.size();
            if (padding > 0) {
                std::vector<char> pad(padding, 0);
                file.write(pad.data(), padding);
            }
        }
    }
};

PEGenerator::PEGenerator(bool is64Bit, uint64_t baseAddr)
    : pImpl_(std::make_unique<Impl>(is64Bit, baseAddr)) {}

PEGenerator::~PEGenerator() = default;

bool PEGenerator::generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                                  const std::vector<Symbol>& symbols,
                                  const std::vector<Relocation>& relocations,
                                  const std::string& outputPath) {
    return pImpl_->generateFromCode(sections, symbols, relocations, outputPath);
}

void PEGenerator::addSection(const std::string& name, const std::vector<uint8_t>& data,
                             uint32_t virtualSize, uint32_t characteristics) {
    pImpl_->addSection(name, data, virtualSize, characteristics);
}

void PEGenerator::addImport(const std::string& moduleName, const std::string& functionName) {
    pImpl_->addImport(moduleName, functionName);
}

void PEGenerator::setMachine(uint16_t machine) { pImpl_->setMachine(machine); }
void PEGenerator::setBaseAddress(uint64_t addr) { pImpl_->setBaseAddress(addr); }
void PEGenerator::setPageSize(uint64_t size) { pImpl_->setPageSize(size); }
void PEGenerator::setSectionAlignment(uint32_t align) { pImpl_->setSectionAlignment(align); }
void PEGenerator::setFileAlignment(uint32_t align) { pImpl_->setFileAlignment(align); }
void PEGenerator::setEntryPoint(uint64_t addr) { pImpl_->setEntryPoint(addr); }
void PEGenerator::setSubsystem(uint16_t subsystem) { pImpl_->setSubsystem(subsystem); }
std::string PEGenerator::getLastError() const { return pImpl_->getLastError(); }
