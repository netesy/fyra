#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include "codegen/CodeGen.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace codegen {
namespace debug {

class DebugInfoManager;

// DWARF debug information generation
class DWARFGenerator {
public:
    struct DebugLine {
        unsigned line;
        unsigned column;
        std::string filename;
        uint64_t address;
        
        DebugLine() : line(0), column(0), address(0) {}
        DebugLine(unsigned l, unsigned c, const std::string& f, uint64_t addr)
            : line(l), column(c), filename(f), address(addr) {}
    };
    
    struct DebugVariable {
        std::string name;
        std::string type;
        uint64_t location; // Stack offset or register
        bool isParameter;
        unsigned scopeStart;
        unsigned scopeEnd;
        
        DebugVariable() : location(0), isParameter(false), scopeStart(0), scopeEnd(0) {}
    };
    
    struct DebugFunction {
        std::string name;
        std::string mangledName;
        uint64_t startAddress;
        uint64_t endAddress;
        std::vector<DebugVariable> variables;
        std::vector<DebugLine> lineTable;
        std::string filename;
        unsigned startLine;
        
        DebugFunction() : startAddress(0), endAddress(0), startLine(0) {}
    };
    
    struct DebugType {
        std::string name;
        uint64_t size;
        uint64_t align;
        std::string encoding; // "int", "float", "struct", etc.
        std::vector<std::pair<std::string, uint64_t>> members; // For structs
        
        DebugType() : size(0), align(0) {}
    };
    
    struct DebugCompileUnit {
        std::string filename;
        std::string producer;
        uint64_t language;
        std::vector<DebugFunction> functions;
        std::vector<DebugType> types;
        std::map<std::string, uint64_t> stringTable;
        
        DebugCompileUnit() : language(0) {}
    };

private:
    std::unique_ptr<DebugCompileUnit> compileUnit;
    uint64_t currentAddress;
    std::map<std::string, uint64_t> typeIDs;
    unsigned stringIDCounter;
    
public:
    DWARFGenerator();
    
    // Compile unit management
    void beginCompileUnit(const std::string& filename, const std::string& producer = "Fyra Compiler");
    void endCompileUnit();
    
    // Function debug info
    void beginFunction(const ir::Function& func, uint64_t startAddr);
    void endFunction(uint64_t endAddr);
    void addFunctionVariable(const std::string& name, const std::string& type, 
                           uint64_t location, bool isParameter = false);
    void setFunctionScope(unsigned start, unsigned end);
    
    // Line table generation
    void addLineInfo(unsigned line, unsigned column, const std::string& filename, uint64_t address);
    void generateLineTable(std::ostream& os);
    
    // Type information
    void addType(const DebugType& type);
    uint64_t getTypeID(const std::string& typeName);
    void generateTypeInformation(std::ostream& os);
    
    // Variable location tracking
    void trackVariableLocation(const std::string& varName, uint64_t address);
    void generateLocationLists(std::ostream& os);
    
    // Debug sections emission
    void generateDebugInfoSection(std::ostream& os);
    void generateDebugAbbrevSection(std::ostream& os);
    void generateDebugStringSection(std::ostream& os);
    void generateDebugFrameSection(std::ostream& os);
    
    // Stack unwinding information
    void generateExceptionTables(std::ostream& os);
    void addUnwindInfo(const std::string& funcName, uint64_t startAddr, uint64_t endAddr);
    
    // Utility methods
    uint64_t getStringID(const std::string& str);
    std::string escapeString(const std::string& str);
    void writeULEB128(std::ostream& os, uint64_t value);
    void writeSLEB128(std::ostream& os, int64_t value);
    void writeFixedLength(std::ostream& os, uint64_t value, unsigned bytes);
    
    // Integration with CodeGen
    void emitDebugInfoForFunction(CodeGen& cg, std::ostream& os, const ir::Function& func, DebugInfoManager& debugManager);
    void emitDebugDirectives(std::ostream& os);
    
private:
    void initializeStandardTypes();
    void generateFunctionDebugInfo(std::ostream& os, const DebugFunction& func);
    void generateVariableDebugInfo(std::ostream& os, const DebugVariable& var);
};

// Debug information manager
class DebugInfoManager {
private:
    std::unique_ptr<DWARFGenerator> dwarfGenerator;
    bool debugEnabled;
    std::string currentFile;
    unsigned currentLine;
    
public:
    DebugInfoManager();
    
    // Configuration
    void enableDebugInfo(bool enable = true) { debugEnabled = enable; }
    bool isDebugEnabled() const { return debugEnabled; }
    
    // Source location tracking
    void setCurrentLocation(const std::string& file, unsigned line) {
        currentFile = file;
        currentLine = line;
    }
    
    void setCurrentFile(const std::string& file) { currentFile = file; }
    void setCurrentLine(unsigned line) { currentLine = line; }
    const std::string& getCurrentFile() const { return currentFile; }
    unsigned getCurrentLine() const { return currentLine; }
    
    // Debug info generation
    void generateDebugInfo(CodeGen& cg, std::ostream& os, ir::Module& module);
    void emitFunctionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Function& func, uint64_t startAddr);
    void emitInstructionDebugInfo(CodeGen& cg, std::ostream& os, const ir::Instruction& instr, uint64_t address);
    
    // Integration points
    void beforeFunctionEmission(CodeGen& cg, std::ostream& os, const ir::Function& func);
    void afterFunctionEmission(CodeGen& cg, std::ostream& os, const ir::Function& func, uint64_t endAddr);
    void beforeInstructionEmission(CodeGen& cg, std::ostream& os, const ir::Instruction& instr, uint64_t address);
    
    // Access to DWARF generator
    DWARFGenerator* getDWARFGenerator() { return dwarfGenerator.get(); }
    const DWARFGenerator* getDWARFGenerator() const { return dwarfGenerator.get(); }
};

// Inline function information
class InlineInfoGenerator {
public:
    struct InlineSite {
        std::string calleeName;
        std::string callerName;
        unsigned callLine;
        unsigned callColumn;
        uint64_t startAddress;
        uint64_t endAddress;
    };
    
    // Inline function tracking
    void addInlineFunction(const std::string& callee, const std::string& caller,
                          unsigned line, unsigned column, uint64_t startAddr, uint64_t endAddr);
    void generateInlineInfo(std::ostream& os);
    
private:
    std::vector<InlineSite> inlineSites;
};

} // namespace debug
} // namespace codegen