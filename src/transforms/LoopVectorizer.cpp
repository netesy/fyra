#include "transforms/LoopVectorizer.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/ScalarEvolution.h"
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <cstdlib>
#include <optional>

namespace transforms {

namespace {

void logDiag(const std::string& msg) {
    if (std::getenv("FYRA_VECTORIZER_DIAG")) {
        std::cout << "[LoopVectorizer Diag] " << msg << std::endl;
    }
}

struct MemoryAccess {
    ir::Instruction* inst = nullptr;
    bool isLoad = false;
    bool isStore = false;
    ir::Value* base = nullptr;
    ir::Value* induction = nullptr;
    int64_t constantOffset = 0; // Bytes from base.
    int64_t stride = 0;         // Bytes per induction step.
    int64_t elementSize = 4;
    ir::Type* elementType = nullptr;
};

enum class MemoryLegalityKind { SafeStatically, RequiresRuntimeCheck, Unsafe };

struct RuntimeAliasCheck {
    ir::Value* firstBase = nullptr;
    ir::Value* secondBase = nullptr;
};

struct MemoryLegality {
    MemoryLegalityKind kind = MemoryLegalityKind::SafeStatically;
    std::string reason;
    std::vector<RuntimeAliasCheck> runtimeChecks;
};

enum class ReductionKind { Add, Mul, SignedMin, SignedMax, FAdd, FMul };

struct ReductionPlan {
    ReductionKind kind = ReductionKind::Add;
    ir::PhiNode* phi = nullptr;
    ir::Value* initialValue = nullptr;
    ir::Value* scalarTerm = nullptr;
    ir::Instruction* update = nullptr;
    ir::Type* scalarType = nullptr;       // Accumulator scalar type (e.g. i64 or i32)
    ir::Type* sourceType = nullptr;       // Source scalar type before extension (e.g. i32)
    ir::VectorType* vectorType = nullptr; // Accumulator vector type (e.g. <4xi64>)
    ir::VectorType* sourceVectorType = nullptr; // Source vector type (e.g. <8xi32>)
    ir::Instruction::Opcode vectorOpcode = ir::Instruction::VAdd;
    int64_t identity = 0;
    double fpIdentity = 0.0;
    bool isFloatingPoint = false;
    const char* collapseStrategy = "scalar lane fold";
    bool isWidening = false;
    // A signed i32 expression which is widened lane-by-lane and accumulated
    // in an i64 scalar.  This deliberately stays 128-bit on x64.
    bool isRegisterWidening = false;
    ir::Instruction::Opcode conversionOpcode = ir::Instruction::VSExt;
    unsigned sourceVF = 8;
    unsigned accumulatorVF = 4;
    unsigned accumulatorsPerChunk = 2;
};

struct PredicationPlan {
    enum class ValueKind { ExistingValue, Constant, UnaryOp, BinaryOp };
    struct PredicatedValue {
        ValueKind kind = ValueKind::ExistingValue;
        ir::Instruction::Opcode opcode = ir::Instruction::Copy;
        ir::Value* value = nullptr;
        ir::Type* type = nullptr;
        unsigned arithmeticCost = 0;
    };
    ir::Instruction* condition = nullptr;
    ir::VectorCompareOp predicate = ir::VectorCompareOp::EQ;
    ir::Value* lhs = nullptr;
    ir::Value* rhs = nullptr;
    ir::BasicBlock* thenBlock = nullptr;
    ir::BasicBlock* elseBlock = nullptr;
    ir::BasicBlock* mergeBlock = nullptr;
    ir::Value* thenValue = nullptr;
    ir::Value* elseValue = nullptr;
    ir::PhiNode* mergePhi = nullptr;
    ir::Instruction* store = nullptr;
    ir::Type* scalarResultType = nullptr;
    ir::Type* compareOperandType = nullptr;
    ir::VectorType* vectorResultType = nullptr;
    size_t elementSize = 0;
    unsigned vectorFactor = 0;
    PredicatedValue thenExpression;
    PredicatedValue elseExpression;
    bool inverted = false;
};

struct VectorizationPlan {
    bool legal = false;
    std::string rejectionReason;
    ir::PhiNode* indVarPhi = nullptr;
    ir::Value* initVal = nullptr;
    ir::Instruction* stepInst = nullptr;
    int64_t stepConst = 1;
    ir::Value* boundVal = nullptr;
    ir::Instruction* condInst = nullptr;

    ir::BasicBlock* headerBB = nullptr;
    ir::BasicBlock* bodyBB = nullptr;
    ir::BasicBlock* latchBB = nullptr;
    ir::BasicBlock* preheaderBB = nullptr;
    ir::BasicBlock* exitBB = nullptr;

    std::vector<ReductionPlan> reductions;
    std::optional<PredicationPlan> predication;
    std::vector<MemoryAccess> memoryAccesses;
    MemoryLegality memoryLegality;

