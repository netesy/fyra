#include "transforms/AffineAnalysis.h"
#include "ir/Use.h"
#include <algorithm>
#include <limits>

namespace transforms {

AffineAnalysis::AffineAnalysis() {
}

void AffineAnalysis::clearCache() {
    cache_.clear();
}

AffineAnalysis::AffineExpr AffineAnalysis::analyze(ir::Value* value, int nodeBudget) {
    if (!value) {
        AffineExpr result;
        result.isValid = false;
        return result;
    }

    auto it = cache_.find(value);
    if (it != cache_.end()) {
        return it->second;
    }

    AnalysisState state(nodeBudget);
    AffineExpr result;
    result.isValid = analyzeImpl(value, result, state);

    if (result.isValid) {
        if (result.resultBitWidth == 0 && value->getType()) {
            if (auto* intTy = dynamic_cast<ir::IntegerType*>(value->getType())) {
                result.resultBitWidth = intTy->getBitwidth();
                result.resultSigned = true;
            }
        }
        cache_[value] = result;
    }

    return result;
}

bool AffineAnalysis::analyzeImpl(ir::Value* value, AffineExpr& result, AnalysisState& state) {
    if (!value) return false;

    state.nodesVisited++;
    if (state.nodesVisited > state.nodeBudget) {
        return false;
    }

    // Cycle check: true cycle along the current call stack path
    if (state.inProgress.count(value)) {
        Term term(value, 1, 0, true);
        if (auto* ty = value->getType()) {
            if (auto* intTy = dynamic_cast<ir::IntegerType*>(ty)) {
                term.bitWidth = intTy->getBitwidth();
            }
        }
        result.terms.clear();
        result.terms.push_back(term);
        result.constant = 0;
        result.resultBitWidth = term.bitWidth;
        result.resultSigned = true;
        result.isValid = true;
        return true;
    }

    // Check cache for DAG subexpression sharing
    auto it = cache_.find(value);
    if (it != cache_.end()) {
        result = it->second;
        return result.isValid;
    }

    state.inProgress.insert(value);

    bool success = false;
    bool isAffineOp = false;
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(value)) {
        uint32_t bw = 32;
        if (auto* intTy = dynamic_cast<ir::IntegerType*>(ci->getType())) {
            bw = intTy->getBitwidth();
        }
        isAffineOp = true;
        success = analyzeConstant(ci, result, bw);
    } else if (auto* inst = dynamic_cast<ir::Instruction*>(value)) {
        ir::Instruction::Opcode op = inst->getOpcode();
        switch (op) {
            case ir::Instruction::Mul:
                isAffineOp = true;
                success = analyzeMul(inst, result, state);
                break;
            case ir::Instruction::Shl:
                isAffineOp = true;
                success = analyzeShl(inst, result, state);
                break;
            case ir::Instruction::Add:
                isAffineOp = true;
                success = analyzeAdd(inst, result, state);
                break;
            case ir::Instruction::Sub:
                isAffineOp = true;
                success = analyzeSub(inst, result, state);
                break;
            case ir::Instruction::ExtSB:
            case ir::Instruction::ExtSH:
            case ir::Instruction::ExtSW:
            case ir::Instruction::ExtS:
            case ir::Instruction::ExtUB:
            case ir::Instruction::ExtUH:
            case ir::Instruction::ExtUW:
            case ir::Instruction::TruncD:
                isAffineOp = true;
                success = analyzeExtension(inst, result, state);
                break;
            default:
                break;
        }
    }

    if (isAffineOp && !success) {
        state.inProgress.erase(value);
        return false;
    }

    if (!success) {
        // For non-affine or leaf values (parameters, phis, loads, bitwise insts, etc.),
        // treat as a symbolic leaf with coefficient 1
        Term term(value, 1, 0, true, value);
        if (auto* ty = value->getType()) {
            if (auto* intTy = dynamic_cast<ir::IntegerType*>(ty)) {
                term.bitWidth = intTy->getBitwidth();
            }
        }
        result.terms.clear();
        result.terms.push_back(term);
        result.constant = 0;
        result.resultBitWidth = term.bitWidth;
        result.resultSigned = true;
        result.isValid = true;
        success = true;
    }

    state.inProgress.erase(value);

    if (success) {
        if (result.resultBitWidth == 0 && value->getType()) {
            if (auto* intTy = dynamic_cast<ir::IntegerType*>(value->getType())) {
                result.resultBitWidth = intTy->getBitwidth();
            }
        }
        cache_[value] = result;
    }
    return success;
}

bool AffineAnalysis::analyzeConstant(ir::ConstantInt* ci, AffineExpr& result, uint32_t bitWidth) {
    if (!ci) return false;

    result.terms.clear();
    result.constant = static_cast<int64_t>(ci->getValue());
    result.resultBitWidth = bitWidth;
    result.resultSigned = true;
    result.isValid = true;
    return true;
}

