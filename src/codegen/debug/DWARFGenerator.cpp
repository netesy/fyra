#include "codegen/debug/DWARFGenerator.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace codegen {
namespace debug {

// DWARFGenerator implementation
DWARFGenerator::DWARFGenerator() : currentAddress(0), stringIDCounter(1) {
    compileUnit = std::make_unique<DebugCompileUnit>();
    initializeStandardTypes();
}

void DWARFGenerator::beginCompileUnit(const std::string& filename, const std::string& producer) {
    compileUnit->filename = filename;
    compileUnit->producer = producer;
    compileUnit->language = 0x0004; // DW_LANG_C_plus_plus (C++)
    
    // Add common strings to string table
    getStringID(filename);
    getStringID(producer);
}

void DWARFGenerator::endCompileUnit() {
    // Finalize compile unit
}

void DWARFGenerator::beginFunction(const ir::Function& func, uint64_t startAddr) {
    DebugFunction debugFunc;
    debugFunc.name = func.getName();
    debugFunc.mangledName = func.getName(); // In a real implementation, this would be mangled
    debugFunc.startAddress = startAddr;
    debugFunc.filename = compileUnit->filename;
    
    // Add to current compile unit
    compileUnit->functions.push_back(debugFunc);
    
    currentAddress = startAddr;
}

void DWARFGenerator::endFunction(uint64_t endAddr) {
    if (!compileUnit->functions.empty()) {
        auto& func = compileUnit->functions.back();
        func.endAddress = endAddr;
    }
    
    currentAddress = endAddr;
}

void DWARFGenerator::addFunctionVariable(const std::string& name, const std::string& type, 
                                       uint64_t location, bool isParameter) {
    if (!compileUnit->functions.empty()) {
        DebugVariable var;
        var.name = name;
        var.type = type;
        var.location = location;
        var.isParameter = isParameter;
        
        auto& func = compileUnit->functions.back();
        func.variables.push_back(var);
        
        // Add strings to string table
        getStringID(name);
        getStringID(type);
    }
}

void DWARFGenerator::setFunctionScope(unsigned start, unsigned end) {
    if (!compileUnit->functions.empty()) {
        auto& func = compileUnit->functions.back();
        // This would set scope information for the last added variable
        if (!func.variables.empty()) {
            auto& var = func.variables.back();
            var.scopeStart = start;
            var.scopeEnd = end;
        }
    }
}

void DWARFGenerator::addLineInfo(unsigned line, unsigned column, const std::string& filename, uint64_t address) {
    if (!compileUnit->functions.empty()) {
        DebugLine debugLine(line, column, filename, address);
        auto& func = compileUnit->functions.back();
        func.lineTable.push_back(debugLine);
        
        // Add filename to string table
        getStringID(filename);
    }
}

void DWARFGenerator::generateLineTable(std::ostream& os) {
    os << "  # DWARF Line Table (simplified)\n";
    os << "  .section .debug_line\n";
    
    for (const auto& func : compileUnit->functions) {
        os << "  # Function: " << func.name << "\n";
        for (const auto& line : func.lineTable) {
            os << "  # Line " << line.line << ":" << line.column 
               << " at address 0x" << std::hex << line.address << std::dec << "\n";
        }
    }
}

void DWARFGenerator::addType(const DebugType& type) {
    compileUnit->types.push_back(type);
    getStringID(type.name);
    
    // Add member names to string table for struct types
    for (const auto& member : type.members) {
        getStringID(member.first);
    }
}

uint64_t DWARFGenerator::getTypeID(const std::string& typeName) {
    auto it = typeIDs.find(typeName);
    if (it != typeIDs.end()) {
        return it->second;
    }
    
    // Assign new ID
    uint64_t id = typeIDs.size() + 1;
    typeIDs[typeName] = id;
    return id;
}

void DWARFGenerator::generateTypeInformation(std::ostream& os) {
    os << "  # DWARF Type Information (simplified)\n";
    os << "  .section .debug_info\n";
    
    for (const auto& type : compileUnit->types) {
        os << "  # Type: " << type.name << " (size=" << type.size 
           << ", align=" << type.align << ")\n";
        
        if (!type.members.empty()) {
            os << "  # Members:\n";
            for (const auto& member : type.members) {
                os << "  #   " << member.first << " at offset " << member.second << "\n";
            }
        }
    }
}

void DWARFGenerator::trackVariableLocation(const std::string& varName, uint64_t address) {
    // In a real implementation, this would track location lists
    // For now, we just acknowledge the tracking
}

void DWARFGenerator::generateLocationLists(std::ostream& os) {
    os << "  # DWARF Location Lists (simplified)\n";
    os << "  .section .debug_loc\n";
    os << "  # Location tracking information\n";
}

void DWARFGenerator::generateDebugInfoSection(std::ostream& os) {
    os << "\n  # DWARF Debug Information Section\n";
    os << "  .section .debug_info\n";
    
    // Compile unit header
    os << "  # Compile Unit: " << compileUnit->filename << "\n";
    os << "  # Producer: " << compileUnit->producer << "\n";
    os << "  # Language: " << compileUnit->language << "\n";
    
    // Function information
    for (const auto& func : compileUnit->functions) {
        generateFunctionDebugInfo(os, func);
    }
}

void DWARFGenerator::generateDebugAbbrevSection(std::ostream& os) {
    os << "\n  # DWARF Debug Abbreviation Section\n";
    os << "  .section .debug_abbrev\n";
    os << "  # Abbreviation table for debug info entries\n";
}

void DWARFGenerator::generateDebugStringSection(std::ostream& os) {
    os << "\n  # DWARF String Section\n";
    os << "  .section .debug_str\n";
    
    for (const auto& pair : compileUnit->stringTable) {
        os << "  # String ID " << pair.second << ": \"" << pair.first << "\"\n";
    }
}

void DWARFGenerator::generateDebugFrameSection(std::ostream& os) {
    os << "\n  # DWARF Call Frame Information\n";
    os << "  .section .debug_frame\n";
    os << "  # Call frame information for stack unwinding\n";
}

void DWARFGenerator::generateExceptionTables(std::ostream& os) {
    os << "\n  # Exception Handling Tables\n";
    os << "  .section .eh_frame\n";
    os << "  # Exception frame information\n";
}

void DWARFGenerator::addUnwindInfo(const std::string& funcName, uint64_t startAddr, uint64_t endAddr) {
    // Add stack unwinding information for exception handling
    // This is a placeholder for actual implementation
}

uint64_t DWARFGenerator::getStringID(const std::string& str) {
    auto it = compileUnit->stringTable.find(str);
    if (it != compileUnit->stringTable.end()) {
        return it->second;
    }
    
    uint64_t id = stringIDCounter++;
    compileUnit->stringTable[str] = id;
    return id;
}

std::string DWARFGenerator::escapeString(const std::string& str) {
    std::stringstream escaped;
    for (char c : str) {
        if (c == '\\' || c == '"') {
            escaped << '\\' << c;
        } else if (c >= 32 && c <= 126) {
            escaped << c;
        } else {
            escaped << "\\x" << std::hex << std::setw(2) << std::setfill('0') 
                   << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
        }
    }
    return escaped.str();
}

void DWARFGenerator::writeULEB128(std::ostream& os, uint64_t value) {
    // Write unsigned LEB128 encoded value
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        os << "\\x" << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(byte) << std::dec;
    } while (value != 0);
}