    // Derived properties
    unsigned loopVF = 8;
    unsigned vectorFactor = 8;
    unsigned vectorWidthBits = 256;
    ir::Type* mainElemType = nullptr;
    ir::VectorType* vectorType = nullptr;
    size_t elementByteSize = 0;
    ir::Instruction::Opcode vectorOpcode = ir::Instruction::VAdd;
    uint64_t mulScaleFactor = 1;
    bool isWideningReduction = false;
    bool isRegisterWideningReduction = false;
    bool inclusiveBound = false;
};

bool isPureI32Expression(ir::Value* value, ir::PhiNode* induction,
                         std::set<ir::Value*>& visiting) {
    if (value == induction || dynamic_cast<ir::ConstantInt*>(value)) {
        ir::Type* type = value ? value->getType() : nullptr;
        return type && type->isIntegerTy() && type->getSize() == 4;
    }
    auto* inst = dynamic_cast<ir::Instruction*>(value);
    if (!inst || !inst->getType() || !inst->getType()->isIntegerTy() ||
        inst->getType()->getSize() != 4 || !visiting.insert(value).second)
        return false;
    const auto op = inst->getOpcode();
    const bool supported = op == ir::Instruction::Add ||
                           op == ir::Instruction::Sub ||
                           op == ir::Instruction::Mul;
    bool result = supported && inst->getOperands().size() == 2 &&
        isPureI32Expression(inst->getOperands()[0]->get(), induction, visiting) &&
        isPureI32Expression(inst->getOperands()[1]->get(), induction, visiting);
    visiting.erase(value);
    return result;
}

bool isInductionIndex(ir::Value* value, ir::PhiNode* induction) {
    if (value == induction) return true;
    auto* inst = dynamic_cast<ir::Instruction*>(value);
    return inst && inst->getOpcode() == ir::Instruction::ExtSW &&
           !inst->getOperands().empty() && inst->getOperands()[0]->get() == induction;
}

ir::Instruction* terminator(ir::BasicBlock* block) {
    if (!block || block->getInstructions().empty()) return nullptr;
    return block->getInstructions().back().get();
}

bool isSpeculativelySafe(ir::Instruction* inst, std::string& reason) {
    using O = ir::Instruction::Opcode;
    switch (inst->getOpcode()) {
        case O::Add: case O::Sub: case O::Mul:
        case O::FAdd: case O::FSub: case O::FMul:
        case O::Neg: case O::Copy:
        case O::Load: case O::Loaduw: case O::Loads: case O::Loadd:
        case O::Jmp:
            return true;
        case O::Div: case O::Udiv: case O::Rem: case O::Urem:
        case O::FDiv: case O::FRem:
            reason = "conditional arm contains potentially trapping operation";
            return false;
        default:
            reason = "conditional arm contains side-effecting or unsupported operation";
            return false;
    }
}

PredicationPlan::PredicatedValue describePredicatedValue(ir::Value* value) {
    PredicationPlan::PredicatedValue result;
    result.value = value;
    result.type = value ? value->getType() : nullptr;
    if (dynamic_cast<ir::Constant*>(value)) {
        result.kind = PredicationPlan::ValueKind::Constant;
    } else if (auto* inst = dynamic_cast<ir::Instruction*>(value)) {
        result.opcode = inst->getOpcode();
        if (inst->getOpcode() == ir::Instruction::Neg) {
            result.kind = PredicationPlan::ValueKind::UnaryOp;
            result.arithmeticCost = 1;
        } else if (inst->getOpcode() == ir::Instruction::Add || inst->getOpcode() == ir::Instruction::Sub ||
                   inst->getOpcode() == ir::Instruction::Mul ||
                   inst->getOpcode() == ir::Instruction::FAdd || inst->getOpcode() == ir::Instruction::FSub ||
                   inst->getOpcode() == ir::Instruction::FMul) {
            result.kind = PredicationPlan::ValueKind::BinaryOp;
            result.arithmeticCost = 1;
        }
    }
    return result;
}

std::optional<ir::VectorCompareOp> vectorPredicate(ir::Instruction::Opcode opcode) {
    using O = ir::Instruction::Opcode;
    switch (opcode) {
        case O::Ceq: case O::Ceqf: return ir::VectorCompareOp::EQ;
        case O::Cne: case O::Cnef: return ir::VectorCompareOp::NE;
        case O::Cslt: case O::Clt: return ir::VectorCompareOp::LT;
        case O::Csle: case O::Cle: return ir::VectorCompareOp::LE;
        case O::Csgt: case O::Cgt: return ir::VectorCompareOp::GT;
        case O::Csge: case O::Cge: return ir::VectorCompareOp::GE;
        default: return std::nullopt;
    }
}

// Flatten the deliberately small address language accepted by the loop
// vectorizer.  It recognizes additions in either order, integer constants,
// and induction*constant in either order.  Anything else remains conservative.
bool collectAddressTerms(ir::Value* value, ir::PhiNode* induction,
                         ir::Value*& base, int64_t& stride,
                         int64_t& constantOffset) {
    if (isInductionIndex(value, induction)) {
        stride += 1;
        return true;
    }
    if (auto* constant = dynamic_cast<ir::ConstantInt*>(value)) {
        constantOffset += static_cast<int64_t>(constant->getValue());
        return true;
    }
    auto* inst = dynamic_cast<ir::Instruction*>(value);
    if (inst && inst->getOpcode() == ir::Instruction::Add &&
        inst->getOperands().size() == 2) {
        return collectAddressTerms(inst->getOperands()[0]->get(), induction,
                                   base, stride, constantOffset) &&
               collectAddressTerms(inst->getOperands()[1]->get(), induction,
                                   base, stride, constantOffset);
    }
    if (inst && inst->getOpcode() == ir::Instruction::Mul &&
        inst->getOperands().size() == 2) {
        for (unsigned indexOperand = 0; indexOperand != 2; ++indexOperand) {
            auto* scale = dynamic_cast<ir::ConstantInt*>(
                inst->getOperands()[1 - indexOperand]->get());
            if (scale && isInductionIndex(inst->getOperands()[indexOperand]->get(), induction)) {
                stride += static_cast<int64_t>(scale->getValue());
                return true;
            }
        }
        return false;
    }
    if (!base) {
        base = value;
        return true;
    }
    return false;
}

bool normalizeMemoryAccess(MemoryAccess& access, ir::PhiNode* induction) {
    ir::Value* pointer = access.isStore ? access.inst->getOperands()[1]->get()
                                        : access.inst->getOperands()[0]->get();
    access.base = nullptr;
    access.induction = induction;
    access.stride = 0;
    access.constantOffset = 0;
    if (!collectAddressTerms(pointer, induction, access.base, access.stride,
                             access.constantOffset) || !access.base)
        return false;
    return access.stride == access.elementSize &&
           access.constantOffset % access.elementSize == 0;
}

bool isKnownDistinctAllocation(ir::Value* lhs, ir::Value* rhs) {
    if (lhs == rhs) return false;
    auto* lhsInst = dynamic_cast<ir::Instruction*>(lhs);
    auto* rhsInst = dynamic_cast<ir::Instruction*>(rhs);
    auto isAlloc = [](ir::Instruction* inst) {
        return inst && (inst->getOpcode() == ir::Instruction::Alloc ||
                        inst->getOpcode() == ir::Instruction::Alloc4 ||
                        inst->getOpcode() == ir::Instruction::Alloc16);
    };
    return isAlloc(lhsInst) && isAlloc(rhsInst);
}

MemoryLegality classifyMemory(const std::vector<MemoryAccess>& accesses) {
    MemoryLegality result;
    std::set<std::pair<ir::Value*, ir::Value*>> seenChecks;
    for (size_t i = 0; i < accesses.size(); ++i) {
        for (size_t j = i + 1; j < accesses.size(); ++j) {
            const auto& first = accesses[i];
            const auto& second = accesses[j];
            if (!first.isStore && !second.isStore)
                continue; // Read/read overlap has no dependence.

            if (first.base == second.base) {
                const int64_t byteDelta = second.constantOffset - first.constantOffset;
                const int64_t distance = byteDelta / first.elementSize;
                // A same-base pair containing a write is accepted only when its
                // touched locations are provably disjoint.  Equal offsets and
                // nonzero distances are conservatively kept scalar: the former
                // can change same-iteration ordering; the latter is a genuine
                // loop-carried dependence for some iteration.
                if (byteDelta % first.elementSize == 0) {
                    result.kind = MemoryLegalityKind::Unsafe;
                    result.reason = distance == 0
                        ? "same-base read/write ordering is not proven safe"
                        : "inherent same-base loop-carried dependence (distance=" +
                              std::to_string(distance) + ")";
                    return result;
                }
                result.kind = MemoryLegalityKind::Unsafe;
                result.reason = "same-base accesses have incompatible byte offsets";
                return result;
            }

            if (isKnownDistinctAllocation(first.base, second.base))
                continue;

            ir::Value* lower = first.base;
            ir::Value* upper = second.base;
            if (std::less<ir::Value*>{}(upper, lower)) std::swap(lower, upper);
            if (seenChecks.insert({lower, upper}).second)
                result.runtimeChecks.push_back({lower, upper});
        }
    }
    if (!result.runtimeChecks.empty()) {
        result.kind = MemoryLegalityKind::RequiresRuntimeCheck;
        result.reason = "possible alias between distinct pointer bases";
    }
    return result;
}

// Recognize typed byte addressing in either commutative order.  A scalar access
// is consecutive only when its address is base + sext(i) * element byte size.
bool isUnitStrideAddress(ir::Value* pointer, ir::PhiNode* induction, size_t elementByteSize) {
    auto* add = dynamic_cast<ir::Instruction*>(pointer);
    if (!add || add->getOpcode() != ir::Instruction::Add || add->getOperands().size() != 2)
        return false;

    for (unsigned offsetOperand = 0; offsetOperand != 2; ++offsetOperand) {
        auto* mul = dynamic_cast<ir::Instruction*>(add->getOperands()[offsetOperand]->get());
        if (!mul || mul->getOpcode() != ir::Instruction::Mul || mul->getOperands().size() != 2)
            continue;
        for (unsigned indexOperand = 0; indexOperand != 2; ++indexOperand) {
            auto* scale = dynamic_cast<ir::ConstantInt*>(mul->getOperands()[1 - indexOperand]->get());
            if (isInductionIndex(mul->getOperands()[indexOperand]->get(), induction) &&
                scale && scale->getValue() == elementByteSize)
                return true;
        }
    }
    return false;
}

ir::Value* extractBasePointer(ir::Value* ptr) {
    if (!ptr) return nullptr;
    ir::Instruction* ptrInst = dynamic_cast<ir::Instruction*>(ptr);
    if (ptrInst && ptrInst->getOpcode() == ir::Instruction::Add && ptrInst->getOperands().size() >= 2) {
        ir::Value* op0 = ptrInst->getOperands()[0]->get();
        ir::Value* op1 = ptrInst->getOperands()[1]->get();
        if (op0->getType() && op0->getType()->isPointerTy()) return op0;
        if (op1->getType() && op1->getType()->isPointerTy()) return op1;
        if (dynamic_cast<ir::Parameter*>(op0) || dynamic_cast<ir::Instruction*>(op0)) return op0;
        return op1;
    }
    return ptr;
}

void replaceAddressBase(ir::Value* value, ir::Value* oldBase, ir::Value* newBase) {
    auto* inst = dynamic_cast<ir::Instruction*>(value);
    if (!inst) return;
    const auto opcode = inst->getOpcode();
    if (opcode != ir::Instruction::Add && opcode != ir::Instruction::Sub &&
        opcode != ir::Instruction::Mul && opcode != ir::Instruction::ExtSW &&
        opcode != ir::Instruction::ExtUW)
        return;
    for (auto& operand : inst->getOperands()) {
        if (operand->get() == oldBase) {
            operand->set(newBase);
        } else {
            replaceAddressBase(operand->get(), oldBase, newBase);
        }
    }
}

} // anonymous namespace

bool LoopVectorizer::performTransformation(ir::Function& func) {
    bool changed = false;

    logDiag("Analyzing function: " + func.getName());

    for (auto bbIt = func.getBasicBlocks().begin(); bbIt != func.getBasicBlocks().end(); ++bbIt) {
        ir::BasicBlock* headerBB = bbIt->get();

        // A versioned loop deliberately retains this original loop as the
        // unsafe scalar fallback.  The marker prevents fixed-point pipelines
        // from versioning that fallback again.
        if (headerBB->getName().find("alias.scalar_fallback") == 0)
            continue;

        std::vector<ir::PhiNode*> headerPhis;
        ir::Instruction* sltCond = nullptr;
        ir::Instruction* brInst = nullptr;

        for (auto& inst : headerBB->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(inst.get())) {
                headerPhis.push_back(phi);
            } else if (inst->getOpcode() == ir::Instruction::Cslt || inst->getOpcode() == ir::Instruction::Csle ||
                       inst->getOpcode() == ir::Instruction::Clt || inst->getOpcode() == ir::Instruction::Csgt ||
                       inst->getOpcode() == ir::Instruction::Csge || inst->getOpcode() == ir::Instruction::Cgt ||
                       inst->getOpcode() == ir::Instruction::Cge) {
                sltCond = inst.get();
            } else if (inst->getOpcode() == ir::Instruction::Br || inst->getOpcode() == ir::Instruction::Jnz) {
                brInst = inst.get();
            }
        }

        if (headerPhis.empty() && !sltCond && !brInst) continue;
        logDiag("candidate " + func.getName() + "/" + headerBB->getName());
        if (headerPhis.empty()) { logDiag("reject: header has no SSA PHI"); continue; }
        if (!sltCond) { logDiag("reject: compare predicate is not canonical signed < (or reversed >)"); continue; }
        if (!brInst) { logDiag("reject: header has no conditional branch"); continue; }
        if (sltCond->getOperands().size() < 2) { logDiag("reject: malformed loop comparison"); continue; }

        ir::BasicBlock* bodyBB = nullptr;
        ir::BasicBlock* exitBB = nullptr;
        if (brInst->getOperands().size() >= 3) {
            bodyBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[1]->get());
            exitBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[2]->get());
        }
        if (!bodyBB || !exitBB) { logDiag("reject: loop does not have one body edge and one exit edge"); continue; }

        ir::BasicBlock* latchBB = bodyBB;
        std::optional<PredicationPlan> predication;
        if (auto* split = terminator(bodyBB); split && split->getOpcode() == ir::Instruction::Br &&
            split->getOperands().size() == 3) {
            PredicationPlan candidate;
            candidate.condition = dynamic_cast<ir::Instruction*>(split->getOperands()[0]->get());
            candidate.thenBlock = dynamic_cast<ir::BasicBlock*>(split->getOperands()[1]->get());
            candidate.elseBlock = dynamic_cast<ir::BasicBlock*>(split->getOperands()[2]->get());
            auto predicate = candidate.condition ? vectorPredicate(candidate.condition->getOpcode()) : std::nullopt;
            auto* thenTerm = terminator(candidate.thenBlock);
            auto* elseTerm = terminator(candidate.elseBlock);
            if (!predicate || !thenTerm || !elseTerm ||
                thenTerm->getOpcode() != ir::Instruction::Jmp ||
                elseTerm->getOpcode() != ir::Instruction::Jmp ||
                thenTerm->getOperands().empty() || elseTerm->getOperands().empty() ||
                thenTerm->getOperands()[0]->get() != elseTerm->getOperands()[0]->get()) {
                logDiag("reject: unsupported internal conditional CFG (requires one reconvergent diamond)");
                continue;
            }
            candidate.predicate = *predicate;
            candidate.lhs = candidate.condition->getOperands()[0]->get();
            candidate.rhs = candidate.condition->getOperands()[1]->get();
            candidate.mergeBlock = dynamic_cast<ir::BasicBlock*>(thenTerm->getOperands()[0]->get());
            auto* mergeTerm = terminator(candidate.mergeBlock);
            if (!candidate.mergeBlock || !mergeTerm || mergeTerm->getOpcode() != ir::Instruction::Jmp ||
                mergeTerm->getOperands().empty() || mergeTerm->getOperands()[0]->get() != headerBB) {
                logDiag("reject: data-dependent loop exit unsupported");
                continue;
            }
            std::string unsafeReason;
            bool safe = true;
            for (ir::BasicBlock* arm : {candidate.thenBlock, candidate.elseBlock}) {
                for (auto& instruction : arm->getInstructions()) {
                    if (!isSpeculativelySafe(instruction.get(), unsafeReason)) { safe = false; break; }
                }
            }
            if (!safe) { logDiag("rejected: " + unsafeReason); continue; }
            for (auto& instruction : candidate.mergeBlock->getInstructions()) {
                if (auto* phi = dynamic_cast<ir::PhiNode*>(instruction.get())) {
                    ir::Value* tv = phi->getIncomingValueForBlock(candidate.thenBlock);
                    ir::Value* fv = phi->getIncomingValueForBlock(candidate.elseBlock);
                    if (tv && fv && !candidate.mergePhi) {
                        candidate.mergePhi = phi; candidate.thenValue = tv; candidate.elseValue = fv;
                    }
                } else if (instruction->getOpcode() == ir::Instruction::Store ||
                           instruction->getOpcode() == ir::Instruction::Stores ||
                           instruction->getOpcode() == ir::Instruction::Stored) {
                    if (instruction->getOperands()[0]->get() == candidate.mergePhi)
                        candidate.store = instruction.get();
                }
            }
            if (!candidate.mergePhi || !candidate.store) {
                logDiag("reject: conditional merge PHI/store pair not recognized");
                continue;
            }
            candidate.scalarResultType = candidate.mergePhi->getType();
            candidate.compareOperandType = candidate.lhs->getType();
            candidate.elementSize = candidate.scalarResultType->getSize();
            candidate.thenExpression = describePredicatedValue(candidate.thenValue);
            candidate.elseExpression = describePredicatedValue(candidate.elseValue);
            if (candidate.thenValue->getType() != candidate.scalarResultType ||
                candidate.elseValue->getType() != candidate.scalarResultType ||
                candidate.lhs->getType() != candidate.rhs->getType()) {
                logDiag("reject: conditional values have incompatible types");
                continue;
            }
            if (candidate.thenExpression.arithmeticCost + candidate.elseExpression.arithmeticCost > 2) {
                logDiag("reject: conditional expressions exceed profitability limit");
                continue;
            }
            latchBB = candidate.mergeBlock;
            predication = candidate;
            logDiag("internal conditional: structured diamond");
            logDiag("merge PHI recognized");
            logDiag("then arm safe"); logDiag("else arm safe");
        }

        ir::BasicBlock* entryBB = nullptr;
        for (auto* pred : headerBB->getPredecessors()) {
            if (pred != latchBB) { entryBB = pred; break; }
        }
        if (!entryBB) { logDiag("reject: no canonical preheader"); continue; }
        if (headerBB->getPredecessors().size() != 2) {
            logDiag("reject: header must have exactly one preheader and one latch");
            continue;
        }
        logDiag("preheader: found; latch: found; exit: found");

        // --- Legality Analysis: Body instructions ---
        bool isLegal = true;
        std::vector<MemoryAccess> memAccesses;
        std::vector<ir::BasicBlock*> scalarBlocks{bodyBB};
        if (predication) {
            scalarBlocks.push_back(predication->thenBlock);
            scalarBlocks.push_back(predication->elseBlock);
            scalarBlocks.push_back(predication->mergeBlock);
        }
        for (ir::BasicBlock* scalarBlock : scalarBlocks) for (auto& inst : scalarBlock->getInstructions()) {
            auto opc = inst->getOpcode();
            if (opc == ir::Instruction::Call || opc == ir::Instruction::ExternCall || opc == ir::Instruction::Syscall ||
                opc == ir::Instruction::Alloc || opc == ir::Instruction::Alloc4 || opc == ir::Instruction::Alloc16) {
                logDiag("Rejected loop: unsupported side-effect opcode in body");
                isLegal = false;
                break;
            }

            if (opc == ir::Instruction::Load || opc == ir::Instruction::Loaduw ||
                opc == ir::Instruction::Loadd || opc == ir::Instruction::Loads ||
                opc == ir::Instruction::Store || opc == ir::Instruction::Stored ||
                opc == ir::Instruction::Stores) {
                MemoryAccess access;
                access.inst = inst.get();
                access.isStore = (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores);
                access.isLoad = !access.isStore;
                memAccesses.push_back(access);
            }
        }
        if (!isLegal) continue;

        VectorizationPlan plan;
        plan.headerBB = headerBB;
        plan.bodyBB = bodyBB;
        plan.latchBB = latchBB;
        plan.preheaderBB = entryBB;
        plan.exitBB = exitBB;
        plan.memoryAccesses = memAccesses;
        plan.predication = predication;

        // 1. Identify Induction Variable & Step
        ir::PhiNode* iPhi = nullptr;
        ir::Instruction* addINextInst = nullptr;

        for (ir::PhiNode* phi : headerPhis) {
            if (!phi->getType() || !phi->getType()->isInteger()) continue;

            ir::Value* preVal = phi->getIncomingValueForBlock(entryBB);
            if (!preVal) continue;

            ir::Value* latchVal = phi->getIncomingValueForBlock(latchBB);
            if (!latchVal) continue;
            auto* latchInst = dynamic_cast<ir::Instruction*>(latchVal);
            if (!latchInst) continue;

            if ((latchInst->getOpcode() == ir::Instruction::Add || latchInst->getOpcode() == ir::Instruction::Sub) &&
                latchInst->getOperands().size() >= 2) {
                ir::Value* op0 = latchInst->getOperands()[0]->get();
                ir::Value* op1 = latchInst->getOperands()[1]->get();
                auto* c1 = dynamic_cast<ir::ConstantInt*>(op1);
                auto* c0 = dynamic_cast<ir::ConstantInt*>(op0);

                if (latchInst->getOpcode() == ir::Instruction::Add) {
                    const ir::ConstantInt* step = op0 == phi ? c1 : (op1 == phi ? c0 : nullptr);
                    if (step) {
                        int64_t stepVal = static_cast<int32_t>(step->getValue());
                        if (stepVal != 0 && std::abs(stepVal) <= INT32_MAX) {
                            iPhi = phi;
                            addINextInst = latchInst;
                            plan.initVal = preVal;
                            plan.stepConst = stepVal;
                            plan.stepInst = latchInst;
                            break;
                        }
                    }
                } else if (latchInst->getOpcode() == ir::Instruction::Sub && op0 == phi && c1) {
                    int64_t stepVal = -static_cast<int32_t>(c1->getValue());
                    if (stepVal != 0 && std::abs(stepVal) <= INT32_MAX) {
                        iPhi = phi;
                        addINextInst = latchInst;
                        plan.initVal = preVal;
                        plan.stepConst = stepVal;
                        plan.stepInst = latchInst;
                        break;
                    }
                }
            }
        }

        if (!iPhi || !addINextInst) {
            logDiag("Rejected loop: positive constant-step induction variable not found");
            continue;
        }

        ir::Value* condOp0 = sltCond->getOperands()[0]->get();
        ir::Value* condOp1 = sltCond->getOperands()[1]->get();
        const bool normalLess = (sltCond->getOpcode() == ir::Instruction::Cslt ||
                                 sltCond->getOpcode() == ir::Instruction::Csle ||
                                 sltCond->getOpcode() == ir::Instruction::Clt) && condOp0 == iPhi;
        const bool normalGreater = (sltCond->getOpcode() == ir::Instruction::Csgt ||
                                    sltCond->getOpcode() == ir::Instruction::Csge ||
                                    sltCond->getOpcode() == ir::Instruction::Cgt ||
                                    sltCond->getOpcode() == ir::Instruction::Cge) && condOp0 == iPhi;
        const bool reversedGreater = sltCond->getOpcode() == ir::Instruction::Csgt && condOp1 == iPhi;

        ir::Value* boundN = nullptr;
        if (plan.stepConst > 0) {
            boundN = normalLess ? condOp1 : (reversedGreater ? condOp0 : nullptr);
        } else {
            boundN = normalGreater ? condOp1 : nullptr;
        }
        if (!boundN) { logDiag("reject: induction is not the varying operand of the loop comparison"); continue; }

        plan.inclusiveBound = sltCond->getOpcode() == ir::Instruction::Csle;
        if (plan.inclusiveBound) {
            auto* constantBound = dynamic_cast<ir::ConstantInt*>(boundN);
            if (!constantBound || static_cast<uint32_t>(constantBound->getValue()) == INT32_MAX) {
                logDiag("reject: inclusive bound cannot be incremented without signed overflow");
                continue;
            }
        }

        plan.indVarPhi = iPhi;
        plan.boundVal = boundN;
        logDiag("induction: found; step: 1; bound: loop invariant input");

        for (auto& access : plan.memoryAccesses) {
            ir::Type* accessType = access.isStore
                ? access.inst->getOperands()[0]->get()->getType()
                : access.inst->getType();
            if (!accessType || !(accessType->isIntegerTy() || accessType->isFloatTy() || accessType->isDoubleTy())) {
                plan.rejectionReason = "unsupported memory element type";
                isLegal = false;
                break;
            }
            if (accessType->isIntegerTy() && accessType->getSize() != 1 &&
                accessType->getSize() != 2 && accessType->getSize() != 4 &&
                accessType->getSize() != 8) {
                plan.rejectionReason = "unsupported integer memory element width";
                isLegal = false;
                break;
            }
            if (plan.mainElemType && plan.mainElemType != accessType) {
                plan.rejectionReason = "mixed memory element types";
                isLegal = false;
                break;
            }
            plan.mainElemType = accessType;
            plan.elementByteSize = accessType->getSize();
            access.elementType = accessType;
            access.elementSize = static_cast<int64_t>(plan.elementByteSize);
        }
        if (!isLegal) { logDiag("reject: " + plan.rejectionReason); continue; }

        for (auto& access : plan.memoryAccesses) {
            if (!normalizeMemoryAccess(access, iPhi)) {
                plan.rejectionReason = "memory address is not base + induction * element size";
                logDiag("reject: " + plan.rejectionReason);
                isLegal = false;
                break;
            }
        }
        if (!isLegal) continue;
        if (!plan.memoryAccesses.empty())
            logDiag("memory: unit stride " + plan.mainElemType->toString());

        plan.memoryLegality = classifyMemory(plan.memoryAccesses);
        for (size_t accessIndex = 0; accessIndex < plan.memoryAccesses.size(); ++accessIndex) {
            const auto& access = plan.memoryAccesses[accessIndex];
            logDiag("access " + std::to_string(accessIndex) + ": " +
                    (access.isStore ? "store" : "load") +
                    " base=" + access.base->getName() +
                    " stride=" + std::to_string(access.stride) +
                    " offset=" + std::to_string(access.constantOffset));
        }
        if (plan.memoryLegality.kind == MemoryLegalityKind::Unsafe) {
            logDiag("Rejected loop: " + plan.memoryLegality.reason);
            continue;
        }
        if (plan.memoryLegality.kind == MemoryLegalityKind::RequiresRuntimeCheck) {
            bool rangesRepresentable = true;
            for (const auto& access : plan.memoryAccesses)
                rangesRepresentable &= access.constantOffset == 0;
            if (!rangesRepresentable) {
                logDiag("Rejected loop: runtime range cannot be constructed overflow-safely");
                continue;
            }
            logDiag("static alias proof: unavailable");
            logDiag("runtime alias versioning required");
            for (const auto& check : plan.memoryLegality.runtimeChecks)
                logDiag("runtime check: " + check.firstBase->getName() + " vs " +
                        check.secondBase->getName());
        } else if (!plan.memoryAccesses.empty()) {
            logDiag("memory legality: safe statically");
        }

        // Runtime disambiguation/setup measured a crossover near 32 i32
        // iterations on the reference x64 host.  Keep known shorter versioned
        // loops scalar; runtime trip counts remain eligible.
        if (plan.memoryLegality.kind == MemoryLegalityKind::RequiresRuntimeCheck) {
            auto* constantBound = dynamic_cast<ir::ConstantInt*>(boundN);
            auto* constantInit = dynamic_cast<ir::ConstantInt*>(plan.initVal);
            if (constantBound && constantInit) {
                const int64_t iterations = static_cast<int64_t>(constantBound->getValue()) -
                                           static_cast<int64_t>(constantInit->getValue());
                if (iterations >= 0 && iterations < 32) {
                    logDiag("Rejected loop: runtime versioning below 32-iteration profitability threshold");
                    continue;
                }
            }
        }

        // Cost model: reject constant trip count < 4
        if (auto* cBound = dynamic_cast<ir::ConstantInt*>(boundN)) {
            if (cBound->getValue() > 0 && cBound->getValue() < 4) {
                logDiag("Rejected loop: trip count too small for vectorization (" + std::to_string(cBound->getValue()) + ")");
                continue;
            }
        }

        // 2. Identify reductions without mutating IR.
        ir::PhiNode* reductionPhi = nullptr;
        for (ir::PhiNode* phi : headerPhis) {
            if (phi != iPhi) { reductionPhi = phi; break; }
        }

        bool unsupportedReduction = false;
        uint64_t mulFactor = 1;
        if (reductionPhi && reductionPhi->getType()) {
            ir::Value* initial = reductionPhi->getIncomingValueForBlock(entryBB);
            auto* update = dynamic_cast<ir::Instruction*>(
                reductionPhi->getIncomingValueForBlock(bodyBB));
            if (initial && update && update->getOperands().size() == 2) {
                ir::Value* lhs = update->getOperands()[0]->get();
                ir::Value* rhs = update->getOperands()[1]->get();
                ir::Value* term = lhs == reductionPhi ? rhs :
                                  (rhs == reductionPhi ? lhs : nullptr);
                ReductionPlan reduction;
                reduction.phi = reductionPhi;
                reduction.initialValue = initial;
                reduction.scalarTerm = term;
                reduction.update = update;
                reduction.scalarType = reductionPhi->getType();

                if (reductionPhi->getType()->isFloatingPoint()) {
                    if (update->getOpcode() == ir::Instruction::FAdd) {
                        reduction.kind = ReductionKind::FAdd;
                        reduction.vectorOpcode = ir::Instruction::VFAdd;
                        reduction.fpIdentity = 0.0;
                        reduction.isFloatingPoint = true;
                    } else if (update->getOpcode() == ir::Instruction::FMul) {
                        reduction.kind = ReductionKind::FMul;
                        reduction.vectorOpcode = ir::Instruction::VFMul;
                        reduction.fpIdentity = 1.0;
                        reduction.isFloatingPoint = true;
                    } else {
                        unsupportedReduction = true;
                        term = nullptr;
                    }
                } else if (reductionPhi->getType()->getSize() == 8) {
                    // Check for i64 sum from signed i32 widening
                    auto* termInst = dynamic_cast<ir::Instruction*>(term);
                    if (termInst && termInst->getOpcode() == ir::Instruction::ExtSW &&
                        update->getOpcode() == ir::Instruction::Add) {
                        ir::Value* extSrc = termInst->getOperands()[0]->get();
                        auto* srcLoad = dynamic_cast<ir::Instruction*>(extSrc);
                        if (srcLoad && (srcLoad->getOpcode() == ir::Instruction::Loaduw ||
                                        srcLoad->getOpcode() == ir::Instruction::Load)) {
                            reduction.kind = ReductionKind::Add;
                            reduction.vectorOpcode = ir::Instruction::VAdd;
                            reduction.identity = 0;
                            reduction.isWidening = true;
                            reduction.conversionOpcode = ir::Instruction::VSExt;
                            reduction.sourceType = extSrc->getType(); // i32
                            plan.isWideningReduction = true;
                            logDiag("signed widening detected; source type i32; accumulator type i64");
                        } else {
                            std::set<ir::Value*> visiting;
                            if (isPureI32Expression(extSrc, iPhi, visiting)) {
                                reduction.kind = ReductionKind::Add;
                                reduction.identity = 0;
                                reduction.isRegisterWidening = true;
                                reduction.sourceType = extSrc->getType();
                                reduction.scalarTerm = term;
                                plan.isRegisterWideningReduction = true;
                                logDiag("signed widening detected for pure i32 register expression");
                            } else {
                                logDiag("Rejected loop: i64 reduction term is not a signed i32 load extension or pure register expression");
                                unsupportedReduction = true;
                            }
                        }
                    } else {
                        logDiag("Rejected loop: i64 reduction is not a supported widening sum pattern");
                        unsupportedReduction = true;
                    }
                } else if (reductionPhi->getType()->getSize() == 4) {
                    switch (update->getOpcode()) {
                        case ir::Instruction::Add:
                            reduction.kind = ReductionKind::Add;
                            reduction.vectorOpcode = ir::Instruction::VAdd;
                            reduction.identity = 0;
                            break;
                        case ir::Instruction::Mul:
                            reduction.kind = ReductionKind::Mul;
                            reduction.vectorOpcode = ir::Instruction::VMul;
                            reduction.identity = 1;
                            break;
                        case ir::Instruction::SMin:
                            reduction.kind = ReductionKind::SignedMin;
                            reduction.vectorOpcode = ir::Instruction::VMin;
                            reduction.identity = INT32_MAX;
                            break;
                        case ir::Instruction::SMax:
                            reduction.kind = ReductionKind::SignedMax;
                            reduction.vectorOpcode = ir::Instruction::VMax;
                            reduction.identity = INT32_MIN;
                            break;
                        case ir::Instruction::Sub:
                        case ir::Instruction::Div:
                            unsupportedReduction = true;
                            term = nullptr;
                            break;
                        default:
                            unsupportedReduction = true;
                            term = nullptr;
                            break;
                    }

                    if (term) {
                        if (auto* termInst = dynamic_cast<ir::Instruction*>(term)) {
                            if (termInst->getOpcode() == ir::Instruction::Mul &&
                                termInst->getOperands()[0]->get() == iPhi) {
                                if (auto* scale = dynamic_cast<ir::ConstantInt*>(termInst->getOperands()[1]->get()))
                                    mulFactor = scale->getValue();
                            }
                        }
                    }
                } else {
                    logDiag("Rejected loop: unsupported integer bitwidth for reduction");
                    unsupportedReduction = true;
                }

                if (!unsupportedReduction && term) {
                    plan.reductions.push_back(reduction);
                    plan.mulScaleFactor = mulFactor;
                }
            }
        }

        if (reductionPhi && plan.reductions.empty())
            unsupportedReduction = true;

        if (unsupportedReduction) {
            logDiag("Rejected loop: non-associative or unsupported reduction");
            continue;
        }

        // Pure-register widening is the new path which overlaps SCEV's
        // existing scalar recurrence language.  Preserve only that overlap;
        // other runtime reductions retain the vectorizer's prior behavior.
        if (plan.isRegisterWideningReduction) {
            ScalarEvolution scalarEvolution;
            if (scalarEvolution.canEliminateClosedForm(func, headerBB)) {
                logDiag("reject: preserving scalar closed-form recurrence for ScalarEvolution");
                continue;
            }
        }

        if (plan.reductions.empty() && plan.memoryAccesses.empty()) {
            logDiag("Rejected loop: no vectorizable reductions or array memory accesses found");
            continue;
        }

        // Target Capability Query & Optimal VF Selection
        auto targetInfo = func.getParent() ? target::TargetResolver::resolve(target_) : nullptr;
        auto ctx = func.getParent()->getContextShared();
        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        if (plan.isRegisterWideningReduction) {
            // The x64 backend already has complete 128-bit i32 arithmetic.
            // Four scalar signed extracts are cheaper and substantially
            // smaller than introducing a new 256-bit widening operation.
            plan.vectorFactor = 4;
            plan.vectorWidthBits = 128;
            plan.mainElemType = i32Ty;
            plan.elementByteSize = 4;
            plan.reductions[0].vectorType = ctx->getVectorType(i32Ty, 4);
            logDiag("pure-register widening reduction: 4xi32 plus four signed scalar extracts");
        } else if (plan.isWideningReduction) {
            auto& reduction = plan.reductions[0];
            ir::VectorType* srcVecTy = ctx->getVectorType(i32Ty, 4);
            ir::VectorType* dstVecTy = ctx->getVectorType(i64Ty, 4);
            ir::VectorType* src8VecTy = ctx->getVectorType(i32Ty, 8);

            logDiag("conversion: signed extend");
            logDiag("i32 -> i64");
            logDiag("source chunk: 8 lanes");
            logDiag("destination: 2 x <4xi64>");
            logDiag("source type i32");
            logDiag("accumulator type i64");
            logDiag("source vector type <8xi32>");
            logDiag("destination vector type <4xi64>");

            bool convSupp = targetInfo && targetInfo->supportsVectorConversion(ir::Instruction::VSExt, srcVecTy, dstVecTy);
            bool addSupp = targetInfo && targetInfo->supportsVectorOperation(ir::Instruction::VAdd, dstVecTy);

            if (convSupp) logDiag("conversion supported");
            if (addSupp) logDiag("vector i64 add supported");

            if (!convSupp || !addSupp) {
                logDiag("Rejected loop: widening conversion or i64 vector add not supported by target");
                continue;
            }

            plan.vectorFactor = 8;
            plan.vectorWidthBits = 256;
            plan.mainElemType = i32Ty;
            plan.elementByteSize = 4;
            reduction.sourceType = i32Ty;
            reduction.scalarType = i64Ty;
            reduction.sourceVectorType = src8VecTy;
            reduction.vectorType = dstVecTy;
            reduction.sourceVF = 8;
            reduction.accumulatorVF = 4;
            reduction.accumulatorsPerChunk = 2;
        } else {
            if (!plan.mainElemType) plan.mainElemType = i32Ty;
            plan.elementByteSize = plan.mainElemType->getSize();
            plan.vectorWidthBits = 0;
            plan.vectorFactor = 0;

            ir::Instruction::Opcode mainVOp = ir::Instruction::VAdd;
            for (auto& inst : bodyBB->getInstructions()) {
                auto opc = inst->getOpcode();
                if (opc == ir::Instruction::Sub) { mainVOp = ir::Instruction::VSub; break; }
                if (opc == ir::Instruction::Mul) { mainVOp = ir::Instruction::VMul; break; }
                if (opc == ir::Instruction::FAdd) { mainVOp = ir::Instruction::VFAdd; break; }
                if (opc == ir::Instruction::FSub) { mainVOp = ir::Instruction::VFSub; break; }
                if (opc == ir::Instruction::FMul) { mainVOp = ir::Instruction::VFMul; break; }
                if (opc == ir::Instruction::FDiv) { mainVOp = ir::Instruction::VFDiv; break; }
            }
            if (!plan.reductions.empty()) mainVOp = plan.reductions[0].vectorOpcode;
            plan.vectorOpcode = mainVOp;

            if (targetInfo) {
                for (unsigned width : {512u, 256u, 128u, 64u}) {
                    if (!width || width % (plan.elementByteSize * 8) != 0) continue;
                    unsigned candidateVF = width / (plan.elementByteSize * 8);
                    auto* candidateType = ctx->getVectorType(plan.mainElemType, candidateVF);
                    if (targetInfo->supportsVectorWidth(width) && targetInfo->supportsVectorType(candidateType) &&
                        targetInfo->supportsVectorOperation(mainVOp, candidateType)) {
                        plan.vectorFactor = candidateVF;
                        plan.vectorWidthBits = width;
                        break;
                    }
                }
            }
            if (!plan.vectorFactor) {
                logDiag("Rejected loop: target has no profitable legal vector width");
                continue;
            }
        }

        if (plan.predication) {
            auto* capabilityType = ctx->getVectorType(plan.mainElemType, plan.vectorFactor);
            if (!targetInfo || !targetInfo->supportsVectorOperation(ir::Instruction::VCmp, capabilityType) ||
                !targetInfo->supportsVectorOperation(ir::Instruction::VSelect, capabilityType)) {
                logDiag("reject: target lacks VCmp/VSelect support");
                continue;
            }
            logDiag("VCmp supported"); logDiag("VSelect supported");
            logDiag("predication legal");
        }

        logDiag("profitability: VF=" + std::to_string(plan.vectorFactor) + " (" + std::to_string(plan.vectorWidthBits) + "-bit) selected");
        plan.legal = true;
        logDiag("plan accepted");

        ir::VectorType* vecTy = ctx->getVectorType(plan.mainElemType, plan.vectorFactor);
        plan.vectorType = vecTy;
        if (plan.predication) {
            plan.predication->vectorResultType = vecTy;
            plan.predication->vectorFactor = plan.vectorFactor;
        }
        if (!plan.reductions.empty()) {
            auto& reduction = plan.reductions[0];
            if (!plan.isWideningReduction) {
                reduction.vectorType = vecTy;
            }
            const char* kind = reduction.kind == ReductionKind::Add ? "add" :
                               reduction.kind == ReductionKind::Mul ? "product" :
                               reduction.kind == ReductionKind::SignedMin ? "signed min" :
                               reduction.kind == ReductionKind::SignedMax ? "signed max" :
                               reduction.kind == ReductionKind::FAdd ? "fadd" : "fmul";
            logDiag(std::string("reduction: ") + kind +
                    "; type: " + reduction.scalarType->toString() +
                    "; identity: " + std::to_string(reduction.identity) +
                    "; horizontal collapse: " + reduction.collapseStrategy);
        }

        ir::IRBuilder builder(ctx);
        builder.setModule(func.getParent());

        // Split the preheader and compute start + floor((N-start)/VF)*VF.
        entryBB->getInstructions().pop_back();
        builder.setInsertPoint(entryBB);

        ir::Instruction* boundNCopy = builder.createCopy(plan.boundVal);
        if (plan.inclusiveBound)
            boundNCopy = builder.createAdd(boundNCopy, ctx->getConstantInt(i32Ty, 1));
        ir::Value* inductionInit = dynamic_cast<ir::ConstantInt*>(plan.initVal)
            ? plan.initVal : static_cast<ir::Value*>(builder.createCopy(plan.initVal));
        ir::Value* postGuardBound = boundNCopy;
        ir::Value* postGuardInit = inductionInit;

        // Reduction initializers can be ABI parameters.  Preserve them before
        // the vector loop introduces temporaries that reuse argument registers;
        // the scalar initializer is combined exactly once after horizontal
        // reduction (or used directly on the no-vector path).
        ir::Value* reductionInit = nullptr;
        if (!plan.reductions.empty())
            reductionInit = builder.createCopy(plan.reductions[0].initialValue);

        // Copy pointer bases in entryBB to ensure stable SSA virtual registers
        std::map<ir::Value*, ir::Value*> baseCopyMap;
        for (auto& ma : plan.memoryAccesses) {
            if (ma.base && !baseCopyMap.count(ma.base)) {
                baseCopyMap[ma.base] = builder.createCopy(ma.base);
            }
        }

        ir::Value* tripCount = boundNCopy;
        ir::Value* nVec = nullptr;
        if (plan.stepConst > 0) {
            auto* constantStart = dynamic_cast<ir::ConstantInt*>(plan.initVal);
            if (!constantStart || constantStart->getValue() != 0)
                tripCount = builder.createSub(boundNCopy, inductionInit);
            if (plan.stepConst != 1) {
                tripCount = builder.createUdiv(
                    builder.createAdd(tripCount, ctx->getConstantInt(i32Ty, plan.stepConst - 1)),
                    ctx->getConstantInt(i32Ty, plan.stepConst));
            }
        } else {
            const int64_t posStep = -plan.stepConst;
            tripCount = builder.createSub(inductionInit, boundNCopy);
            if (posStep != 1) {
                tripCount = builder.createUdiv(
                    builder.createAdd(tripCount, ctx->getConstantInt(i32Ty, posStep - 1)),
                    ctx->getConstantInt(i32Ty, posStep));
            }
        }
        ir::Instruction* hasVec = builder.createCsgt(tripCount, ctx->getConstantInt(i32Ty, plan.vectorFactor - 1));
        ir::Instruction* vectorCount = builder.createAnd(tripCount, ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)plan.vectorFactor)));
        ir::Value* vectorSpan = vectorCount;
        if (plan.stepConst < 0) {
            vectorSpan = builder.createMul(vectorCount, ctx->getConstantInt(i32Ty, (uint64_t)plan.stepConst));
        } else if (plan.stepConst != 1) {
            vectorSpan = builder.createMul(vectorCount, ctx->getConstantInt(i32Ty, plan.stepConst));
        }
        nVec = builder.createAdd(inductionInit, vectorSpan);

        ir::BasicBlock* vPreheaderBB = builder.createBasicBlock("v_preheader", &func);
        ir::BasicBlock* vLoopHeaderBB = builder.createBasicBlock("v_loop_header", &func);
        ir::BasicBlock* vLoopBodyBB = builder.createBasicBlock("v_loop_body", &func);
        ir::BasicBlock* vReductionBB = builder.createBasicBlock("v_reduction", &func);
        ir::BasicBlock* epiHeaderBB = builder.createBasicBlock("epi_header", &func);
        ir::BasicBlock* epiBodyBB = builder.createBasicBlock("epi_body", &func);
        ir::BasicBlock* epiThenBB = nullptr;
        ir::BasicBlock* epiElseBB = nullptr;
        ir::BasicBlock* epiMergeBB = nullptr;
        if (plan.predication) {
            epiThenBB = builder.createBasicBlock("epi_pred_then", &func);
            epiElseBB = builder.createBasicBlock("epi_pred_else", &func);
            epiMergeBB = builder.createBasicBlock("epi_pred_merge", &func);
        }
        ir::BasicBlock* epiLatchBB = plan.predication ? epiMergeBB : epiBodyBB;
        ir::BasicBlock* aliasCheckBB = nullptr;
        if (plan.memoryLegality.kind == MemoryLegalityKind::RequiresRuntimeCheck)
            aliasCheckBB = builder.createBasicBlock("alias.runtime_check", &func);

        builder.createBr(hasVec, aliasCheckBB ? aliasCheckBB : vPreheaderBB,
                         epiHeaderBB);

        if (aliasCheckBB) {
            // Runtime ranges are half-open [base + start*size,
            // base + bound*size).  Versioning is intentionally constrained to
            // nonnegative offsets; a dynamic negative start makes the runtime
            // guard false. Unsigned monotonicity checks make every pointer
            // addition overflow-safe.
            bool rangesRepresentable = true;
            for (const auto& access : plan.memoryAccesses)
                rangesRepresentable &= access.constantOffset >= 0;
            if (!rangesRepresentable) {
                logDiag("Rejected loop: runtime range cannot be constructed overflow-safely");
                // No IR has escaped yet except newly appended blocks.  Keep the
                // original scalar loop by abandoning this candidate before CFG
                // reconstruction; remove the appended empty blocks below.
                auto removeNewBlock = [&](ir::BasicBlock* target) {
                    for (auto it = func.getBasicBlocks().begin();
                         it != func.getBasicBlocks().end(); ++it) {
                        if (it->get() == target) { func.getBasicBlocks().erase(it); break; }
                    }
                };
                removeNewBlock(aliasCheckBB);
                removeNewBlock(epiBodyBB);
                removeNewBlock(epiHeaderBB);
                removeNewBlock(vReductionBB);
                removeNewBlock(vLoopBodyBB);
                removeNewBlock(vLoopHeaderBB);
                removeNewBlock(vPreheaderBB);
                continue;
            }

            builder.setInsertPoint(aliasCheckBB);
            ir::Instruction* trip64 = builder.createExtUW(tripCount, i64Ty);
            ir::Instruction* byteLength = builder.createMul(
                trip64, ctx->getConstantInt(i64Ty, plan.elementByteSize));
            ir::Instruction* guardBound = builder.createCopy(boundNCopy);
            ir::Instruction* bound64ForRange = builder.createExtUW(guardBound, i64Ty);
            ir::Instruction* endOffset = builder.createMul(
                bound64ForRange, ctx->getConstantInt(i64Ty, plan.elementByteSize));
            ir::Instruction* startNonnegative = builder.createCsge(
                inductionInit, ctx->getConstantInt(i32Ty, 0));

            ir::Value* allSafe = nullptr;
            for (const auto& check : plan.memoryLegality.runtimeChecks) {
                ir::Value* first = builder.createCopy(baseCopyMap.at(check.firstBase));
                ir::Value* second = builder.createCopy(baseCopyMap.at(check.secondBase));
                ir::Instruction* firstBeforeSecond = builder.createCule(first, second);
                ir::Instruction* secondBeforeFirst = builder.createCule(second, first);
                ir::Instruction* firstGap = builder.createSub(second, first);
                ir::Instruction* secondGap = builder.createSub(first, second);
                ir::Instruction* firstDisjoint = builder.createAnd(
                    firstBeforeSecond, builder.createCuge(firstGap, byteLength));
                ir::Instruction* secondDisjoint = builder.createAnd(
                    secondBeforeFirst, builder.createCuge(secondGap, byteLength));
                ir::Instruction* disjoint = builder.createOr(firstDisjoint, secondDisjoint);
                // byteLength is at most UINT32_MAX*8.  Requiring the wrapped
                // distance to address zero to cover it prevents end overflow.
                ir::Instruction* firstCapacity = builder.createSub(
                    ctx->getConstantInt(i64Ty, 0), first);
                ir::Instruction* secondCapacity = builder.createSub(
                    ctx->getConstantInt(i64Ty, 0), second);
                ir::Instruction* capacitiesValid = builder.createAnd(
                    builder.createCuge(firstCapacity, endOffset),
                    builder.createCuge(secondCapacity, endOffset));
                ir::Instruction* valid = builder.createAnd(startNonnegative, capacitiesValid);
                ir::Instruction* safe = builder.createAnd(valid, disjoint);
                allSafe = allSafe ? static_cast<ir::Value*>(builder.createAnd(allSafe, safe))
                                  : static_cast<ir::Value*>(safe);
            }
            // Materialize fresh values after the guard.  They have no live
            // range through the guard itself, avoiding destructive reuse by
            // scalar two-address lowering while dominating both successors.
            for (const auto& copiedBase : baseCopyMap) {
                ir::Instruction* preserved = builder.createCeq(
                    copiedBase.second, copiedBase.second);
                allSafe = builder.createAnd(allSafe, preserved);
            }
            postGuardBound = builder.createCopy(boundNCopy);
            if (!dynamic_cast<ir::ConstantInt*>(inductionInit))
                postGuardInit = builder.createCopy(inductionInit);
            builder.createBr(allSafe, vPreheaderBB, headerBB);

            // The fallback is the untouched original loop.  Redirect every
            // preheader PHI edge to the alias-check block and mark the header
            // so subsequent fixed-point iterations cannot version it again.
            for (ir::PhiNode* phi : headerPhis) {
                ir::Value* incoming = phi->getIncomingValueForBlock(entryBB);
                if (phi == iPhi) incoming = postGuardInit;
                phi->removeIncomingValue(entryBB);
                phi->addIncoming(incoming, aliasCheckBB);
            }
            for (auto& operand : sltCond->getOperands())
                if (operand->get() == plan.boundVal) operand->set(postGuardBound);
            headerBB->setName("alias.scalar_fallback." + headerBB->getName());
            bodyBB->setName("alias.scalar_fallback.body." + bodyBB->getName());
            logDiag("runtime check emitted: " +
                    std::to_string(plan.memoryLegality.runtimeChecks.size()) +
                    " deduplicated conflict(s)");
            logDiag("scalar fallback preserved");
        }

        // Vector Preheader
        builder.setInsertPoint(vPreheaderBB);
        uint32_t startValConst = 0;
        if (auto* cStart = dynamic_cast<ir::ConstantInt*>(plan.initVal)) {
            startValConst = static_cast<uint32_t>(cStart->getValue());
        }

        auto buildVectorConst = [&](uint32_t val0, uint32_t valStep) -> ir::VectorInstruction* {
            ir::Instruction* buf = builder.createAlloc(ctx->getConstantInt(i64Ty, plan.vectorWidthBits / 8), i64Ty);
            ir::Value* lanePtr = buf;
            for (unsigned k = 0; k < plan.vectorFactor; ++k) {
                builder.createStore(ctx->getConstantInt(i32Ty, val0 + k * valStep), lanePtr);
                if (k + 1 < plan.vectorFactor)
                    lanePtr = builder.createAdd(lanePtr, ctx->getConstantInt(i64Ty, 4));
            }
            // Scalar Add is lowered destructively, so recover the base from the
            // final running pointer instead of keeping the original SSA value
            // live across all address updates.
            ir::Value* loadBase = lanePtr;
            if (plan.vectorFactor > 1)
                loadBase = builder.createSub(lanePtr,
                    ctx->getConstantInt(i64Ty, (plan.vectorFactor - 1) * 4));
            return builder.createVLoad(vecTy, loadBase);
        };

        std::map<ir::Value*, ir::Value*> predicationConstants;
        if (plan.predication) {
            std::set<ir::Value*> constants;
            std::set<ir::Value*> visitedValues;
            std::function<void(ir::Value*)> collectConstants = [&](ir::Value* value) {
                if (!value || !visitedValues.insert(value).second) return;
                if (dynamic_cast<ir::Constant*>(value)) { constants.insert(value); return; }
                auto* instruction = dynamic_cast<ir::Instruction*>(value);
                if (!instruction) return;
                if (instruction->getOpcode() == ir::Instruction::Load ||
                    instruction->getOpcode() == ir::Instruction::Loaduw ||
                    instruction->getOpcode() == ir::Instruction::Loads ||
                    instruction->getOpcode() == ir::Instruction::Loadd) return;
                for (auto& operand : instruction->getOperands()) collectConstants(operand->get());
            };
            collectConstants(plan.predication->lhs); collectConstants(plan.predication->rhs);
            collectConstants(plan.predication->thenValue); collectConstants(plan.predication->elseValue);
            for (ir::Value* constant : constants) {
                if (auto* integer = dynamic_cast<ir::ConstantInt*>(constant);
                    integer && integer->getValue() != 0)
                    predicationConstants[constant] = buildVectorConst(integer->getValue(), 0);
            }
        }

        ir::VectorInstruction* vInitI = nullptr;
        ir::VectorInstruction* vStep = nullptr;
        ir::VectorInstruction* vScale = nullptr;
        ir::Instruction* registerLaneBuffer = nullptr;
        std::map<ir::Value*, ir::Value*> registerConstants;
        if (plan.memoryAccesses.empty() && !plan.isWideningReduction) {
            if (dynamic_cast<ir::ConstantInt*>(inductionInit)) {
                vInitI = buildVectorConst(startValConst, plan.stepConst);
            } else {
                ir::VectorInstruction* laneOffsets = buildVectorConst(0, plan.stepConst);
                ir::VectorInstruction* startBroadcast = builder.createVBroadcast(vecTy, inductionInit);
                vInitI = builder.createVAdd(startBroadcast, laneOffsets);
            }
            vStep = buildVectorConst(plan.vectorFactor * plan.stepConst, 0);
            if (!plan.isRegisterWideningReduction)
                vScale = buildVectorConst((uint32_t)mulFactor, 0);
            if (plan.isRegisterWideningReduction)
                registerLaneBuffer = builder.createAlloc(ctx->getConstantInt(i64Ty, 16), i64Ty);
        }
        if (plan.isRegisterWideningReduction) {
            auto* extension = dynamic_cast<ir::Instruction*>(plan.reductions[0].scalarTerm);
            std::set<ir::Value*> seen;
            std::function<void(ir::Value*)> materializeConstants = [&](ir::Value* value) {
                if (!value || !seen.insert(value).second) return;
                if (auto* constant = dynamic_cast<ir::ConstantInt*>(value)) {
                    ir::VectorInstruction* loaded = buildVectorConst(
                        static_cast<uint32_t>(constant->getValue()), 0);
                    registerConstants[value] = loaded->getOperands()[0]->get();
                    loaded->getParent()->getInstructions().pop_back();
                    return;
                }
                if (auto* inst = dynamic_cast<ir::Instruction*>(value))
                    for (auto& operand : inst->getOperands()) materializeConstants(operand->get());
            };
            if (extension) materializeConstants(extension->getOperands()[0]->get());
        }

        ir::VectorInstruction* vReductionIdentity = nullptr;
        ir::VectorInstruction* vZeroAcc0 = nullptr;
        ir::VectorInstruction* vZeroAcc1 = nullptr;

        if (!plan.reductions.empty()) {
            if (plan.isWideningReduction) {
                ir::VectorType* v4i64Ty = ctx->getVectorType(i64Ty, 4);
                vZeroAcc0 = builder.createVBroadcast(v4i64Ty, ctx->getConstantInt(i64Ty, 0));
                vZeroAcc1 = builder.createVBroadcast(v4i64Ty, ctx->getConstantInt(i64Ty, 0));
            } else if (!plan.isRegisterWideningReduction) {
                if (plan.reductions[0].isFloatingPoint) {
                    ir::Value* fpConst = ctx->getConstantFP(
                        plan.reductions[0].scalarType, plan.reductions[0].fpIdentity);
                    vReductionIdentity = builder.createVBroadcast(vecTy, fpConst);
                } else {
                    vReductionIdentity = builder.createVBroadcast(
                        vecTy, ctx->getConstantInt(i32Ty,
                            static_cast<uint32_t>(plan.reductions[0].identity)));
                }
            }
        }

        ir::Value* lateTripCount = postGuardBound;
        if (plan.stepConst > 0) {
            auto* constantStart = dynamic_cast<ir::ConstantInt*>(plan.initVal);
            if (!constantStart || constantStart->getValue() != 0)
                lateTripCount = builder.createSub(postGuardBound, postGuardInit);
            if (plan.stepConst != 1) {
                lateTripCount = builder.createUdiv(
                    builder.createAdd(lateTripCount, ctx->getConstantInt(i32Ty, plan.stepConst - 1)),
                    ctx->getConstantInt(i32Ty, plan.stepConst));
            }
        } else {
            const int64_t posStep = -plan.stepConst;
            lateTripCount = builder.createSub(postGuardInit, postGuardBound);
            if (posStep != 1) {
                lateTripCount = builder.createUdiv(
                    builder.createAdd(lateTripCount, ctx->getConstantInt(i32Ty, posStep - 1)),
                    ctx->getConstantInt(i32Ty, posStep));
            }
        }
        ir::Instruction* lateVectorCount = builder.createAnd(
            lateTripCount,
            ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)plan.vectorFactor)));
        ir::Value* lateVectorSpan = lateVectorCount;
        if (plan.stepConst < 0) {
            lateVectorSpan = builder.createMul(lateVectorCount, ctx->getConstantInt(i32Ty, (uint64_t)plan.stepConst));
        } else if (plan.stepConst != 1) {
            lateVectorSpan = builder.createMul(lateVectorCount, ctx->getConstantInt(i32Ty, plan.stepConst));
        }
        nVec = builder.createAdd(postGuardInit, lateVectorSpan);

        builder.createJmp(vLoopHeaderBB);

        // Vector Header
        builder.setInsertPoint(vLoopHeaderBB);
        ir::PhiNode* rawPhiVI = nullptr;
        if (vInitI) {
            auto phiVI = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
            rawPhiVI = phiVI.get();
            vLoopHeaderBB->getInstructions().push_back(std::move(phiVI));
            rawPhiVI->addIncoming(vInitI, vPreheaderBB);
        }

        ir::PhiNode* rawPhiVSum0 = nullptr;
        ir::PhiNode* rawPhiVSum1 = nullptr;
        ir::PhiNode* rawPhiVSum = nullptr;
        ir::PhiNode* rawPhiRegisterSum = nullptr;

        if (!plan.reductions.empty()) {
            if (plan.isRegisterWideningReduction) {
                auto owner = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, vLoopHeaderBB);
                rawPhiRegisterSum = owner.get();
                vLoopHeaderBB->getInstructions().push_back(std::move(owner));
                rawPhiRegisterSum->addIncoming(reductionInit, vPreheaderBB);
            } else if (plan.isWideningReduction) {
                ir::VectorType* v4i64Ty = ctx->getVectorType(i64Ty, 4);
                auto phiVSum0 = std::make_unique<ir::PhiNode>(v4i64Ty, 0, nullptr, vLoopHeaderBB);
                rawPhiVSum0 = phiVSum0.get();
                vLoopHeaderBB->getInstructions().push_back(std::move(phiVSum0));
                rawPhiVSum0->addIncoming(vZeroAcc0, vPreheaderBB);

                auto phiVSum1 = std::make_unique<ir::PhiNode>(v4i64Ty, 0, nullptr, vLoopHeaderBB);
                rawPhiVSum1 = phiVSum1.get();
                vLoopHeaderBB->getInstructions().push_back(std::move(phiVSum1));
                rawPhiVSum1->addIncoming(vZeroAcc1, vPreheaderBB);
            } else {
                auto phiVSum = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
                rawPhiVSum = phiVSum.get();
                vLoopHeaderBB->getInstructions().push_back(std::move(phiVSum));
                rawPhiVSum->addIncoming(vReductionIdentity, vPreheaderBB);
            }
        }

        auto phiICnt = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiICnt = phiICnt.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiICnt));

        rawPhiICnt->addIncoming(postGuardInit, vPreheaderBB);

        std::map<ir::Value*, ir::Value*> vectorBaseMap;
        for (const auto& copiedBase : baseCopyMap) {
            auto owner = std::make_unique<ir::PhiNode>(copiedBase.second->getType(), 0,
                                                       nullptr, vLoopHeaderBB);
            ir::PhiNode* phi = owner.get();
            vLoopHeaderBB->getInstructions().push_front(std::move(owner));
            phi->addIncoming(copiedBase.second, vPreheaderBB);
            phi->addIncoming(phi, vLoopBodyBB);
            vectorBaseMap[copiedBase.first] = phi;
        }

        ir::Instruction* vCond = plan.stepConst > 0
            ? builder.createCslt(rawPhiICnt, nVec)
            : builder.createCsgt(rawPhiICnt, nVec);
        builder.createBr(vCond, vLoopBodyBB, vReductionBB);

        // Vector Body
        builder.setInsertPoint(vLoopBodyBB);

        if (plan.isRegisterWideningReduction) {
            std::map<ir::Value*, ir::Value*> values;
            values[iPhi] = rawPhiVI;
            std::map<uint32_t, ir::Value*> affineInductions;
            affineInductions[0] = rawPhiVI;
            ir::Value* oneAddress = nullptr;
            for (const auto& entry : registerConstants) {
                auto* constant = dynamic_cast<ir::ConstantInt*>(entry.first);
                if (constant && static_cast<uint32_t>(constant->getValue()) == 1) {
                    oneAddress = entry.second;
                    break;
                }
            }
            ir::Value* vectorOne = nullptr;
            auto affineInduction = [&](uint32_t offset) -> ir::Value* {
                if (!oneAddress || offset > 32) return nullptr;
                if (!vectorOne) vectorOne = builder.createVLoad(vecTy, oneAddress);
                for (uint32_t laneOffset = 1; laneOffset <= offset; ++laneOffset) {
                    if (!affineInductions.count(laneOffset))
                        affineInductions[laneOffset] = builder.createVAdd(
                            affineInductions[laneOffset - 1], vectorOne);
                }
                return affineInductions[offset];
            };
            std::function<ir::Value*(ir::Value*)> vectorize = [&](ir::Value* value) -> ir::Value* {
                if (values.count(value)) return values[value];
                if (registerConstants.count(value))
                    return builder.createVLoad(vecTy, registerConstants[value]);
                auto* inst = dynamic_cast<ir::Instruction*>(value);
                if (!inst || inst->getOperands().size() != 2) return nullptr;
                if (inst->getOpcode() == ir::Instruction::Add) {
                    for (unsigned inductionOperand = 0; inductionOperand != 2; ++inductionOperand) {
                        auto* offset = dynamic_cast<ir::ConstantInt*>(
                            inst->getOperands()[1 - inductionOperand]->get());
                        if (inst->getOperands()[inductionOperand]->get() == iPhi && offset) {
                            if (ir::Value* affine = affineInduction(
                                    static_cast<uint32_t>(offset->getValue()))) {
                                values[value] = affine;
                                return affine;
                            }
                        }
                    }
                }
                ir::Value* lhs = vectorize(inst->getOperands()[0]->get());
                ir::Value* rhs = vectorize(inst->getOperands()[1]->get());
                if (!lhs || !rhs) return nullptr;
                if (inst->getOpcode() == ir::Instruction::Add) values[value] = builder.createVAdd(lhs, rhs);
                else if (inst->getOpcode() == ir::Instruction::Sub) values[value] = builder.createVSub(lhs, rhs);
                else if (inst->getOpcode() == ir::Instruction::Mul) values[value] = builder.createVMul(lhs, rhs);
                return values.count(value) ? values[value] : nullptr;
            };
            auto* extension = dynamic_cast<ir::Instruction*>(plan.reductions[0].scalarTerm);
            ir::Value* expression = extension ? extension->getOperands()[0]->get() : nullptr;
            ir::Value* vectorExpression = vectorize(expression);
            builder.createVStore(vectorExpression, registerLaneBuffer);
            ir::Value* next = rawPhiRegisterSum;
            for (unsigned lane = 0; lane < 4; ++lane) {
                ir::Value* address = lane == 0 ? static_cast<ir::Value*>(registerLaneBuffer)
                    : static_cast<ir::Value*>(builder.createAdd(registerLaneBuffer,
                        ctx->getConstantInt(i64Ty, lane * 4)));
                ir::Instruction* lane32 = builder.createLoaduw(address);
                ir::Instruction* lane64 = builder.createExtSW(lane32, i64Ty);
                next = builder.createAdd(next, lane64);
            }
            rawPhiRegisterSum->addIncoming(next, vLoopBodyBB);
        } else if (plan.isWideningReduction) {
            ir::VectorType* v8i32Ty = ctx->getVectorType(i32Ty, 8);
            ir::VectorType* v4i64Ty = ctx->getVectorType(i64Ty, 4);

            ir::Instruction* i64ICnt = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset = builder.createMul(
                i64ICnt, ctx->getConstantInt(i64Ty, 4));

            ir::Value* basePtr = nullptr;
            for (auto& inst : bodyBB->getInstructions()) {
                if (inst->getOpcode() == ir::Instruction::Loaduw || inst->getOpcode() == ir::Instruction::Load) {
                    basePtr = extractBasePointer(inst->getOperands()[0]->get());
                    break;
                }
            }
            ir::Value* safeBase = vectorBaseMap.count(basePtr) ? vectorBaseMap[basePtr] : basePtr;
            ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
            ir::VectorInstruction* vLd8 = builder.createVLoad(v8i32Ty, vPtr);

            ir::VectorInstruction* vLow = builder.createVSExt(vLd8, v4i64Ty);
            ir::VectorInstruction* vHighShuf = builder.createVShuffle(
                vLd8, vLd8, ir::ShuffleMask({4, 5, 6, 7, 4, 5, 6, 7}, 8));
            ir::VectorInstruction* vHigh = builder.createVSExt(vHighShuf, v4i64Ty);

            ir::VectorInstruction* vNextSum0 = builder.createVAdd(rawPhiVSum0, vLow);
            ir::VectorInstruction* vNextSum1 = builder.createVAdd(rawPhiVSum1, vHigh);

            rawPhiVSum0->addIncoming(vNextSum0, vLoopBodyBB);
            rawPhiVSum1->addIncoming(vNextSum1, vLoopBodyBB);
        } else if (plan.predication) {
            std::map<ir::Value*, ir::Value*> values;
            ir::Instruction* wideIndex = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset = builder.createMul(wideIndex,
                ctx->getConstantInt(i64Ty, plan.elementByteSize));
            auto vectorValue = [&](ir::Value* value) -> ir::Value* {
                if (values.count(value)) return values[value];
                if (predicationConstants.count(value)) return predicationConstants[value];
                if (auto* integer = dynamic_cast<ir::ConstantInt*>(value);
                    integer && integer->getValue() == 0) {
                    for (const auto& entry : values) {
                        if (dynamic_cast<ir::VectorType*>(entry.second->getType())) {
                            values[value] = builder.createVSub(entry.second, entry.second);
                            return values[value];
                        }
                    }
                }
                return nullptr;
            };
            for (ir::BasicBlock* scalarBlock : scalarBlocks) for (auto& owner : scalarBlock->getInstructions()) {
                ir::Instruction* inst = owner.get(); auto opc = inst->getOpcode();
                if (inst == addINextInst || opc == ir::Instruction::Br || opc == ir::Instruction::Jmp) continue;
                if (opc == ir::Instruction::Load || opc == ir::Instruction::Loaduw ||
                    opc == ir::Instruction::Loads || opc == ir::Instruction::Loadd) {
                    ir::Value* base = extractBasePointer(inst->getOperands()[0]->get());
                    ir::Value* safeBase = vectorBaseMap.count(base) ? vectorBaseMap[base] : base;
                    values[inst] = builder.createVLoad(vecTy, builder.createAdd(safeBase, byteOffset));
                } else if (vectorPredicate(opc)) {
                    ir::Value* lhs = vectorValue(inst->getOperands()[0]->get());
                    ir::Value* rhs = vectorValue(inst->getOperands()[1]->get());
                    if (lhs && rhs) values[inst] = builder.createVCmp(lhs, rhs, *vectorPredicate(opc));
                } else if (opc == ir::Instruction::Add || opc == ir::Instruction::Sub ||
                           opc == ir::Instruction::Mul || opc == ir::Instruction::FAdd ||
                           opc == ir::Instruction::FSub || opc == ir::Instruction::FMul) {
                    ir::Value* lhs = vectorValue(inst->getOperands()[0]->get());
                    ir::Value* rhs = vectorValue(inst->getOperands()[1]->get());
                    if (!lhs || !rhs) continue;
                    if (opc == ir::Instruction::Add) values[inst] = builder.createVAdd(lhs, rhs);
                    else if (opc == ir::Instruction::Sub) values[inst] = builder.createVSub(lhs, rhs);
                    else if (opc == ir::Instruction::Mul) values[inst] = builder.createVMul(lhs, rhs);
                    else if (opc == ir::Instruction::FAdd) values[inst] = builder.createVFAdd(lhs, rhs);
                    else if (opc == ir::Instruction::FSub) values[inst] = builder.createVFSub(lhs, rhs);
                    else values[inst] = builder.createVFMul(lhs, rhs);
                } else if (opc == ir::Instruction::Neg) {
                    ir::Value* operand = vectorValue(inst->getOperands()[0]->get());
                    if (operand) values[inst] = builder.createVSub(
                        builder.createVBroadcast(vecTy, ctx->getConstantInt(i32Ty, 0)), operand);
                } else if (auto* phi = dynamic_cast<ir::PhiNode*>(inst); phi == plan.predication->mergePhi) {
                    ir::Value* mask = vectorValue(plan.predication->condition);
                    ir::Value* yes = vectorValue(plan.predication->thenValue);
                    ir::Value* no = vectorValue(plan.predication->elseValue);
                    if (mask && yes && no) values[phi] = builder.createVSelect(mask, yes, no);
                } else if (inst == plan.predication->store) {
                    ir::Value* selected = vectorValue(plan.predication->mergePhi);
                    ir::Value* base = extractBasePointer(inst->getOperands()[1]->get());
                    ir::Value* safeBase = vectorBaseMap.count(base) ? vectorBaseMap[base] : base;
                    builder.createVStore(selected, builder.createAdd(safeBase, byteOffset));
                }
            }
        } else if (!plan.memoryAccesses.empty()) {
            std::map<ir::Instruction*, ir::Value*> vValueMap;
            ir::Instruction* i64ICnt = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset = builder.createMul(
                i64ICnt, ctx->getConstantInt(i64Ty, plan.elementByteSize));

            auto lanePointer = [&](ir::Value* base, unsigned lane) -> ir::Value* {
                if (lane == 0) return builder.createAdd(base, byteOffset);
                ir::Value* laneIndex = builder.createAdd(rawPhiICnt,
                    ctx->getConstantInt(i32Ty, lane * plan.stepConst));
                ir::Value* wideLane = builder.createExtSW(laneIndex, i64Ty);
                ir::Value* laneOffset = builder.createMul(wideLane,
                    ctx->getConstantInt(i64Ty, plan.elementByteSize));
                return builder.createAdd(base, laneOffset);
            };
            auto scalarizedGather = [&](ir::Instruction* scalarLoad, ir::Value* base) -> ir::Value* {
                ir::Value* packed = nullptr;
                for (unsigned lane = 0; lane < plan.vectorFactor; ++lane) {
                    ir::Value* pointer = lanePointer(base, lane);
                    ir::Instruction* value = scalarLoad->getOpcode() == ir::Instruction::Loads
                        ? builder.createLoads(pointer)
                        : scalarLoad->getOpcode() == ir::Instruction::Loadd
                            ? builder.createLoadd(pointer) : builder.createLoad(pointer);
                    if (!packed) packed = builder.createVBroadcast(vecTy, value);
                    else packed = builder.createVInsert(packed, value,
                        ctx->getConstantInt(i32Ty, lane));
                }
                return packed;
            };

            for (auto& inst : bodyBB->getInstructions()) {
                auto opc = inst->getOpcode();
                if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load ||
                    opc == ir::Instruction::Loads || opc == ir::Instruction::Loadd) {
                    ir::Value* ptr = inst->getOperands()[0]->get();
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = vectorBaseMap.count(basePtr) ? vectorBaseMap[basePtr] : basePtr;
                    if (plan.stepConst == 1) {
                        ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                        vValueMap[inst.get()] = builder.createVLoad(vecTy, vPtr);
                    } else {
                        vValueMap[inst.get()] = scalarizedGather(inst.get(), safeBase);
                    }
                } else if (!plan.reductions.empty() && inst.get() == plan.reductions[0].update) {
                    auto& reduction = plan.reductions[0];
                    auto* termInst = dynamic_cast<ir::Instruction*>(reduction.scalarTerm);
                    ir::Value* vectorTerm = termInst && vValueMap.count(termInst)
                        ? vValueMap[termInst] : nullptr;
                    if (!vectorTerm) {
                        logDiag("reject during transform: reduction term was not vectorized");
                        continue;
                    }
                    ir::VectorInstruction* next = nullptr;
                    switch (reduction.kind) {
                        case ReductionKind::Add: next = builder.createVAdd(rawPhiVSum, vectorTerm); break;
                        case ReductionKind::Mul: next = builder.createVMul(rawPhiVSum, vectorTerm); break;
                        case ReductionKind::SignedMin: next = builder.createVMin(rawPhiVSum, vectorTerm); break;
                        case ReductionKind::SignedMax: next = builder.createVMax(rawPhiVSum, vectorTerm); break;
                        case ReductionKind::FAdd: next = builder.createVFAdd(rawPhiVSum, vectorTerm); break;
                        case ReductionKind::FMul: next = builder.createVFMul(rawPhiVSum, vectorTerm); break;
                    }
                    vValueMap[inst.get()] = next;
                    rawPhiVSum->addIncoming(next, vLoopBodyBB);
                } else if ((opc == ir::Instruction::Add || opc == ir::Instruction::FAdd) && inst.get() != addINextInst) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vAdd = opc == ir::Instruction::FAdd
                            ? builder.createVFAdd(vOp0, vOp1) : builder.createVAdd(vOp0, vOp1);
                        vValueMap[inst.get()] = vAdd;
                    } else if (rawPhiVSum) {
                        ir::Value* ldVal = vOp0 ? vOp0 : vOp1;
                        if (ldVal) {
                            ir::VectorInstruction* vAddSum = builder.createVAdd(rawPhiVSum, ldVal);
                            vValueMap[inst.get()] = vAddSum;
                            rawPhiVSum->addIncoming(vAddSum, vLoopBodyBB);
                        }
                    }
                } else if (opc == ir::Instruction::Sub || opc == ir::Instruction::FSub) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vSub = opc == ir::Instruction::FSub
                            ? builder.createVFSub(vOp0, vOp1) : builder.createVSub(vOp0, vOp1);
                        vValueMap[inst.get()] = vSub;
                    }
                } else if (opc == ir::Instruction::Mul || opc == ir::Instruction::FMul ||
                           opc == ir::Instruction::FDiv) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vResult = nullptr;
                        if (opc == ir::Instruction::FMul) vResult = builder.createVFMul(vOp0, vOp1);
                        else if (opc == ir::Instruction::FDiv) vResult = builder.createVFDiv(vOp0, vOp1);
                        else vResult = builder.createVMul(vOp0, vOp1);
                        vValueMap[inst.get()] = vResult;
                    }
                } else if (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores) {
                    ir::Value* valToStore = inst->getOperands()[0]->get();
                    ir::Value* ptrToStore = inst->getOperands()[1]->get();

                    auto* instVal = dynamic_cast<ir::Instruction*>(valToStore);
                    ir::Value* vVal = (instVal && vValueMap.count(instVal)) ? vValueMap[instVal] : valToStore;

                    ir::Value* basePtr = extractBasePointer(ptrToStore);
                    ir::Value* safeBase = vectorBaseMap.count(basePtr) ? vectorBaseMap[basePtr] : basePtr;
                    if (plan.stepConst == 1) {
                        ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                        builder.createVStore(vVal, vPtr);
                    } else {
                        for (unsigned lane = 0; lane < plan.vectorFactor; ++lane) {
                            ir::Value* scalar = builder.createVExtract(vVal,
                                ctx->getConstantInt(i32Ty, lane));
                            ir::Value* pointer = lanePointer(safeBase, lane);
                            if (opc == ir::Instruction::Stores) builder.createStores(scalar, pointer);
                            else if (opc == ir::Instruction::Stored) builder.createStored(scalar, pointer);
                            else builder.createStore(scalar, pointer);
                        }
                    }
                }
            }
        } else if (rawPhiVSum) {
            ir::VectorInstruction* vTerm = builder.createVMul(rawPhiVI, vScale);
            ir::VectorInstruction* vSumNext = builder.createVAdd(rawPhiVSum, vTerm);
            rawPhiVSum->addIncoming(vSumNext, vLoopBodyBB);
        }

        ir::Instruction* iCntNext = builder.createAdd(rawPhiICnt,
            ctx->getConstantInt(i32Ty, plan.vectorFactor * plan.stepConst));

        if (rawPhiVI) {
            ir::VectorInstruction* vINext = builder.createVAdd(rawPhiVI, vStep);
            rawPhiVI->addIncoming(vINext, vLoopBodyBB);
        }
        rawPhiICnt->addIncoming(iCntNext, vLoopBodyBB);

        builder.createJmp(vLoopHeaderBB);

        // Vector Reduction Block
        builder.setInsertPoint(vReductionBB);
        auto createScalarReduction = [&](ir::Value* lhs, ir::Value* rhs,
                                         ReductionKind kind) -> ir::Instruction* {
            switch (kind) {
                case ReductionKind::Add: return builder.createAdd(lhs, rhs);
                case ReductionKind::Mul: return builder.createMul(lhs, rhs);
                case ReductionKind::SignedMin: return builder.createSMin(lhs, rhs);
                case ReductionKind::SignedMax: return builder.createSMax(lhs, rhs);
                case ReductionKind::FAdd: return builder.createFAdd(lhs, rhs);
                case ReductionKind::FMul: return builder.createFMul(lhs, rhs);
            }
            return nullptr;
        };
        ir::Instruction* sumReduced = nullptr;
        ir::Value* vectorPathSum = nullptr;

        if (plan.isRegisterWideningReduction && rawPhiRegisterSum) {
            vectorPathSum = rawPhiRegisterSum;
        } else if (plan.isWideningReduction && rawPhiVSum0 && rawPhiVSum1) {
            ir::VectorInstruction* vCombined = builder.createVAdd(rawPhiVSum0, rawPhiVSum1);
            ir::Instruction* redBuf = builder.createAlloc(ctx->getConstantInt(i64Ty, 32), i64Ty);
            builder.createVStore(vCombined, redBuf);

            ir::Instruction* l0 = builder.createLoadl(redBuf);
            ir::Instruction* l1 = builder.createLoadl(builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 8)));
            ir::Instruction* l2 = builder.createLoadl(builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 16)));
            ir::Instruction* l3 = builder.createLoadl(builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 24)));

            ir::Instruction* s01 = builder.createAdd(l0, l1);
            ir::Instruction* s23 = builder.createAdd(l2, l3);
            ir::Instruction* sumRed64 = builder.createAdd(s01, s23);

            vectorPathSum = builder.createAdd(reductionInit, sumRed64);
        } else if (!plan.reductions.empty() && rawPhiVSum) {
            ir::Instruction* redBuf = builder.createAlloc(ctx->getConstantInt(i64Ty, plan.vectorWidthBits / 8), i64Ty);
            builder.createVStore(rawPhiVSum, redBuf);

            const bool isFP = plan.reductions[0].isFloatingPoint;
            const bool isFloat = plan.reductions[0].scalarType && plan.reductions[0].scalarType->isFloatTy();

            auto loadLane = [&](ir::Value* ptr) -> ir::Instruction* {
                if (isFP) {
                    return isFloat ? builder.createLoads(ptr) : builder.createLoadd(ptr);
                }
                return builder.createLoaduw(ptr);
            };

            const size_t elemByteSize = isFP ? (isFloat ? 4 : 8) : 4;
            sumReduced = loadLane(redBuf);
            for (unsigned lane = 1; lane < plan.vectorFactor; ++lane) {
                ir::Instruction* pOff = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, lane * elemByteSize));
                ir::Instruction* laneVal = loadLane(pOff);
                sumReduced = createScalarReduction(sumReduced, laneVal,
                                                   plan.reductions[0].kind);
            }
            vectorPathSum = createScalarReduction(reductionInit, sumReduced,
                                                  plan.reductions[0].kind);
        }

        builder.createJmp(epiHeaderBB);

        // Epilogue Header
        builder.setInsertPoint(epiHeaderBB);
        auto phiEpiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiI = phiEpiI.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiI));

        auto phiEpiBoundOwner = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr,
                                                              epiHeaderBB);
        ir::PhiNode* rawPhiEpiBound = phiEpiBoundOwner.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiBoundOwner));
        rawPhiEpiBound->addIncoming(boundNCopy, entryBB);
        rawPhiEpiBound->addIncoming(postGuardBound, vReductionBB);
        rawPhiEpiBound->addIncoming(rawPhiEpiBound, epiLatchBB);

        std::map<ir::Value*, ir::Value*> epiBaseMap;
        for (const auto& copiedBase : baseCopyMap) {
            auto owner = std::make_unique<ir::PhiNode>(copiedBase.second->getType(), 0,
                                                       nullptr, epiHeaderBB);
            ir::PhiNode* phi = owner.get();
            epiHeaderBB->getInstructions().push_back(std::move(owner));
            phi->addIncoming(copiedBase.second, entryBB);
            phi->addIncoming(copiedBase.second, vReductionBB);
            phi->addIncoming(phi, epiLatchBB);
            epiBaseMap[copiedBase.first] = phi;
        }

        ir::PhiNode* rawPhiEpiSum = nullptr;
        if (!plan.reductions.empty()) {
            ir::Type* sumTy = plan.reductions[0].scalarType;
            auto phiEpiSum = std::make_unique<ir::PhiNode>(sumTy, 0, nullptr, epiHeaderBB);
            rawPhiEpiSum = phiEpiSum.get();
            epiHeaderBB->getInstructions().push_back(std::move(phiEpiSum));

            rawPhiEpiSum->addIncoming(reductionInit, entryBB);
            if (vectorPathSum) {
                rawPhiEpiSum->addIncoming(vectorPathSum, vReductionBB);
            } else {
                rawPhiEpiSum->addIncoming(reductionInit, vReductionBB);
            }
        }

        rawPhiEpiI->addIncoming(inductionInit, entryBB);
        rawPhiEpiI->addIncoming(nVec, vReductionBB);

        ir::Instruction* epiCond = plan.stepConst > 0
            ? builder.createCslt(rawPhiEpiI, rawPhiEpiBound)
            : builder.createCsgt(rawPhiEpiI, rawPhiEpiBound);
        builder.createBr(epiCond, epiBodyBB, exitBB);

        // Epilogue Body
        builder.setInsertPoint(epiBodyBB);

        // Recreate original scalar body in epilogue for remainder iterations
        std::map<ir::Instruction*, ir::Instruction*> epiValueMap;
        if (!plan.predication) for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (inst.get() == addINextInst) continue;
            if (!plan.reductions.empty() && inst.get() == plan.reductions[0].update) continue;
            if (!plan.reductions.empty() && plan.memoryAccesses.empty() &&
                !plan.isRegisterWideningReduction &&
                inst.get() == plan.reductions[0].scalarTerm) continue;

            if (opc == ir::Instruction::ExtSW) {
                ir::Value* srcVal = inst->getOperands()[0]->get();
                if (srcVal == iPhi) {
                    ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                    epiValueMap[inst.get()] = epiI64;
                } else {
                    auto* instSrc = dynamic_cast<ir::Instruction*>(srcVal);
                    ir::Value* eSrc = (instSrc && epiValueMap.count(instSrc)) ? epiValueMap[instSrc] : srcVal;
                    ir::Instruction* extVal = builder.createExtSW(eSrc, i64Ty);
                    epiValueMap[inst.get()] = extVal;
                }
            } else if (opc == ir::Instruction::Mul || opc == ir::Instruction::FMul ||
                       opc == ir::Instruction::FDiv) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                ir::Value* eOp0 = op0 == iPhi ? static_cast<ir::Value*>(rawPhiEpiI)
                    : ((inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0);
                ir::Value* eOp1 = op1;
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                if (inst1 && epiValueMap.count(inst1)) eOp1 = epiValueMap[inst1];
                ir::Instruction* epiResult = opc == ir::Instruction::FMul ? builder.createFMul(eOp0, eOp1)
                    : (opc == ir::Instruction::FDiv ? builder.createFDiv(eOp0, eOp1)
                                                    : builder.createMul(eOp0, eOp1));
                epiValueMap[inst.get()] = epiResult;
            } else if (opc == ir::Instruction::Add || opc == ir::Instruction::FAdd) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                ir::Value* eOp0 = op0 == iPhi ? static_cast<ir::Value*>(rawPhiEpiI)
                    : ((inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0);
                ir::Value* eOp1 = op1 == iPhi ? static_cast<ir::Value*>(rawPhiEpiI)
                    : ((inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1);
                ir::Instruction* epiAdd = opc == ir::Instruction::FAdd
                    ? builder.createFAdd(eOp0, eOp1) : builder.createAdd(eOp0, eOp1);
                epiValueMap[inst.get()] = epiAdd;
            } else if (opc == ir::Instruction::Sub || opc == ir::Instruction::FSub) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                ir::Value* eOp0 = op0 == iPhi ? static_cast<ir::Value*>(rawPhiEpiI)
                    : ((inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0);
                ir::Value* eOp1 = op1 == iPhi ? static_cast<ir::Value*>(rawPhiEpiI)
                    : ((inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1);
                ir::Instruction* epiSub = opc == ir::Instruction::FSub
                    ? builder.createFSub(eOp0, eOp1) : builder.createSub(eOp0, eOp1);
                epiValueMap[inst.get()] = epiSub;
            } else if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load ||
                       opc == ir::Instruction::Loads || opc == ir::Instruction::Loadd) {
                ir::Value* ptr = inst->getOperands()[0]->get();
                ir::Instruction* ptrAdd = dynamic_cast<ir::Instruction*>(ptr);
                ir::Value* ePtr = ptr;
                if (ptrAdd && ptrAdd->getOpcode() == ir::Instruction::Add) {
                    ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                    ir::Instruction* byteOff = builder.createMul(
                        epiI64, ctx->getConstantInt(i64Ty, plan.elementByteSize));
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = epiBaseMap.count(basePtr) ? epiBaseMap[basePtr] : basePtr;
                    ePtr = builder.createAdd(safeBase, byteOff);
                }
                ir::Instruction* epiLd = opc == ir::Instruction::Loads ? builder.createLoads(ePtr)
                    : (opc == ir::Instruction::Loadd ? builder.createLoadd(ePtr)
                                                     : builder.createLoaduw(ePtr));
                epiValueMap[inst.get()] = epiLd;
            } else if (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores) {
                ir::Value* valToStore = inst->getOperands()[0]->get();
                ir::Value* ptrToStore = inst->getOperands()[1]->get();
                auto* instVal = dynamic_cast<ir::Instruction*>(valToStore);
                ir::Value* eVal = (instVal && epiValueMap.count(instVal)) ? epiValueMap[instVal] : valToStore;

                ir::Instruction* ptrAdd = dynamic_cast<ir::Instruction*>(ptrToStore);
                ir::Value* ePtr = ptrToStore;
                if (ptrAdd && ptrAdd->getOpcode() == ir::Instruction::Add) {
                    ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                    ir::Instruction* byteOff = builder.createMul(
                        epiI64, ctx->getConstantInt(i64Ty, plan.elementByteSize));
                    ir::Value* basePtr = extractBasePointer(ptrToStore);
                    ir::Value* safeBase = epiBaseMap.count(basePtr) ? epiBaseMap[basePtr] : basePtr;
                    ePtr = builder.createAdd(safeBase, byteOff);
                }
                if (opc == ir::Instruction::Stores) builder.createStores(eVal, ePtr);
                else if (opc == ir::Instruction::Stored) builder.createStored(eVal, ePtr);
                else builder.createStore(eVal, ePtr);
            }
        } else {
            std::function<ir::Value*(ir::Value*)> cloneValue = [&](ir::Value* value) -> ir::Value* {
                auto* inst = dynamic_cast<ir::Instruction*>(value);
                if (!inst) return value;
                if (epiValueMap.count(inst)) return epiValueMap[inst];
                auto op = inst->getOpcode();
                ir::Instruction* result = nullptr;
                if (op == ir::Instruction::Load || op == ir::Instruction::Loaduw ||
                    op == ir::Instruction::Loads || op == ir::Instruction::Loadd) {
                    ir::Value* base = extractBasePointer(inst->getOperands()[0]->get());
                    ir::Value* safeBase = epiBaseMap.count(base) ? epiBaseMap[base] : base;
                    ir::Value* wide = builder.createExtSW(rawPhiEpiI, i64Ty);
                    ir::Value* offset = builder.createMul(wide, ctx->getConstantInt(i64Ty, plan.elementByteSize));
                    ir::Value* pointer = builder.createAdd(safeBase, offset);
                    result = op == ir::Instruction::Loads ? builder.createLoads(pointer) :
                             (op == ir::Instruction::Loadd ? builder.createLoadd(pointer) : builder.createLoaduw(pointer));
                } else if (op == ir::Instruction::Add || op == ir::Instruction::Sub ||
                           op == ir::Instruction::Mul || op == ir::Instruction::FAdd ||
                           op == ir::Instruction::FSub || op == ir::Instruction::FMul) {
                    ir::Value* lhs = cloneValue(inst->getOperands()[0]->get());
                    ir::Value* rhs = cloneValue(inst->getOperands()[1]->get());
                    result = op == ir::Instruction::Add ? builder.createAdd(lhs, rhs) :
                             op == ir::Instruction::Sub ? builder.createSub(lhs, rhs) :
                             op == ir::Instruction::Mul ? builder.createMul(lhs, rhs) :
                             op == ir::Instruction::FAdd ? builder.createFAdd(lhs, rhs) :
                             op == ir::Instruction::FSub ? builder.createFSub(lhs, rhs) : builder.createFMul(lhs, rhs);
                } else if (op == ir::Instruction::Neg) {
                    result = builder.createNeg(cloneValue(inst->getOperands()[0]->get()));
                } else if (vectorPredicate(op)) {
                    ir::Value* lhs = cloneValue(inst->getOperands()[0]->get());
                    ir::Value* rhs = cloneValue(inst->getOperands()[1]->get());
                    switch (op) {
                        case ir::Instruction::Ceq: result = builder.createCeq(lhs, rhs); break;
                        case ir::Instruction::Cne: result = builder.createCne(lhs, rhs); break;
                        case ir::Instruction::Cslt: result = builder.createCslt(lhs, rhs); break;
                        case ir::Instruction::Csle: result = builder.createCsle(lhs, rhs); break;
                        case ir::Instruction::Csgt: result = builder.createCsgt(lhs, rhs); break;
                        case ir::Instruction::Csge: result = builder.createCsge(lhs, rhs); break;
                        case ir::Instruction::Ceqf: result = builder.createCeqf(lhs, rhs); break;
                        case ir::Instruction::Cnef: result = builder.createCnef(lhs, rhs); break;
                        case ir::Instruction::Clt: result = builder.createClt(lhs, rhs); break;
                        case ir::Instruction::Cle: result = builder.createCle(lhs, rhs); break;
                        case ir::Instruction::Cgt: result = builder.createCgt(lhs, rhs); break;
                        case ir::Instruction::Cge: result = builder.createCge(lhs, rhs); break;
                        default: break;
                    }
                }
                if (result) epiValueMap[inst] = result;
                return result;
            };
            ir::Value* scalarCondition = cloneValue(plan.predication->condition);
            builder.createBr(scalarCondition, epiThenBB, epiElseBB);
            builder.setInsertPoint(epiThenBB);
            ir::Value* thenValue = cloneValue(plan.predication->thenValue);
            builder.createJmp(epiMergeBB);
            builder.setInsertPoint(epiElseBB);
            ir::Value* elseValue = cloneValue(plan.predication->elseValue);
            builder.createJmp(epiMergeBB);
            builder.setInsertPoint(epiMergeBB);
            auto selectedOwner = std::make_unique<ir::PhiNode>(plan.mainElemType, 0, nullptr, epiMergeBB);
            ir::PhiNode* selected = selectedOwner.get();
            epiMergeBB->getInstructions().push_back(std::move(selectedOwner));
            selected->addIncoming(thenValue, epiThenBB);
            selected->addIncoming(elseValue, epiElseBB);
            ir::Value* storeBase = extractBasePointer(plan.predication->store->getOperands()[1]->get());
            ir::Value* safeStoreBase = epiBaseMap.count(storeBase) ? epiBaseMap[storeBase] : storeBase;
            ir::Value* wide = builder.createExtSW(rawPhiEpiI, i64Ty);
            ir::Value* offset = builder.createMul(wide, ctx->getConstantInt(i64Ty, plan.elementByteSize));
            ir::Value* pointer = builder.createAdd(safeStoreBase, offset);
            if (plan.mainElemType->isFloatTy()) builder.createStores(selected, pointer);
            else if (plan.mainElemType->isDoubleTy()) builder.createStored(selected, pointer);
            else builder.createStore(selected, pointer);
        }

        ir::Instruction* epiINext = builder.createAdd(rawPhiEpiI, ctx->getConstantInt(i32Ty, (uint64_t)plan.stepConst));
        rawPhiEpiI->addIncoming(epiINext, epiLatchBB);

        if (rawPhiEpiSum) {
            ir::Instruction* epiSumNext = nullptr;
            if (!plan.reductions.empty() && plan.reductions[0].scalarTerm) {
                auto* origTermInst = dynamic_cast<ir::Instruction*>(plan.reductions[0].scalarTerm);
                if (origTermInst && epiValueMap.count(origTermInst)) {
                    ir::Value* epiTerm = epiValueMap[origTermInst];
                    epiSumNext = createScalarReduction(rawPhiEpiSum, epiTerm, plan.reductions[0].kind);
                }
            }
            if (!epiSumNext && !plan.memoryAccesses.empty() && plan.reductions[0].update) {
                for (auto& inst : bodyBB->getInstructions()) {
                    if (inst->getOpcode() == ir::Instruction::Loaduw || inst->getOpcode() == ir::Instruction::Load) {
                        if (epiValueMap.count(inst.get())) {
                            epiSumNext = createScalarReduction(rawPhiEpiSum, epiValueMap[inst.get()],
                                                               plan.reductions[0].kind);
                            break;
                        }
                    }
                }
            }
            if (!epiSumNext) {
                ir::Instruction* epiTerm = builder.createMul(rawPhiEpiI, ctx->getConstantInt(i32Ty, mulFactor));
                epiSumNext = createScalarReduction(rawPhiEpiSum, epiTerm,
                                                   plan.reductions[0].kind);
            }
            rawPhiEpiSum->addIncoming(epiSumNext, epiBodyBB);

            for (auto& inst : exitBB->getInstructions()) {
                for (auto& operand : inst->getOperands())
                    if (operand->get() == reductionPhi) operand->set(rawPhiEpiSum);
            }
        }

        // Explicit latch copies keep loop-invariant values live past every
        // derived address/store in the scalar tail.  The allocator's PHI-edge
        // model otherwise permits a store pointer to reuse the bound/base
        // register before the backedge.
        rawPhiEpiBound->setIncomingValueForBlock(
            epiLatchBB, builder.createCopy(rawPhiEpiBound));
        for (const auto& basePhi : epiBaseMap) {
            auto* phi = static_cast<ir::PhiNode*>(basePhi.second);
            phi->setIncomingValueForBlock(epiLatchBB, builder.createCopy(phi));
        }

        builder.createJmp(epiHeaderBB);

        if (aliasCheckBB) {
            // Keep each fallback base explicitly loop-carried.  This both
            // documents its invariance and prevents destructive scalar address
            // lowering from reusing the base register for a derived pointer.
            auto aliasTerminator = aliasCheckBB->getInstructions().end();
            --aliasTerminator;
            builder.setInsertPoint(aliasCheckBB, aliasTerminator);
            for (const auto& copiedBase : baseCopyMap) {
                auto owner = std::make_unique<ir::PhiNode>(copiedBase.second->getType(), 0,
                                                           nullptr, headerBB);
                ir::PhiNode* phi = owner.get();
                headerBB->getInstructions().push_front(std::move(owner));
                phi->addIncoming(copiedBase.second, aliasCheckBB);
                phi->addIncoming(phi, latchBB);
                for (auto& access : plan.memoryAccesses) {
                    if (access.base != copiedBase.first) continue;
                    ir::Value* pointer = access.isStore
                        ? access.inst->getOperands()[1]->get()
                        : access.inst->getOperands()[0]->get();
                    replaceAddressBase(pointer, copiedBase.first, phi);
                }
            }
        }

        auto removeBB = [&](ir::BasicBlock* target) {
            for (auto it = func.getBasicBlocks().begin(); it != func.getBasicBlocks().end(); ++it) {
                if (it->get() == target) {
                    func.getBasicBlocks().erase(it);
                    break;
                }
            }
        };

        if (!aliasCheckBB) {
            removeBB(headerBB);
            removeBB(bodyBB);
        }

        CFGBuilder::run(func);
        changed = true;
        logDiag("loop vectorized: " + func.getName() + " (VF=" + std::to_string(plan.vectorFactor) + ")");
        break;
    }

    return changed;
}

} // namespace transforms