bool AffineAnalysis::analyzeMul(ir::Instruction* inst, AffineExpr& result, AnalysisState& state) {
    if (!inst || inst->getOperands().size() < 2) return false;
    if (!inst->getOperands()[0] || !inst->getOperands()[1]) return false;

    ir::Value* op0 = inst->getOperands()[0]->get();
    ir::Value* op1 = inst->getOperands()[1]->get();
    if (!op0 || !op1) return false;

    uint32_t bitWidth = 64;
    if (auto* intTy = dynamic_cast<ir::IntegerType*>(inst->getType())) {
        bitWidth = intTy->getBitwidth();
    }

    ir::ConstantInt* const0 = dynamic_cast<ir::ConstantInt*>(op0);
    ir::ConstantInt* const1 = dynamic_cast<ir::ConstantInt*>(op1);

    // Both constants
    if (const0 && const1) {
        int64_t val = 0;
        if (__builtin_mul_overflow(static_cast<int64_t>(const0->getValue()),
                                   static_cast<int64_t>(const1->getValue()), &val)) {
            return false;
        }
        if (!isConstantSafe(val, bitWidth, true)) return false;

        result.terms.clear();
        result.constant = val;
        result.resultBitWidth = bitWidth;
        result.resultSigned = true;
        result.isValid = true;
        return true;
    }

    ir::Value* valOp = nullptr;
    ir::ConstantInt* constOp = nullptr;
    if (const1 && !const0) {
        valOp = op0;
        constOp = const1;
    } else if (const0 && !const1) {
        valOp = op1;
        constOp = const0;
    } else {
        // Value * Value is not affine
        return false;
    }

    AffineExpr subExpr;
    if (!analyzeImpl(valOp, subExpr, state)) return false;

    int64_t coeff = static_cast<int64_t>(constOp->getValue());
    int64_t newConstant = 0;
    if (__builtin_mul_overflow(subExpr.constant, coeff, &newConstant)) return false;
    if (!isConstantSafe(newConstant, bitWidth, true)) return false;

    result.terms.clear();
    result.constant = newConstant;
    result.resultBitWidth = bitWidth;
    result.resultSigned = subExpr.resultSigned;

    for (const auto& term : subExpr.terms) {
        Term newTerm = term;
        int64_t newTermCoeff = 0;
        if (__builtin_mul_overflow(newTerm.coefficient, coeff, &newTermCoeff)) return false;
        newTerm.coefficient = newTermCoeff;
        if (!isCoefficientSafe(newTerm.coefficient, bitWidth, newTerm.isSigned)) return false;
        result.terms.push_back(newTerm);
    }
    result.isValid = true;
    return true;
}

bool AffineAnalysis::analyzeShl(ir::Instruction* inst, AffineExpr& result, AnalysisState& state) {
    if (!inst || inst->getOperands().size() < 2) return false;
    if (!inst->getOperands()[0] || !inst->getOperands()[1]) return false;

    ir::Value* op0 = inst->getOperands()[0]->get();
    ir::Value* op1 = inst->getOperands()[1]->get();
    if (!op0 || !op1) return false;

    ir::ConstantInt* shiftConst = dynamic_cast<ir::ConstantInt*>(op1);
    if (!shiftConst) return false;

    int64_t shift = static_cast<int64_t>(shiftConst->getValue());
    uint32_t bitWidth = 64;
    if (auto* intTy = dynamic_cast<ir::IntegerType*>(inst->getType())) {
        bitWidth = intTy->getBitwidth();
    }

    if (shift < 0 || shift >= 63 || (bitWidth > 0 && shift >= bitWidth)) return false;

    AffineExpr subExpr;
    if (!analyzeImpl(op0, subExpr, state)) return false;

    int64_t coeff = (1LL << shift);
    int64_t newConstant = 0;
    if (__builtin_mul_overflow(subExpr.constant, coeff, &newConstant)) return false;
    if (!isConstantSafe(newConstant, bitWidth, true)) return false;

    result.terms.clear();
    result.constant = newConstant;
    result.resultBitWidth = bitWidth;
    result.resultSigned = subExpr.resultSigned;

    for (const auto& term : subExpr.terms) {
        Term newTerm = term;
        int64_t newTermCoeff = 0;
        if (__builtin_mul_overflow(newTerm.coefficient, coeff, &newTermCoeff)) return false;
        newTerm.coefficient = newTermCoeff;
        if (!isCoefficientSafe(newTerm.coefficient, bitWidth, newTerm.isSigned)) return false;
        result.terms.push_back(newTerm);
    }
    result.isValid = true;
    return true;
}

