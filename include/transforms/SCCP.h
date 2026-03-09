#pragma once

#include "TransformPass.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <map>
#include <set>
#include <vector>
#include <limits>

namespace transforms {

/**
 * @brief Enhanced Sparse Conditional Constant Propagation pass
 * 
 * This enhanced version supports:
 * - All arithmetic operations (Add, Sub, Mul, Div, Rem, Udiv, Urem)
 * - Bitwise operations (And, Or, Xor, Shl, Shr, Sar)
 * - Comparison operations with boolean results
 * - Overflow detection and error reporting
 * - Type-safe constant propagation
 */
class SCCP : public TransformPass {
public:
    explicit SCCP(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Enhanced SCCP", error_reporter) {}

    // Legacy interface for compatibility
    bool run(ir::Function& func) {
        return TransformPass::run(func);
    }

protected:
    /**
     * @brief Perform the actual SCCP transformation
     * 
     * @param func Function to transform
     * @return true if IR was modified
     */
    bool performTransformation(ir::Function& func) override;

    /**
     * @brief Validate that the function is in SSA form
     * 
     * @param func Function to validate
     * @return true if preconditions are met
     */
    bool validatePreconditions(ir::Function& func) override;

private:
    enum LatticeValue {
        Top,
        Constant,
        Bottom
    };

    struct LatticeEntry {
        LatticeValue type = Top;
        ir::Constant* constant = nullptr;
    };

    void initialize(ir::Function& func);
    void visit(ir::Instruction* instr);
    LatticeEntry getLatticeValue(ir::Value* val);
    void setLatticeValue(ir::Instruction* instr, LatticeEntry new_val);
    
    // Enhanced operation handlers
    void handleArithmeticOp(ir::Instruction* instr);
    void handleBitwiseOp(ir::Instruction* instr);
    void handleComparisonOp(ir::Instruction* instr);
    void handleUnaryOp(ir::Instruction* instr);
    
    // Overflow detection helpers
    bool detectAddOverflow(uint64_t a, uint64_t b, ir::IntegerType* type);
    bool detectMulOverflow(uint64_t a, uint64_t b, ir::IntegerType* type);
    bool detectDivisionByZero(uint64_t divisor);
    
    // Type-safe operation helpers
    ir::Constant* performSafeArithmetic(ir::Instruction::Opcode op, 
                                       ir::ConstantInt* c1, ir::ConstantInt* c2);
    ir::Constant* performSafeBitwise(ir::Instruction::Opcode op,
                                   ir::ConstantInt* c1, ir::ConstantInt* c2);
    ir::Constant* performSafeComparison(ir::Instruction::Opcode op,
                                      ir::ConstantInt* c1, ir::ConstantInt* c2);

    std::map<ir::Value*, LatticeEntry> lattice;
    std::vector<ir::Instruction*> instructionWorklist;
    std::vector<ir::BasicBlock*> blockWorklist;
};

} // namespace transforms
