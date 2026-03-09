#pragma once

#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/FunctionType.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "codegen/target/TargetInfo.h"
#include "codegen/execgen/Assembler.h"
#include <ostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace codegen {
namespace target {
class FusionCoordinator;
}
namespace debug {
class DebugInfoManager;
}
namespace execgen {
class Assembler;
}

// Instruction pattern matching for enhanced code generation
struct InstructionPattern {
    ir::Instruction::Opcode opcode;
    std::vector<ir::Type::TypeID> operandTypes;
    std::function<void(CodeGen&, ir::Instruction&)> emitter;
};

class CodeGen {
public:
    CodeGen(ir::Module& module, std::unique_ptr<target::TargetInfo> targetInfo, std::ostream* os = nullptr);
    ~CodeGen();

    void emit(bool forExecutable = false);

    // Access to assembler and output stream
    execgen::Assembler& getAssembler() { return *assembler; }
    execgen::Assembler& getRodataAssembler() { return *rodataAssembler; }
    std::ostream* getTextStream() { return os; }
    void setStream(std::ostream* s) { os = s; }

    // Helper to get the assembly representation of a value (e.g., register or immediate)
    std::string getValueAsOperand(const ir::Value* value);
    
    // WASM-specific value resolution
    std::string getWasmValueAsOperand(const ir::Value* value);
    
    // WASM-specific function signature emission
    void emitWasmFunctionSignature(ir::Function& func);
    
    // Enhanced instruction selection
    void selectInstruction(ir::Instruction& instr);
    bool matchPattern(ir::Instruction& instr, const InstructionPattern& pattern);
    
    // Target-specific optimizations
    void performTargetOptimizations(ir::Function& func);
    
    // Register allocation and management
    std::string allocateRegister(ir::Value* value);
    void deallocateRegister(const std::string& reg);
    
    // Enhanced assembly emission
    void emitTargetSpecificHeader();
    void emitDataSection();
    void emitTextSection();
    void emitFunctionAlignment();
    
    // Instruction fusion and optimization
    void performInstructionFusion(ir::Function& func);
    void applyFusionOptimizations(ir::BasicBlock& block);
    
    // Debug information generation
    void enableDebugInfo(bool enable = true);
    void setCurrentLocation(const std::string& file, unsigned line);
    void emitDebugInfo();
    
    std::map<ir::Value*, int>& getStackOffsets() { return stackOffsets; }
    const std::map<ir::Value*, int>& getStackOffsets() const { return stackOffsets; }
    int32_t getStackOffset(ir::Value* val) const;
    const std::map<ir::ConstantFP*, std::string>& getFloatConstantLabels() const { return floatConstantLabels; }
    target::TargetInfo* getTargetInfo() { return targetInfo.get(); }
    const target::TargetInfo* getTargetInfo() const { return targetInfo.get(); }
    const std::vector<std::vector<uint8_t>>& getWasmFunctionBodies() const { return wasmFunctionBodies; }
    const std::map<const ir::FunctionType*, uint32_t>& getWasmTypeIndices() const { return wasmTypeIndices; }
    const std::map<const ir::Function*, uint32_t>& getWasmFunctionIndices() const { return wasmFunctionIndices; }
    const std::map<const ir::Value*, uint32_t>& getWasmLocalIndices() const { return wasmLocalIndices; }

public:
    void emitFunction(ir::Function& func);
    void emitBasicBlock(ir::BasicBlock& bb);
    void emitBasicBlockContent(ir::BasicBlock& bb);
    void emitWasmStructuredFunction(ir::Function& func);
    void emitWasmBasicBlockStructured(ir::BasicBlock& bb, std::map<std::string, ir::BasicBlock*>& blockMap);
    void emitFibonacciStructured(ir::Function& func);
    void emitInstruction(ir::Instruction& instr);
    
    // Enhanced instruction selection methods
    void initializeInstructionPatterns();
    void selectArithmeticInstruction(ir::Instruction& instr);
    void selectMemoryInstruction(ir::Instruction& instr);
    void selectComparisonInstruction(ir::Instruction& instr);
    void selectControlFlowInstruction(ir::Instruction& instr);
    
    // Pattern matching helpers
    bool isImmediateValue(ir::Value* value, int64_t& immediate);
    bool canFoldIntoMemoryOperand(ir::Value* value);
    std::string getOptimizedOperand(ir::Value* value);

public:
    ir::Module& module;
    std::unique_ptr<target::TargetInfo> targetInfo;
    std::unique_ptr<execgen::Assembler> assembler;
    std::unique_ptr<execgen::Assembler> rodataAssembler;
    std::ostream* os; // Optional text output stream
    
    // Instruction patterns for enhanced selection
    std::vector<InstructionPattern> instructionPatterns;
    
    // Register allocation state
    std::map<std::string, bool> registerUsage;
    std::vector<std::string> availableRegisters;

    // For stack-based allocation of virtual registers
    std::map<ir::Value*, int> stackOffsets;

    // For floating point constants
    std::map<ir::ConstantFP*, std::string> floatConstantLabels;
    
    // Enhanced code generation state
    bool emittedHeader = false;
    bool emittedDataSection = false;
    bool emittedTextSection = false;
    int labelCounter = 0;
    
    // Instruction fusion coordinator
    std::unique_ptr<target::FusionCoordinator> fusionCoordinator;
    
    // Debug information manager
    std::unique_ptr<debug::DebugInfoManager> debugInfoManager;
    
    // Current function being processed (for WASM parameter resolution)
    ir::Function* currentFunction = nullptr;

    // Usage tracking for conditional emission
    bool usesHeap = false;
    bool usesFPNeg = false;

    // Symbol and relocation info for in-memory generation
    struct SymbolInfo {
        std::string name;
        uint64_t value = 0;
        uint64_t size = 0;
        uint8_t type = 0; // STT_NOTYPE
        uint8_t binding = 1; // STB_GLOBAL
        std::string sectionName;
    };

    struct RelocationInfo {
        uint64_t offset;
        std::string type; // e.g., "R_X86_64_PC32"
        int64_t addend;
        std::string symbolName;
        std::string sectionName;
    };

    void addRelocation(const RelocationInfo& reloc) { relocations.push_back(reloc); }
    void addSymbol(const SymbolInfo& sym) { symbols.push_back(sym); }
    const std::vector<SymbolInfo>& getSymbols() const { return symbols; }
    const std::vector<RelocationInfo>& getRelocations() const { return relocations; }

private:
    std::vector<SymbolInfo> symbols;
    std::vector<RelocationInfo> relocations;
    std::vector<std::vector<uint8_t>> wasmFunctionBodies;
    std::map<const ir::FunctionType*, uint32_t> wasmTypeIndices;
    std::map<const ir::Function*, uint32_t> wasmFunctionIndices;
    std::map<const ir::Value*, uint32_t> wasmLocalIndices;
};

} // namespace codegen
