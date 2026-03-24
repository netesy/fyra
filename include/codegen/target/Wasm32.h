#pragma once

#include "TargetInfo.h"
#include <set>

namespace transforms { class DominatorTree; }

namespace codegen {
namespace target {

class Wasm32 : public TargetInfo {
public:
    // Core TargetInfo methods
    std::string getName() const override;
    size_t getPointerSize() const override;
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    
    // Register information
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    
    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    
    // Enhanced calling convention support
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;
    
    // Argument passing
    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(CodeGen& cg, size_t argIndex,
                        const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex,
                       const std::string& dest, const ir::Type* type) override;

    // Instruction emission - existing methods
    void emitRet(CodeGen& cg, ir::Instruction& instr) override;
    void emitAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitDiv(CodeGen& cg, ir::Instruction& instr) override;
    void emitRem(CodeGen& cg, ir::Instruction& instr) override;
    void emitAnd(CodeGen& cg, ir::Instruction& instr) override;
    void emitOr(CodeGen& cg, ir::Instruction& instr) override;
    void emitXor(CodeGen& cg, ir::Instruction& instr) override;
    void emitShl(CodeGen& cg, ir::Instruction& instr) override;
    void emitShr(CodeGen& cg, ir::Instruction& instr) override;
    void emitSar(CodeGen& cg, ir::Instruction& instr) override;
    void emitNeg(CodeGen& cg, ir::Instruction& instr) override;
    void emitCopy(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;
    
    // New instruction emission methods
    void emitNot(CodeGen& cg, ir::Instruction& instr) override;

    // Floating point operations
    void emitFAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitFSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitFMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& instr) override;

    void emitCmp(CodeGen& cg, ir::Instruction& instr) override;
    void emitCast(CodeGen& cg, ir::Instruction& instr,
                 const ir::Type* fromType, const ir::Type* toType) override;
    void emitVAStart(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAEnd(CodeGen& cg, ir::Instruction& instr) override;
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;

    // SIMD/Vector Support
    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned width) const override;
    bool supportsVectorType(const ir::VectorType* type) const override;
    unsigned getOptimalVectorWidth(const ir::Type* elementType) const override;
    void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) override {
        (void)cg; (void)instr;
    }
    void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) override;

    // Fused Instruction Support
    bool supportsFusedPattern(FusedPattern pattern) const override;
    
    // Register classification
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
    std::string formatConstant(const ir::ConstantInt* C) const override;

    // File Extensions
    std::string getAssemblyFileExtension() const override { return ".wat"; }

    // Wasm binary emission
    void emitHeader(CodeGen& cg);
    void emitTypeSection(CodeGen& cg);
    void emitFunctionSection(CodeGen& cg);
    void emitExportSection(CodeGen& cg);
    void emitCodeSection(CodeGen& cg);
    void emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) override;
    
private:
    std::set<ir::BasicBlock*> visitedBlocks;
    transforms::DominatorTree* currentDomTree = nullptr;
    transforms::DominatorTree* currentPostDomTree = nullptr;
    ir::BasicBlock* currentEndBlock = nullptr;

    // Helper methods for structured control flow
    void emitStructuredBlock(CodeGen& cg, ir::BasicBlock* bb, ir::BasicBlock* endBlock);
    ir::BasicBlock* findMergePoint(ir::BasicBlock* b1, ir::BasicBlock* b2);
    void emitPhis(CodeGen& cg, ir::BasicBlock* target, ir::BasicBlock* source, const std::string& indent);
    bool isLoopHeader(ir::BasicBlock* bb);

    // Helper methods
    std::string getWasmType(const ir::Type* type);
    std::string getWasmLoadInstruction(const ir::Type* type);
    std::string getWasmStoreInstruction(const ir::Type* type);
    
    // Advanced control flow helpers
    std::string getWasmCompareOp(const std::string& op, const ir::Type* type, bool isUnsigned = false);
    void emitBlockStructure(CodeGen& cg, const std::string& blockType);
    void emitLoopStructure(CodeGen& cg, const std::string& label);
    void emitBasicBlock(CodeGen& cg, ir::BasicBlock& bb);
    void emitBasicBlockInstructions(CodeGen& cg, ir::BasicBlock& bb, const std::string& indent);
    
    // Leaf optimization helpers
    bool isLeaf(ir::Function& func) const;
    bool needsTempLocals(ir::Function& func) const;

    // Enhanced ABI Support Methods for WASM32
    void emitStackAllocation(CodeGen& cg, size_t size, const std::string& dest);
    void emitStackDeallocation(CodeGen& cg, size_t size);
    void emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex,
                              const std::string& typeIndex);
    void emitMemoryOperation(CodeGen& cg, const std::string& operation,
                            const ir::Type* type, size_t alignment = 0, size_t offset = 0);
    void emitTypeConversion(CodeGen& cg, const ir::Type* fromType,
                           const ir::Type* toType, bool isUnsigned = false);
};

} // namespace target
} // namespace codegen