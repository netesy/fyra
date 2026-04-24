#ifndef EXECGEN_ASSEMBLER_HH
#define EXECGEN_ASSEMBLER_HH

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

// This header serves as a central place for types used in the
// executable generation process.

// The Section enum is used to identify different parts of the binary.
enum class Section {
    NONE,
    TEXT,
    DATA,
    BSS,
    RODATA
};

// Symbol-related enums.
enum class SymbolBinding {
    LOCAL,
    GLOBAL,
    WEAK
};

enum class SymbolType {
    NOTYPE,
    OBJECT,
    FUNCTION,
    SECTION,
    FILE
};

enum class SymbolVisibility {
    DEFAULT,
    HIDDEN,
    PROTECTED
};

// The SymbolEntry struct represents a symbol in the symbol table.
struct SymbolEntry {
    std::string name;
    uint64_t address = 0;
    uint64_t size = 0;
    SymbolBinding binding = SymbolBinding::LOCAL;
    SymbolType type = SymbolType::NOTYPE;
    SymbolVisibility visibility = SymbolVisibility::DEFAULT;
    Section section = Section::NONE;
    bool isDefined = false;
};

// Relocation-related enums and structs.
enum class RelocationType {
    R_X86_64_64,
    R_X86_64_PC32,
    R_X86_64_PLT32
};

struct RelocationEntry {
    std::string symbolName;
    uint64_t offset = 0;
    RelocationType type = RelocationType::R_X86_64_PC32;
    int64_t addend = 0;
    Section section = Section::TEXT;
};

#endif // EXECGEN_ASSEMBLER_HH