bool AffineAnalysis::analyzeAdd(ir::Instruction* inst, AffineExpr& result, AnalysisState& state) {
    if (!inst || inst->getOperands().size() < 2) return false;
    if (!inst->getOperands()[0] || !inst->getOperands()[1]) return false;

    ir::Value* op0 = inst->getOperands()[0]->get();
    ir::Value* op1 = inst->getOperands()[1]->get();
    if (!op0 || !op1) return false;

    AffineExpr expr0, expr1;
    if (!analyzeImpl(op0, expr0, state) || !analyzeImpl(op1, expr1, state)) {
        return false;
    }

    return mergeAffine(expr0, expr1, result, false);
}

bool AffineAnalysis::analyzeSub(ir::Instruction* inst, AffineExpr& result, AnalysisState& state) {
    if (!inst || inst->getOperands().size() < 2) return false;
    if (!inst->getOperands()[0] || !inst->getOperands()[1]) return false;

    ir::Value* op0 = inst->getOperands()[0]->get();
    ir::Value* op1 = inst->getOperands()[1]->get();
    if (!op0 || !op1) return false;

    AffineExpr expr0, expr1;
    if (!analyzeImpl(op0, expr0, state) || !analyzeImpl(op1, expr1, state)) {
        return false;
    }

    return mergeAffine(expr0, expr1, result, true);
}

bool AffineAnalysis::analyzeExtension(ir::Instruction* inst, AffineExpr& result, AnalysisState& state) {
    if (!inst || inst->getOperands().empty() || !inst->getOperands()[0] || !inst->getOperands()[0]->get()) {
        return false;
    }

    ir::Value* inner = inst->getOperands()[0]->get();
    uint32_t dstBits = 0;
    if (auto* intTy = dynamic_cast<ir::IntegerType*>(inst->getType())) {
        dstBits = intTy->getBitwidth();
    }
    if (dstBits == 0) dstBits = 64;

    ir::Instruction::Opcode op = inst->getOpcode();
    auto isSignedExtOp = [](ir::Instruction::Opcode o) {
        return o == ir::Instruction::ExtSB || o == ir::Instruction::ExtSH ||
               o == ir::Instruction::ExtSW || o == ir::Instruction::ExtS;
    };
    auto isExtOrTruncOp = [&isSignedExtOp](ir::Instruction::Opcode o) {
        return isSignedExtOp(o) || o == ir::Instruction::ExtUB ||
               o == ir::Instruction::ExtUH || o == ir::Instruction::ExtUW ||
               o == ir::Instruction::TruncD;
    };

    // If constant, fold safely into dstBits
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(inner)) {
        uint32_t srcBits = 32;
        if (auto* sTy = dynamic_cast<ir::IntegerType*>(ci->getType())) {
            srcBits = sTy->getBitwidth();
        }
        int64_t val = 0;
        if (op == ir::Instruction::TruncD) {
            if (dstBits < 64) {
                uint64_t mask = (1ULL << dstBits) - 1;
                val = static_cast<int64_t>(ci->getValue() & mask);
            } else {
                val = static_cast<int64_t>(ci->getValue());
            }
        } else if (isSignedExtOp(op)) {
            if (srcBits < 64) {
                uint64_t mask = (1ULL << srcBits) - 1;
                uint64_t uval = ci->getValue() & mask;
                if (uval & (1ULL << (srcBits - 1))) {
                    val = static_cast<int64_t>(uval | ~mask);
                } else {
                    val = static_cast<int64_t>(uval);
                }
            } else {
                val = static_cast<int64_t>(ci->getValue());
            }
        } else {
            // Unsigned extension
            if (srcBits < 64) {
                uint64_t mask = (1ULL << srcBits) - 1;
                val = static_cast<int64_t>(ci->getValue() & mask);
            } else {
                val = static_cast<int64_t>(ci->getValue());
            }
        }
        result.terms.clear();
        result.constant = val;
        result.resultBitWidth = dstBits;
        result.resultSigned = isSignedExtOp(op);
        result.isValid = true;
        return true;
    }

    // Check if inner is an arithmetic operation (Add, Sub, Mul, Shl)
    if (auto* innerInst = dynamic_cast<ir::Instruction*>(inner)) {
        ir::Instruction::Opcode innerOp = innerInst->getOpcode();
        if (innerOp == ir::Instruction::Add || innerOp == ir::Instruction::Sub ||
            innerOp == ir::Instruction::Mul || innerOp == ir::Instruction::Shl) {
            // Extension or truncation of an arithmetic operation wraps in the source width!
            // It cannot be distributed into the destination width as an affine expression.
            // Conservatively reject distribution across width-changing boundary.
            return false;
        }
    }

    // Inner is a leaf value (e.g. PhiNode, parameter, global, load) or nested extension
    ir::Value* rootLeaf = inner;
    uint32_t srcBits = dstBits;
    if (auto* sTy = dynamic_cast<ir::IntegerType*>(inner->getType())) {
        srcBits = sTy->getBitwidth();
    }
    bool isSignedExt = isSignedExtOp(op);

    while (auto* innerExt = dynamic_cast<ir::Instruction*>(rootLeaf)) {
        ir::Instruction::Opcode extOp = innerExt->getOpcode();
        if (isExtOrTruncOp(extOp)) {
            if (innerExt->getOperands().empty() || !innerExt->getOperands()[0] || !innerExt->getOperands()[0]->get()) {
                break;
            }
            ir::Value* nextInner = innerExt->getOperands()[0]->get();
            if (auto* nextInnerInst = dynamic_cast<ir::Instruction*>(nextInner)) {
                ir::Instruction::Opcode nextOp = nextInnerInst->getOpcode();
                if (nextOp == ir::Instruction::Add || nextOp == ir::Instruction::Sub ||
                    nextOp == ir::Instruction::Mul || nextOp == ir::Instruction::Shl) {
                    break;
                }
            }
            rootLeaf = nextInner;
            if (auto* sTy = dynamic_cast<ir::IntegerType*>(rootLeaf->getType())) {
                srcBits = sTy->getBitwidth();
            }
        } else {
            break;
        }
    }

    Term term(rootLeaf, 1, srcBits, isSignedExt, inst);
    result.terms.clear();
    result.terms.push_back(term);
    result.constant = 0;
    result.resultBitWidth = dstBits;
    result.resultSigned = isSignedExt;
    result.isValid = true;
    return true;
}

