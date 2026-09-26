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
#include <cmath>

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
    unsigned unrollFactor = 1;          // 1x or 2x unrolled
    unsigned vectorWidthBits = 256;
    ir::Type* mainElemType = nullptr;
    ir::VectorType* vectorType = nullptr;
    size_t elementByteSize = 0;
    ir::Instruction::Opcode vectorOpcode = ir::Instruction::VAdd;
    uint64_t mulScaleFactor = 1;
    bool isWideningReduction = false;
    bool isRegisterWideningReduction = false;
    bool inclusiveBound = false;
    bool enableAlignmentGuard = false;
};

struct LoopCostModel {
    int scalarInstCount = 0;
    int vectorInstCount = 0;
    unsigned estimatedRegPressure = 0;
    bool isProfitable = true;
    unsigned selectedUnrollFactor = 1;

    static LoopCostModel evaluate(const VectorizationPlan& plan, unsigned vf, unsigned targetMaxRegs = 16) {
        LoopCostModel cost;
        int numMemOps = static_cast<int>(plan.memoryAccesses.size());
        int numReductions = static_cast<int>(plan.reductions.size());

        cost.scalarInstCount = (numMemOps * 2) + (numReductions * 2) + 3;

        // Try 2x unroll if loop body is small and has no heavy predication
        if (!plan.predication && (numMemOps + numReductions <= 6) && vf >= 4) {
            cost.selectedUnrollFactor = 2;
        } else {
            cost.selectedUnrollFactor = 1;
        }

        cost.vectorInstCount = (numMemOps * cost.selectedUnrollFactor) + (numReductions * cost.selectedUnrollFactor) + 2;

        // Register pressure estimation:
        // Live vector registers: induction (1), accumulators (numReductions * unroll), memory temporaries (numMemOps * unroll), scratch (2)
        cost.estimatedRegPressure = 1 + (numReductions * cost.selectedUnrollFactor) + (numMemOps * cost.selectedUnrollFactor) + 2;

        if (cost.estimatedRegPressure > targetMaxRegs - 2 && cost.selectedUnrollFactor > 1) {
            // High register pressure -> fallback to 1x unrolling
            cost.selectedUnrollFactor = 1;
            cost.estimatedRegPressure = 1 + numReductions + numMemOps + 2;
        }

        cost.isProfitable = (cost.scalarInstCount * static_cast<int>(vf * cost.selectedUnrollFactor)) > (cost.vectorInstCount + 2);
        return cost;
    }
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
                continue;

