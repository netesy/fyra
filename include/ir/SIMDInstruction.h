#pragma once

#include "Instruction.h"
#include "Type.h"
#include <string>
#include <vector>

namespace ir {

// SIMD comparison operations
enum class VectorCompareOp {
    EQ,    // Equal
    NE,    // Not equal
    LT,    // Less than
    LE,    // Less than or equal
    GT,    // Greater than
    GE,    // Greater than or equal
    ULT,   // Unsigned less than
    ULE,   // Unsigned less than or equal
    UGT,   // Unsigned greater than
    UGE    // Unsigned greater than or equal
};

// Vector shuffle mask representation
struct ShuffleMask {
    std::vector<int> indices;
    unsigned resultElements;
    
    ShuffleMask(std::vector<int> indices, unsigned resultElements) 
        : indices(indices), resultElements(resultElements) {}
    
    bool isValid() const {
        return indices.size() == resultElements;
    }
    
    std::string toString() const {
        std::string result = "[";
        for (size_t i = 0; i < indices.size(); ++i) {
            if (i > 0) result += ", ";
            result += std::to_string(indices[i]);
        }
        result += "]";
        return result;
    }
};

// SIMD instruction specialization
class VectorInstruction : public Instruction {
public:
    VectorInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands, 
                     unsigned vectorWidth = 0, BasicBlock* parent = nullptr);
    
    // Vector-specific methods
    unsigned getVectorWidth() const { return vectorWidth; }
    void setVectorWidth(unsigned width) { vectorWidth = width; }
    
    bool isVectorInstruction() const { return vectorWidth > 0; }
    
    // Type checking helpers
    bool hasVectorOperands() const;
    VectorType* getVectorType() const;
    Type* getElementType() const;
    unsigned getNumElements() const;
    
    // Pattern matching helpers
    bool isVectorArithmetic() const;
    bool isVectorLogical() const;
    bool isVectorMemory() const;
    bool isVectorComparison() const;
    bool isVectorShuffle() const;
    
    void print(std::ostream& os) const override;

private:
    unsigned vectorWidth; // Width in bits (128, 256, 512, etc.)
};

// Fused instruction specialization
class FusedInstruction : public Instruction {
public:
    enum FusedType {
        MultiplyAdd,      // a * b + c
        MultiplySubtract, // a * b - c
        NegMultiplyAdd,   // -(a * b) + c
        NegMultiplySubtract, // -(a * b) - c
        AddressCalculation, // Complex addressing modes
        CompareAndBranch,  // Fused compare and branch
        LoadAndOperate,    // Load followed by operation
        StoreWithUpdate    // Store with address update
    };
    
    FusedInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands,
                    FusedType fusedType, BasicBlock* parent = nullptr);
    
    FusedType getFusedType() const { return fusedType; }
    
    // Pattern matching for optimization
    bool canBeFused() const;
    bool isBeneficial() const; // Cost analysis
    
    void print(std::ostream& os) const override;

private:
    FusedType fusedType;
};

// SIMD pattern matching utilities
class SIMDPatternMatcher {
public:
    // Pattern detection
    static bool isVectorizableLoop(const std::vector<Instruction*>& instructions);
    static bool canVectorize(Instruction* inst);
    static unsigned getOptimalVectorWidth(Type* elementType, const std::string& targetArch);
    
    // Fusion detection
    static bool canFuseMultiplyAdd(Instruction* mul, Instruction* add);
    static bool hasComplexAddressingMode(Instruction* load);
    
    // Cost analysis
    static double getVectorizationBenefit(const std::vector<Instruction*>& instructions, 
                                        unsigned vectorWidth);
    static bool isProfitableToVectorize(const std::vector<Instruction*>& instructions);
};

// SIMD instruction builder helper
class SIMDBuilder {
public:
    // Vector instruction creation
    static VectorInstruction* createVectorAdd(VectorType* type, Value* lhs, Value* rhs);
    static VectorInstruction* createVectorSub(VectorType* type, Value* lhs, Value* rhs);
    static VectorInstruction* createVectorMul(VectorType* type, Value* lhs, Value* rhs);
    static VectorInstruction* createVectorLoad(VectorType* type, Value* ptr);
    static VectorInstruction* createVectorStore(Value* vec, Value* ptr);
    static VectorInstruction* createBroadcast(VectorType* type, Value* scalar);
    static VectorInstruction* createShuffle(VectorType* type, Value* vec1, Value* vec2, 
                                          const ShuffleMask& mask);
    
    // Fused instruction creation
    static FusedInstruction* createFMA(Type* type, Value* a, Value* b, Value* c);
    static FusedInstruction* createFMS(Type* type, Value* a, Value* b, Value* c);
    
    // Vectorization utilities
    static std::vector<VectorInstruction*> vectorizeScalarLoop(
        const std::vector<Instruction*>& scalarInstructions, unsigned vectorWidth);
};

} // namespace ir