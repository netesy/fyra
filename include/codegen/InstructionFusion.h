#pragma once

#include "ir/Instruction.h"
#include "ir/SIMDInstruction.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
namespace codegen { class CodeGen; }

#include <vector>
#include <map>
#include <functional>

namespace codegen {
namespace target {

// Advanced addressing mode patterns
struct AddressingMode {
    enum Type {
        Simple,          // [reg]
        Offset,          // [reg + offset]
        PreIndex,        // [reg + offset]!
        PostIndex,       // [reg], offset
        ScaleIndex,      // [base + index*scale]
        ScaleIndexOffset // [base + index*scale + offset]
    };
    
    Type type;
    std::string baseReg;
    std::string indexReg;
    int64_t offset;
    unsigned scale;
    
    AddressingMode() : type(Simple), offset(0), scale(1) {}
    
    std::string toString() const {
        switch (type) {
            case Simple: return "[" + baseReg + "]";
            case Offset: return "[" + baseReg + ", #" + std::to_string(offset) + "]";
            case PreIndex: return "[" + baseReg + ", #" + std::to_string(offset) + "]!";
            case PostIndex: return "[" + baseReg + "], #" + std::to_string(offset);
            case ScaleIndex: return "[" + baseReg + ", " + indexReg + ", lsl #" + std::to_string(scale) + "]";
            case ScaleIndexOffset: return "[" + baseReg + ", " + indexReg + ", lsl #" + std::to_string(scale) + ", #" + std::to_string(offset) + "]";
            default: return "[" + baseReg + "]";
        }
    }
};

// Instruction fusion patterns
class InstructionFusion {
public:
    struct FusionPattern {
        std::string name;
        std::vector<ir::Instruction::Opcode> opcodes;
        std::function<bool(const std::vector<ir::Instruction*>&)> matcher;
        std::function<void(codegen::CodeGen&, const std::vector<ir::Instruction*>&)> emitter;
        double benefit; // Performance benefit estimate
    };
    
    struct FusionCandidate {
        std::vector<ir::Instruction*> instructions;
        FusionPattern* pattern;
        double estimatedBenefit;
        
        bool operator<(const FusionCandidate& other) const {
            return estimatedBenefit > other.estimatedBenefit; // Higher benefit first
        }
    };
    
private:
    std::vector<FusionPattern> patterns;
    
public:
    InstructionFusion();
    
    // Pattern registration
    void registerPattern(const FusionPattern& pattern);
    void registerDefaultPatterns();
    
    // Pattern matching and analysis
    std::vector<FusionCandidate> findFusionOpportunities(ir::BasicBlock& block);
    bool canFuseInstructions(const std::vector<ir::Instruction*>& instructions);
    double estimateFusionBenefit(const std::vector<ir::Instruction*>& instructions, const FusionPattern& pattern);
    
    // Fusion application
    void applyFusion(codegen::CodeGen& cg, const FusionCandidate& candidate);
    void optimizeBasicBlock(ir::BasicBlock& block);
    
    // Pattern matchers
    static bool isMultiplyAddPattern(const std::vector<ir::Instruction*>& instructions);
    static bool isLoadOperatePattern(const std::vector<ir::Instruction*>& instructions);
    static bool isCompareAndBranchPattern(const std::vector<ir::Instruction*>& instructions);
    static bool isAddressCalculationPattern(const std::vector<ir::Instruction*>& instructions);
    
    // Pattern emitters
    static void emitFusedMultiplyAdd(codegen::CodeGen& cg, const std::vector<ir::Instruction*>& instructions);
    static void emitLoadAndOperate(codegen::CodeGen& cg, const std::vector<ir::Instruction*>& instructions);
    static void emitCompareAndBranch(codegen::CodeGen& cg, const std::vector<ir::Instruction*>& instructions);
    static void emitComplexAddressing(codegen::CodeGen& cg, const std::vector<ir::Instruction*>& instructions);
};

// Complex addressing mode analysis
class AddressingModeAnalyzer {
public:
    struct AddressExpression {
        ir::Value* base;
        ir::Value* index;
        int64_t offset;
        unsigned scale;
        bool isValid;
        
        AddressExpression() : base(nullptr), index(nullptr), offset(0), scale(1), isValid(false) {}
    };
    
    // Analyze address calculation patterns
    static AddressExpression analyzeAddress(ir::Instruction* addrInst);
    static bool canOptimizeAddressing(ir::Instruction* loadStore);
    static AddressingMode generateAddressingMode(const AddressExpression& expr, const std::string& targetArch);
    
    // Pattern detection
    static bool isArrayAccess(ir::Instruction* inst);
    static bool isStructAccess(ir::Instruction* inst);
    static bool hasConstantOffset(ir::Instruction* inst, int64_t& offset);
    static bool hasScaledIndex(ir::Instruction* inst, unsigned& scale);
    