            if (first.base == second.base) {
                const int64_t byteDelta = second.constantOffset - first.constantOffset;
                const int64_t distance = byteDelta / first.elementSize;
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
    bool allowFPReassociate = allowFPReassociate_ || (std::getenv("FYRA_FAST_MATH") != nullptr);

    logDiag("Analyzing function: " + func.getName() + " (FP reassociate: " + (allowFPReassociate ? "allowed" : "strict") + ")");

    for (auto bbIt = func.getBasicBlocks().begin(); bbIt != func.getBasicBlocks().end(); ++bbIt) {
        ir::BasicBlock* headerBB = bbIt->get();

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
                logDiag("reject: unsupported internal conditional CFG");
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
            latchBB = candidate.mergeBlock;
            predication = candidate;
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
            logDiag("Rejected loop: constant-step induction variable not found");
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

        plan.inclusiveBound = sltCond->getOpcode() == ir::Instruction::Csle ||
                              sltCond->getOpcode() == ir::Instruction::Csge;
        if (plan.inclusiveBound) {
            auto* constantBound = dynamic_cast<ir::ConstantInt*>(boundN);
            if (!constantBound || static_cast<uint32_t>(constantBound->getValue()) == INT32_MAX ||
                (plan.stepConst < 0 && static_cast<int32_t>(constantBound->getValue()) == INT32_MIN)) {
                logDiag("reject: inclusive bound cannot be adjusted without signed overflow");
                continue;
            }
        }

        plan.indVarPhi = iPhi;
        plan.boundVal = boundN;

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

        plan.memoryLegality = classifyMemory(plan.memoryAccesses);
        if (plan.memoryLegality.kind == MemoryLegalityKind::Unsafe) {
            logDiag("Rejected loop: " + plan.memoryLegality.reason);
            continue;
        }

        // 2. Identify reductions
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
                    if (!allowFPReassociate) {
                        logDiag("reject: FP reduction requires FP reassociation permission (strict IEEE mode active)");
                        unsupportedReduction = true;
                    } else if (update->getOpcode() == ir::Instruction::FAdd) {
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
                    }
                } else if (reductionPhi->getType()->getSize() == 8) {
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
                            reduction.sourceType = extSrc->getType();
                            plan.isWideningReduction = true;
                        } else {
                            unsupportedReduction = true;
                        }
                    } else {
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
                        default:
                            unsupportedReduction = true;
                            break;
                    }
                    if (term && dynamic_cast<ir::Instruction*>(term)) {
                        auto* termInst = dynamic_cast<ir::Instruction*>(term);
                        if (termInst->getOpcode() == ir::Instruction::Mul &&
                            termInst->getOperands()[0]->get() == iPhi) {
                            if (auto* scale = dynamic_cast<ir::ConstantInt*>(termInst->getOperands()[1]->get()))
                                mulFactor = scale->getValue();
                        }
                    }
                } else {
                    unsupportedReduction = true;
                }

                if (!unsupportedReduction && term) {
                    plan.reductions.push_back(reduction);
                    plan.mulScaleFactor = mulFactor;
                }
            }
        }

        if (reductionPhi && plan.reductions.empty()) unsupportedReduction = true;
        if (unsupportedReduction) {
            logDiag("Rejected loop: non-associative or unsupported reduction");
            continue;
        }

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

        // Target Capability Query & VF / Cost Model Evaluation
        auto targetInfo = func.getParent() ? target::TargetResolver::resolve(target_) : nullptr;
        auto ctx = func.getParent()->getContextShared();
        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        if (plan.isRegisterWideningReduction) {
            plan.vectorFactor = 4;
            plan.vectorWidthBits = 128;
            plan.mainElemType = i32Ty;
            plan.elementByteSize = 4;
            plan.reductions[0].vectorType = ctx->getVectorType(i32Ty, 4);
            plan.unrollFactor = 1;
        } else if (plan.isWideningReduction) {
            auto& reduction = plan.reductions[0];
            ir::VectorType* srcVecTy = ctx->getVectorType(i32Ty, 4);
            ir::VectorType* dstVecTy = ctx->getVectorType(i64Ty, 4);
            ir::VectorType* src8VecTy = ctx->getVectorType(i32Ty, 8);

            bool convSupp = targetInfo && targetInfo->supportsVectorConversion(ir::Instruction::VSExt, srcVecTy, dstVecTy);
            bool addSupp = targetInfo && targetInfo->supportsVectorOperation(ir::Instruction::VAdd, dstVecTy);
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
            plan.unrollFactor = 1;
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

            // Cost Model Evaluation
            LoopCostModel cost = LoopCostModel::evaluate(plan, plan.vectorFactor);
            if (!cost.isProfitable) {
                logDiag("Rejected loop: cost model evaluated vectorization unprofitable");
                continue;
            }
            plan.unrollFactor = cost.selectedUnrollFactor;
            logDiag("Cost model selected VF=" + std::to_string(plan.vectorFactor) + " unrollFactor=" + std::to_string(plan.unrollFactor));
        }

        if (plan.predication) {
            auto* capabilityType = ctx->getVectorType(plan.mainElemType, plan.vectorFactor);
            if (!targetInfo || !targetInfo->supportsVectorOperation(ir::Instruction::VCmp, capabilityType) ||
                !targetInfo->supportsVectorOperation(ir::Instruction::VSelect, capabilityType)) {
                logDiag("reject: target lacks VCmp/VSelect support");
                continue;
            }
        }

        plan.legal = true;
        logDiag("plan accepted: VF=" + std::to_string(plan.vectorFactor) + " unroll=" + std::to_string(plan.unrollFactor));

        ir::VectorType* vecTy = ctx->getVectorType(plan.mainElemType, plan.vectorFactor);
        plan.vectorType = vecTy;
        if (plan.predication) {
            plan.predication->vectorResultType = vecTy;
            plan.predication->vectorFactor = plan.vectorFactor;
        }
        if (!plan.reductions.empty() && !plan.isWideningReduction) {
            plan.reductions[0].vectorType = vecTy;
        }

        ir::IRBuilder builder(ctx);
        builder.setModule(func.getParent());

        // Split preheader
        entryBB->getInstructions().pop_back();
        builder.setInsertPoint(entryBB);

        ir::Instruction* boundNCopy = builder.createCopy(plan.boundVal);
        if (plan.inclusiveBound) {
            if (plan.stepConst > 0)
                boundNCopy = builder.createAdd(boundNCopy, ctx->getConstantInt(i32Ty, 1));
            else
                boundNCopy = builder.createSub(boundNCopy, ctx->getConstantInt(i32Ty, 1));
        }
        ir::Value* inductionInit = dynamic_cast<ir::ConstantInt*>(plan.initVal)
            ? plan.initVal : static_cast<ir::Value*>(builder.createCopy(plan.initVal));
        ir::Value* postGuardBound = boundNCopy;
        ir::Value* postGuardInit = inductionInit;

        ir::Value* reductionInit = nullptr;
        if (!plan.reductions.empty())
            reductionInit = builder.createCopy(plan.reductions[0].initialValue);

        std::map<ir::Value*, ir::Value*> baseCopyMap;
        for (auto& ma : plan.memoryAccesses) {
            if (ma.base && !baseCopyMap.count(ma.base)) {
                baseCopyMap[ma.base] = builder.createCopy(ma.base);
            }
        }

        const unsigned totalStepElements = plan.vectorFactor * plan.unrollFactor;

        ir::Value* tripCount = boundNCopy;
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

        ir::Instruction* hasVec = builder.createCsgt(tripCount, ctx->getConstantInt(i32Ty, totalStepElements - 1));
        ir::Instruction* vectorCount = builder.createAnd(tripCount, ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)totalStepElements)));
        ir::Value* vectorSpan = vectorCount;
        if (plan.stepConst < 0) {
            vectorSpan = builder.createMul(vectorCount, ctx->getConstantInt(i32Ty, (uint64_t)plan.stepConst));
        } else if (plan.stepConst != 1) {
            vectorSpan = builder.createMul(vectorCount, ctx->getConstantInt(i32Ty, plan.stepConst));
        }
        ir::Value* nVec = builder.createAdd(inductionInit, vectorSpan);

        ir::BasicBlock* vPreheaderBB = builder.createBasicBlock("v_preheader", &func);
        ir::BasicBlock* vLoopHeaderBB = builder.createBasicBlock("v_loop_header", &func);
        ir::BasicBlock* vLoopBodyBB = builder.createBasicBlock("v_loop_body", &func);
        ir::BasicBlock* vReductionBB = builder.createBasicBlock("v_reduction", &func);
        ir::BasicBlock* epiHeaderBB = builder.createBasicBlock("epi_header", &func);
        ir::BasicBlock* epiBodyBB = builder.createBasicBlock("epi_body", &func);
        ir::BasicBlock* epiLatchBB = epiBodyBB;

        ir::BasicBlock* aliasCheckBB = nullptr;
        if (plan.memoryLegality.kind == MemoryLegalityKind::RequiresRuntimeCheck)
            aliasCheckBB = builder.createBasicBlock("alias.runtime_check", &func);

        builder.createBr(hasVec, aliasCheckBB ? aliasCheckBB : vPreheaderBB, epiHeaderBB);

        if (aliasCheckBB) {
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
            postGuardBound = builder.createCopy(boundNCopy);
            if (!dynamic_cast<ir::ConstantInt*>(inductionInit))
                postGuardInit = builder.createCopy(inductionInit);
            builder.createBr(allSafe, vPreheaderBB, headerBB);

            for (ir::PhiNode* phi : headerPhis) {
                ir::Value* incoming = phi->getIncomingValueForBlock(entryBB);
                if (phi == iPhi) incoming = postGuardInit;
                phi->removeIncomingValue(entryBB);
                phi->addIncoming(incoming, aliasCheckBB);
            }
            headerBB->setName("alias.scalar_fallback." + headerBB->getName());
        }

        // Vector Preheader
        builder.setInsertPoint(vPreheaderBB);
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
                        vecTy, ctx->getConstantInt(i32Ty, static_cast<uint32_t>(plan.reductions[0].identity)));
                }
            }
        }

        builder.createJmp(vLoopHeaderBB);

        // Vector Header
        builder.setInsertPoint(vLoopHeaderBB);
        ir::PhiNode* rawPhiVSum0 = nullptr;
        ir::PhiNode* rawPhiVSum1 = nullptr;

        if (!plan.reductions.empty() && !plan.isWideningReduction && !plan.isRegisterWideningReduction) {
            auto phi0 = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
            rawPhiVSum0 = phi0.get();
            vLoopHeaderBB->getInstructions().push_back(std::move(phi0));
            rawPhiVSum0->addIncoming(vReductionIdentity, vPreheaderBB);

            if (plan.unrollFactor == 2) {
                auto phi1 = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
                rawPhiVSum1 = phi1.get();
                vLoopHeaderBB->getInstructions().push_back(std::move(phi1));
                rawPhiVSum1->addIncoming(vReductionIdentity, vPreheaderBB);
            }
        } else if (plan.isWideningReduction) {
            ir::VectorType* v4i64Ty = ctx->getVectorType(i64Ty, 4);
            auto phi0 = std::make_unique<ir::PhiNode>(v4i64Ty, 0, nullptr, vLoopHeaderBB);
            rawPhiVSum0 = phi0.get();
            vLoopHeaderBB->getInstructions().push_back(std::move(phi0));
            rawPhiVSum0->addIncoming(vZeroAcc0, vPreheaderBB);

            auto phi1 = std::make_unique<ir::PhiNode>(v4i64Ty, 0, nullptr, vLoopHeaderBB);
            rawPhiVSum1 = phi1.get();
            vLoopHeaderBB->getInstructions().push_back(std::move(phi1));
            rawPhiVSum1->addIncoming(vZeroAcc1, vPreheaderBB);
        }

        auto phiICnt = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiICnt = phiICnt.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiICnt));
        rawPhiICnt->addIncoming(postGuardInit, vPreheaderBB);

        std::map<ir::Value*, ir::Value*> vectorBaseMap;
        for (const auto& copiedBase : baseCopyMap) {
            auto owner = std::make_unique<ir::PhiNode>(copiedBase.second->getType(), 0, nullptr, vLoopHeaderBB);
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

        if (!plan.memoryAccesses.empty() && !plan.isWideningReduction) {
            std::map<ir::Instruction*, ir::Value*> vValueMapChunk0;
            std::map<ir::Instruction*, ir::Value*> vValueMapChunk1;

            ir::Instruction* i64ICnt0 = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset0 = builder.createMul(i64ICnt0, ctx->getConstantInt(i64Ty, plan.elementByteSize));

            ir::Instruction* iCntChunk1 = builder.createAdd(rawPhiICnt, ctx->getConstantInt(i32Ty, plan.vectorFactor * plan.stepConst));
            ir::Instruction* i64ICnt1 = builder.createExtSW(iCntChunk1, i64Ty);
            ir::Instruction* byteOffset1 = builder.createMul(i64ICnt1, ctx->getConstantInt(i64Ty, plan.elementByteSize));

            for (auto& inst : bodyBB->getInstructions()) {
                auto opc = inst->getOpcode();
                if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load ||
                    opc == ir::Instruction::Loads || opc == ir::Instruction::Loadd) {
                    ir::Value* ptr = inst->getOperands()[0]->get();
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = vectorBaseMap.count(basePtr) ? vectorBaseMap[basePtr] : basePtr;

                    ir::Instruction* vPtr0 = builder.createAdd(safeBase, byteOffset0);
                    vValueMapChunk0[inst.get()] = builder.createVLoad(vecTy, vPtr0);

                    if (plan.unrollFactor == 2) {
                        ir::Instruction* vPtr1 = builder.createAdd(safeBase, byteOffset1);
                        vValueMapChunk1[inst.get()] = builder.createVLoad(vecTy, vPtr1);
                    }
                } else if (!plan.reductions.empty() && inst.get() == plan.reductions[0].update) {
                    auto& reduction = plan.reductions[0];
                    auto* termInst = dynamic_cast<ir::Instruction*>(reduction.scalarTerm);
                    ir::Value* term0 = termInst && vValueMapChunk0.count(termInst) ? vValueMapChunk0[termInst] : nullptr;
                    if (term0) {
                        ir::VectorInstruction* next0 = nullptr;
                        switch (reduction.kind) {
                            case ReductionKind::Add: next0 = builder.createVAdd(rawPhiVSum0, term0); break;
                            case ReductionKind::Mul: next0 = builder.createVMul(rawPhiVSum0, term0); break;
                            case ReductionKind::SignedMin: next0 = builder.createVMin(rawPhiVSum0, term0); break;
                            case ReductionKind::SignedMax: next0 = builder.createVMax(rawPhiVSum0, term0); break;
                            case ReductionKind::FAdd: next0 = builder.createVFAdd(rawPhiVSum0, term0); break;
                            case ReductionKind::FMul: next0 = builder.createVFMul(rawPhiVSum0, term0); break;
                        }
                        vValueMapChunk0[inst.get()] = next0;
                        rawPhiVSum0->addIncoming(next0, vLoopBodyBB);

                        if (plan.unrollFactor == 2 && rawPhiVSum1 && vValueMapChunk1.count(termInst)) {
                            ir::Value* term1 = vValueMapChunk1[termInst];
                            ir::VectorInstruction* next1 = nullptr;
                            switch (reduction.kind) {
                                case ReductionKind::Add: next1 = builder.createVAdd(rawPhiVSum1, term1); break;
                                case ReductionKind::Mul: next1 = builder.createVMul(rawPhiVSum1, term1); break;
                                case ReductionKind::SignedMin: next1 = builder.createVMin(rawPhiVSum1, term1); break;
                                case ReductionKind::SignedMax: next1 = builder.createVMax(rawPhiVSum1, term1); break;
                                case ReductionKind::FAdd: next1 = builder.createVFAdd(rawPhiVSum1, term1); break;
                                case ReductionKind::FMul: next1 = builder.createVFMul(rawPhiVSum1, term1); break;
                            }
                            vValueMapChunk1[inst.get()] = next1;
                            rawPhiVSum1->addIncoming(next1, vLoopBodyBB);
                        }
                    }
                } else if (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores) {
                    ir::Value* valToStore = inst->getOperands()[0]->get();
                    ir::Value* ptrToStore = inst->getOperands()[1]->get();
                    auto* instVal = dynamic_cast<ir::Instruction*>(valToStore);
                    ir::Value* vVal0 = (instVal && vValueMapChunk0.count(instVal)) ? vValueMapChunk0[instVal] : valToStore;

                    ir::Value* basePtr = extractBasePointer(ptrToStore);
                    ir::Value* safeBase = vectorBaseMap.count(basePtr) ? vectorBaseMap[basePtr] : basePtr;

                    ir::Instruction* vPtr0 = builder.createAdd(safeBase, byteOffset0);
                    builder.createVStore(vVal0, vPtr0);

                    if (plan.unrollFactor == 2) {
                        ir::Value* vVal1 = (instVal && vValueMapChunk1.count(instVal)) ? vValueMapChunk1[instVal] : valToStore;
                        ir::Instruction* vPtr1 = builder.createAdd(safeBase, byteOffset1);
                        builder.createVStore(vVal1, vPtr1);
                    }
                }
            }
        }

        ir::Instruction* iCntNext = builder.createAdd(rawPhiICnt,
            ctx->getConstantInt(i32Ty, totalStepElements * plan.stepConst));
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

        ir::Value* vectorPathSum = nullptr;

        if (!plan.reductions.empty() && rawPhiVSum0) {
            ir::Value* accVec = rawPhiVSum0;
            if (plan.unrollFactor == 2 && rawPhiVSum1) {
                switch (plan.reductions[0].kind) {
                    case ReductionKind::Add: accVec = builder.createVAdd(rawPhiVSum0, rawPhiVSum1); break;
                    case ReductionKind::Mul: accVec = builder.createVMul(rawPhiVSum0, rawPhiVSum1); break;
                    case ReductionKind::SignedMin: accVec = builder.createVMin(rawPhiVSum0, rawPhiVSum1); break;
                    case ReductionKind::SignedMax: accVec = builder.createVMax(rawPhiVSum0, rawPhiVSum1); break;
                    case ReductionKind::FAdd: accVec = builder.createVFAdd(rawPhiVSum0, rawPhiVSum1); break;
                    case ReductionKind::FMul: accVec = builder.createVFMul(rawPhiVSum0, rawPhiVSum1); break;
                }
            }

            ir::Instruction* redBuf = builder.createAlloc(ctx->getConstantInt(i64Ty, plan.vectorWidthBits / 8), i64Ty);
            builder.createVStore(accVec, redBuf);

            const bool isFP = plan.reductions[0].isFloatingPoint;
            const bool isFloat = plan.reductions[0].scalarType && plan.reductions[0].scalarType->isFloatTy();

            auto loadLane = [&](ir::Value* ptr) -> ir::Instruction* {
                if (isFP) {
                    return isFloat ? builder.createLoads(ptr) : builder.createLoadd(ptr);
                }
                return builder.createLoaduw(ptr);
            };

            const size_t elemByteSize = isFP ? (isFloat ? 4 : 8) : 4;
            ir::Instruction* sumReduced = loadLane(redBuf);
            for (unsigned lane = 1; lane < plan.vectorFactor; ++lane) {
                ir::Instruction* pOff = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, lane * elemByteSize));
                ir::Instruction* laneVal = loadLane(pOff);
                sumReduced = createScalarReduction(sumReduced, laneVal, plan.reductions[0].kind);
            }
            vectorPathSum = createScalarReduction(reductionInit, sumReduced, plan.reductions[0].kind);
        }

        builder.createJmp(epiHeaderBB);

        // Epilogue Header
        builder.setInsertPoint(epiHeaderBB);
        auto phiEpiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiI = phiEpiI.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiI));

        auto phiEpiBoundOwner = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiBound = phiEpiBoundOwner.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiBoundOwner));
        rawPhiEpiBound->addIncoming(boundNCopy, entryBB);
        rawPhiEpiBound->addIncoming(postGuardBound, vReductionBB);
        rawPhiEpiBound->addIncoming(rawPhiEpiBound, epiLatchBB);

        std::map<ir::Value*, ir::Value*> epiBaseMap;
        for (const auto& copiedBase : baseCopyMap) {
            auto owner = std::make_unique<ir::PhiNode>(copiedBase.second->getType(), 0, nullptr, epiHeaderBB);
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

        std::map<ir::Instruction*, ir::Instruction*> epiValueMap;
        for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (inst.get() == addINextInst) continue;
            if (!plan.reductions.empty() && inst.get() == plan.reductions[0].update) continue;

            if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load ||
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
        }

        ir::Instruction* epiINext = builder.createAdd(rawPhiEpiI, ctx->getConstantInt(i32Ty, (uint64_t)plan.stepConst));
        rawPhiEpiI->addIncoming(epiINext, epiLatchBB);

        if (rawPhiEpiSum) {
            ir::Instruction* epiSumNext = nullptr;
            for (auto& inst : bodyBB->getInstructions()) {
                if (inst->getOpcode() == ir::Instruction::Loaduw || inst->getOpcode() == ir::Instruction::Load) {
                    if (epiValueMap.count(inst.get())) {
                        epiSumNext = createScalarReduction(rawPhiEpiSum, epiValueMap[inst.get()], plan.reductions[0].kind);
                        break;
                    }
                }
            }
            if (!epiSumNext) {
                ir::Instruction* epiTerm = builder.createMul(rawPhiEpiI, ctx->getConstantInt(i32Ty, mulFactor));
                epiSumNext = createScalarReduction(rawPhiEpiSum, epiTerm, plan.reductions[0].kind);
            }
            rawPhiEpiSum->addIncoming(epiSumNext, epiBodyBB);

            for (auto& inst : exitBB->getInstructions()) {
                for (auto& operand : inst->getOperands())
                    if (operand->get() == reductionPhi) operand->set(rawPhiEpiSum);
            }
        }

        rawPhiEpiBound->setIncomingValueForBlock(epiLatchBB, builder.createCopy(rawPhiEpiBound));
        for (const auto& basePhi : epiBaseMap) {
            auto* phi = static_cast<ir::PhiNode*>(basePhi.second);
            phi->setIncomingValueForBlock(epiLatchBB, builder.createCopy(phi));
        }

        builder.createJmp(epiHeaderBB);

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
        logDiag("loop vectorized: " + func.getName() + " (VF=" + std::to_string(plan.vectorFactor) + " unroll=" + std::to_string(plan.unrollFactor) + ")");
        break;
    }

    return changed;
}

} // namespace transforms