bool AffineAnalysis::mergeAffine(const AffineExpr& lhs, const AffineExpr& rhs,
                                 AffineExpr& result, bool subtract) {
    if (!lhs.isValid || !rhs.isValid) return false;

    uint32_t width = lhs.resultBitWidth ? lhs.resultBitWidth : rhs.resultBitWidth;
    if (lhs.resultBitWidth != 0 && rhs.resultBitWidth != 0 && lhs.resultBitWidth != rhs.resultBitWidth) {
        return false;
    }

    int64_t newConstant = 0;
    if (subtract) {
        if (__builtin_sub_overflow(lhs.constant, rhs.constant, &newConstant)) return false;
    } else {
        if (__builtin_add_overflow(lhs.constant, rhs.constant, &newConstant)) return false;
    }
    if (!isConstantSafe(newConstant, width, true)) return false;

    result.terms.clear();
    result.constant = newConstant;
    result.resultBitWidth = width;
    result.resultSigned = lhs.resultSigned || rhs.resultSigned;

    std::vector<Term> mergedTerms;
    for (const auto& term : lhs.terms) {
        mergedTerms.push_back(term);
    }

    for (const auto& rTerm : rhs.terms) {
        int64_t rhsCoeff = subtract ? -rTerm.coefficient : rTerm.coefficient;
        if (subtract && rTerm.coefficient == std::numeric_limits<int64_t>::min()) return false;

        bool found = false;
        for (auto& lTerm : mergedTerms) {
            if (lTerm.value == rTerm.value) {
                int64_t newCoeff = 0;
                if (__builtin_add_overflow(lTerm.coefficient, rhsCoeff, &newCoeff)) return false;
                if (!isCoefficientSafe(newCoeff, width, lTerm.isSigned)) return false;
                lTerm.coefficient = newCoeff;
                found = true;
                break;
            }
        }
        if (!found) {
            Term newTerm = rTerm;
            newTerm.coefficient = rhsCoeff;
            if (!isCoefficientSafe(newTerm.coefficient, width, newTerm.isSigned)) return false;
            mergedTerms.push_back(newTerm);
        }
    }

    // Filter out canceled terms (coefficient == 0)
    for (const auto& term : mergedTerms) {
        if (term.coefficient != 0) {
            result.terms.push_back(term);
        }
    }

    result.isValid = true;
    return true;
}

bool AffineAnalysis::isCoefficientSafe(int64_t coeff, uint32_t bitWidth, bool isSigned) {
    if (bitWidth == 0 || bitWidth >= 64) return true;

    if (isSigned) {
        int64_t maxVal = (int64_t)((1ULL << (bitWidth - 1)) - 1);
        int64_t minVal = -maxVal - 1;
        return coeff >= minVal && coeff <= maxVal;
    } else {
        uint64_t maxVal = (bitWidth >= 64) ? UINT64_MAX : ((1ULL << bitWidth) - 1);
        return coeff >= 0 && static_cast<uint64_t>(coeff) <= maxVal;
    }
}

bool AffineAnalysis::isConstantSafe(int64_t constant, uint32_t bitWidth, bool isSigned) {
    return isCoefficientSafe(constant, bitWidth, isSigned);
}

} // namespace transforms
