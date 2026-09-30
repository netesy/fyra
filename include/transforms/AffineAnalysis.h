#pragma once

#include "ir/Function.h"
#include "ir/Value.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include <optional>
#include <vector>
#include <map>
#include <set>

namespace ir {
class Use;
}

namespace transforms {

/**
 * @brief Target-independent affine expression analysis
 * 
 * This analysis provides a single authoritative implementation for decomposing
 * affine expressions of the form: constant + Σ(coefficient_i * value_i)
 * 
 * Semantic boundaries:
 * - Analyzing an expression != rewriting an expression
 * - Fixed-width wrapping semantics are preserved
 * - Arithmetic occurring in a narrower width (e.g. sext(add i8 x, y)) is NOT
 *   distributed across width boundaries because narrower wrapping would be lost
 * 
 * Supported operations:
 * - Constant
 * - Value (symbolic leaf)
 * - Add
 * - Sub
 * - Mul(Value, Constant)
 * - Mul(Constant, Value)
 * - Shl(Value, Constant) -> treated as coefficient 2^shift
 * - ExtSW/ExtUW/Trunc on leaf symbols
 */
class AffineAnalysis {
public:
    struct Term {
        ir::Value* value;      // Underlying symbolic value (e.g. induction variable, base, etc.)
        ir::Value* rawValue;   // Exact IR value appearing in expression (e.g. ExtSW instruction)
        int64_t coefficient;   // Coefficient in the affine sum
        uint32_t bitWidth;     // Source bit width of this term's value
        bool isSigned;         // Signed interpretation if relevant

        Term() : value(nullptr), rawValue(nullptr), coefficient(0), bitWidth(0), isSigned(false) {}
        Term(ir::Value* v, int64_t c, uint32_t bw = 0, bool s = false, ir::Value* raw = nullptr)
            : value(v), rawValue(raw ? raw : v), coefficient(c), bitWidth(bw), isSigned(s) {}
    };

    struct AffineExpr {
        std::vector<Term> terms;    // All coefficient * value terms
        int64_t constant;           // Constant offset
        uint32_t resultBitWidth;    // Bit width of the overall expression
        bool resultSigned;          // Signed interpretation of result
        bool isValid;               // True if expression is successfully decomposed

        AffineExpr() : constant(0), resultBitWidth(0), resultSigned(false), isValid(false) {}

        // Get coefficient for a specific value (matches either underlying symbol or raw operand)
        std::optional<int64_t> getCoefficient(ir::Value* value) const {
            if (!value) return std::nullopt;
            for (const auto& term : terms) {
                if (term.value == value || term.rawValue == value) {
                    return term.coefficient;
                }
            }
            return std::nullopt;
        }

        // Check if expression is a simple constant
        bool isConstant() const {
            return isValid && terms.empty();
        }

        // Check if expression has a single symbolic term
        bool isSingleTerm() const {
            return isValid && terms.size() == 1;
        }
    };

    AffineAnalysis();
    ~AffineAnalysis() = default;

    /**
     * @brief Analyze a value as an affine expression
     * 
     * @param value The value to analyze
     * @param nodeBudget Maximum number of nodes to visit (prevents pathological growth)
     * @return AffineExpr The decomposed affine expression
     */
    AffineExpr analyze(ir::Value* value, int nodeBudget = 100);

    /**
     * @brief Clear analysis cache (e.g., after IR modification)
     */
    void clearCache();

private:
    struct AnalysisState {
        int nodesVisited;
        int nodeBudget;
        std::set<ir::Value*> inProgress; // Active nodes in current call path (cycle detection)

        AnalysisState(int budget) : nodesVisited(0), nodeBudget(budget) {}
    };

    bool analyzeImpl(ir::Value* value, AffineExpr& result, AnalysisState& state);

    bool analyzeConstant(ir::ConstantInt* ci, AffineExpr& result, uint32_t bitWidth);
    bool analyzeMul(ir::Instruction* inst, AffineExpr& result, AnalysisState& state);
    bool analyzeShl(ir::Instruction* inst, AffineExpr& result, AnalysisState& state);
    bool analyzeAdd(ir::Instruction* inst, AffineExpr& result, AnalysisState& state);
    bool analyzeSub(ir::Instruction* inst, AffineExpr& result, AnalysisState& state);
    bool analyzeExtension(ir::Instruction* inst, AffineExpr& result, AnalysisState& state);

    bool mergeAffine(const AffineExpr& lhs, const AffineExpr& rhs, 
                     AffineExpr& result, bool subtract = false);

    bool isCoefficientSafe(int64_t coeff, uint32_t bitWidth, bool isSigned);
    bool isConstantSafe(int64_t constant, uint32_t bitWidth, bool isSigned);

    std::map<ir::Value*, AffineExpr> cache_;
};

} // namespace transforms