    // Optimization opportunities
    static std::vector<ir::Instruction*> findAddressingOptimizations(ir::BasicBlock& block);
    static double estimateAddressingBenefit(ir::Instruction* inst);
};

// FMA (Fused Multiply-Add) detection and optimization
class FMAOptimizer {
public:
    struct FMAPattern {
        ir::Instruction* multiply;
        ir::Instruction* add;
        ir::Value* multiplicand1;
        ir::Value* multiplicand2;
        ir::Value* addend;
        bool isSubtract;
        bool isNegated;
    };
    
    // FMA pattern detection
    static std::vector<FMAPattern> findFMAPatterns(ir::BasicBlock& block);
    static bool canFormFMA(ir::Instruction* mul, ir::Instruction* add);
    static bool isValidFMAOperands(ir::Value* a, ir::Value* b, ir::Value* c);
    
    // FMA transformation
    static ir::FusedInstruction* createFMAInstruction(const FMAPattern& pattern);
    static void replaceFMAPattern(ir::BasicBlock& block, const FMAPattern& pattern);
    
    // Cost analysis
    static double estimateFMABenefit(const FMAPattern& pattern);
    static bool isProfitableToFuse(const FMAPattern& pattern);
    
    // Target-specific FMA support
    static bool targetSupportsFMA(const std::string& targetArch);
    static std::string getFMAInstruction(const FMAPattern& pattern, const std::string& targetArch);
};

// Load-operate fusion for memory-to-register operations
class LoadOperateFusion {
public:
    struct LoadOperatePattern {
        ir::Instruction* load;
        ir::Instruction* operate;
        bool canFuseWithMemory;
        AddressingMode addressing;
    };
    
    // Pattern detection
    static std::vector<LoadOperatePattern> findLoadOperatePatterns(ir::BasicBlock& block);
    static bool canFuseLoadWithOperation(ir::Instruction* load, ir::Instruction* op);
    static bool supportsMemoryOperand(ir::Instruction::Opcode opcode);
    
    // Fusion application
    static void applyLoadOperateFusion(ir::BasicBlock& block, const LoadOperatePattern& pattern);
    static std::string generateFusedInstruction(const LoadOperatePattern& pattern, const std::string& targetArch);
    
    // Benefits analysis
    static double estimateLoadOperateBenefit(const LoadOperatePattern& pattern);
    static bool isWorthFusing(const LoadOperatePattern& pattern);
};

// Compare-and-branch fusion for control flow optimization
class CompareAndBranchFusion {
public:
    struct CompareAndBranchPattern {
        ir::Instruction* compare;
        ir::Instruction* branch;
        std::string condition;
        ir::Value* lhs;
        ir::Value* rhs;
        std::string trueLabel;
        std::string falseLabel;
    };
    
    // Pattern detection
    static std::vector<CompareAndBranchPattern> findCompareAndBranchPatterns(ir::BasicBlock& block);
    static bool canFuseCompareWithBranch(ir::Instruction* cmp, ir::Instruction* br);
    static std::string extractCondition(ir::Instruction* cmp);
    
    // Fusion application
    static void applyCompareAndBranchFusion(ir::BasicBlock& block, const CompareAndBranchPattern& pattern);
    static std::string generateFusedBranch(const CompareAndBranchPattern& pattern, const std::string& targetArch);
    
    // Optimization analysis
    static double estimateCompareAndBranchBenefit(const CompareAndBranchPattern& pattern);
    static bool shouldFuseCompareAndBranch(const CompareAndBranchPattern& pattern);
};

// Master fusion coordinator
class FusionCoordinator {
private:
    InstructionFusion instructionFusion;
    FMAOptimizer fmaOptimizer;
    LoadOperateFusion loadOperateFusion;
    CompareAndBranchFusion compareAndBranchFusion;
    AddressingModeAnalyzer addressingAnalyzer;
    
public:
    FusionCoordinator();
    
    // High-level optimization interface
    void optimizeFunction(ir::Function& func);
    void optimizeBasicBlock(ir::BasicBlock& block);
    
    // Analysis and reporting
    struct OptimizationReport {
        unsigned fmaOptimizations;
        unsigned loadOperateOptimizations;
        unsigned compareAndBranchOptimizations;
        unsigned addressingOptimizations;
        double estimatedSpeedup;
        std::vector<std::string> appliedOptimizations;
    };
    
    OptimizationReport generateReport(ir::Function& func);
    void printOptimizationSummary(const OptimizationReport& report, std::ostream& os);
    
    // Configuration
    void enableOptimization(const std::string& optimizationName, bool enable);
    void setTargetArchitecture(const std::string& arch);
    void setOptimizationLevel(unsigned level); // 0=none, 1=basic, 2=aggressive
    
private:
    std::map<std::string, bool> enabledOptimizations;
    std::string targetArchitecture;
    unsigned optimizationLevel;
};

} // namespace target
} // namespace codegen