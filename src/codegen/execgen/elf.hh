#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>

/**
 * @class ElfGenerator
 * @brief Generates 64-bit ELF files (executables or relocatable objects) from assembly source.
 *
 * This class encapsulates the logic for converting an assembly file (.s) into a final
 * ELF binary. It works by invoking the system assembler (as) to create a temporary
 * object file, then parsing that object file to reconstruct a final executable or
 * relocatable file with the correct headers and layout.
 */
class ElfGenerator {
public:
    /**
     * @brief Constructs an ElfGenerator.
     * @param inputFilename The original source file name (e.g., "test.fy"). This is used for metadata in the ELF file.
     * @param is64Bit Currently only 64-bit is supported. Defaults to true.
     * @param baseAddress The virtual address for the start of the text segment in executables.
     */
    explicit ElfGenerator(const std::string& inputFilename, bool is64Bit = true, uint64_t baseAddress = 0x400000);
    ~ElfGenerator();

    /**
     * @brief The main generation function.
     * @param assemblyPath Path to the input assembly file (.s).
     * @param outputPath Path where the final ELF file will be written.
     * @param generateRelocatable If true, generates a relocatable object file (.o). If false, generates an executable.
     * @return True on success, false on failure.
     */
    bool generate(const std::string& assemblyPath, const std::string& outputPath, bool generateRelocatable = false);

    struct Symbol {
        std::string name;
        uint64_t value = 0;
        uint64_t size = 0;
        uint8_t type = 0; // STT_NOTYPE
        uint8_t binding = 1; // STB_GLOBAL
        std::string sectionName;
    };

    struct Relocation {
        uint64_t offset;
        std::string type; // e.g., "R_X86_64_PC32"
        int64_t addend;
        std::string symbolName;
        std::string sectionName;
    };

    /**
     * @brief Generates an ELF executable directly from in-memory machine code sections.
     * @param sections A map where keys are section names (e.g., ".text", ".rodata")
     *                 and values are the corresponding byte vectors of machine code.
     * @param symbols A vector of symbol definitions.
     * @param relocations A vector of relocation entries.
     * @param outputPath Path where the final ELF file will be written.
     * @return True on success, false on failure.
     */
    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                          const std::vector<Symbol>& symbols,
                          const std::vector<Relocation>& relocations,
                          const std::string& outputPath);

    // --- Configuration for Executable Generation ---

    /**
     * @brief Sets the base virtual address for the text segment.
     * @param address The virtual address.
     */
    void setBaseAddress(uint64_t address);

    /**
     * @brief Sets the page size for segment alignment.
     * @param size The page size, typically 0x1000.
     */
    void setPageSize(uint64_t size);

    /**
     * @brief Sets the name of the symbol to be used as the entry point.
     * @param name The name of the entry point symbol (e.g., "_start").
     */
    void setEntryPointName(const std::string& name);

    /**
     * @brief Sets the target machine type.
     * @param machine The ELF machine constant (e.g., 62 for x86-64, 183 for AArch64).
     */
    void setMachine(uint16_t machine);

    /**
     * @brief Retrieves the last error message.
     * @return A string containing the last error, or an empty string if no error occurred.
     */
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // Disable copy and move
    ElfGenerator(const ElfGenerator&) = delete;
    ElfGenerator& operator=(const ElfGenerator&) = delete;
    ElfGenerator(ElfGenerator&&) = delete;
    ElfGenerator& operator=(ElfGenerator&&) = delete;
};