void DWARFGenerator::writeSLEB128(std::ostream& os, int64_t value) {
    // Write signed LEB128 encoded value
    bool more = true;
    bool negative = (value < 0);
    int size = sizeof(int64_t) * 8;
    
    while (more) {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        
        if (negative) {
            // Sign extend
            value |= -(1LL << (size - 7));
        }
        
        if ((value == 0 && (byte & 0x40) == 0) || 
            (value == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        
        os << "\\x" << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(byte) << std::dec;
    }
}

void DWARFGenerator::writeFixedLength(std::ostream& os, uint64_t value, unsigned bytes) {
    // Write fixed-length value in little-endian format
    for (unsigned i = 0; i < bytes; ++i) {
        uint8_t byte = (value >> (i * 8)) & 0xFF;
        os << "\\x" << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(byte) << std::dec;
    }
}

void DWARFGenerator::emitDebugInfoForFunction(CodeGen& cg, std::ostream& os, const ir::Function& func, DebugInfoManager& debugManager) {
    os << "  # Debug info for function: " << func.getName() << "\n";
    
    // Emit function start debug info
    os << "  .loc 1 " << debugManager.getCurrentLine() << " 0\n";
}

void DWARFGenerator::emitDebugDirectives(std::ostream& os) {
    os << "  .file 1 \"" << compileUnit->filename << "\"\n";
}

// Private helper methods
void DWARFGenerator::initializeStandardTypes() {
    // Initialize common types
    DebugType intType;
    intType.name = "int";
    intType.size = 4;
    intType.align = 4;
    intType.encoding = "int";
    addType(intType);
    
    DebugType floatType;
    floatType.name = "float";
    floatType.size = 4;
    floatType.align = 4;
    floatType.encoding = "float";
    addType(floatType);
    
    DebugType doubleType;
    doubleType.name = "double";
    doubleType.size = 8;
    doubleType.align = 8;
    doubleType.encoding = "float";
    addType(doubleType);
    
    DebugType voidType;
    voidType.name = "void";
    voidType.size = 0;
    voidType.align = 1;
    voidType.encoding = "void";
    addType(voidType);
}

void DWARFGenerator::generateFunctionDebugInfo(std::ostream& os, const DebugFunction& func) {
    os << "  # Function: " << func.name << "\n";
    os << "  #   Address range: 0x" << std::hex << func.startAddress 
       << " - 0x" << func.endAddress << std::dec << "\n";
    os << "  #   File: " << func.filename << ":" << func.startLine << "\n";
    
    // Variable information
    for (const auto& var : func.variables) {
        generateVariableDebugInfo(os, var);
    }
    
    // Line number information
    if (!func.lineTable.empty()) {
        os << "  #   Line table entries: " << func.lineTable.size() << "\n";
        for (const auto& line : func.lineTable) {
            os << "  #     Line " << line.line << ":" << line.column 
               << " at 0x" << std::hex << line.address << std::dec << "\n";
        }
    }
}

void DWARFGenerator::generateVariableDebugInfo(std::ostream& os, const DebugVariable& var) {
    os << "  #   Variable: " << var.name << " (" << var.type << ")\n";
    os << "  #     Location: " << (var.isParameter ? "parameter" : "local") << "\n";
    if (var.location != 0) {
        os << "  #     Address: 0x" << std::hex << var.location << std::dec << "\n";
    }
    if (var.scopeStart != 0 || var.scopeEnd != 0) {
        os << "  #     Scope: " << var.scopeStart << " - " << var.scopeEnd << "\n";
    }
}

} // namespace debug
} // namespace codegen