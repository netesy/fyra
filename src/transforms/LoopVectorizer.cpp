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
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <cstdlib>

namespace transforms {

namespace {

void logDiag(const std::string& msg) {
    if (std::getenv("FYRA_VECTORIZER_DIAG")) {
        std::cout << "[LoopVectorizer Diag] " << msg << std::endl;
    }
}

struct MemoryAccess {
    ir::Instruction* inst = nullptr;
    bool isStore = false;
    ir::Value* base = nullptr;
    ir::Value* index = nullptr;
    int64_t stride = 1;
    int64_t elementSize = 4;
};

enum class ReductionKind { Add, Mul, SignedMin, SignedMax };

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
    const char* collapseStrategy = "scalar lane fold";
    bool isWidening = false;
    ir::Instruction::Opcode conversionOpcode = ir::Instruction::VSExt;
    unsigned sourceVF = 8;
    unsigned accumulatorVF = 4;
    unsigned accumulatorsPerChunk = 2;
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
    ir::BasicBlock* preheaderBB = nullptr;
    ir::BasicBlock* exitBB = nullptr;

    std::vector<ReductionPlan> reductions;
    std::vector<MemoryAccess> memoryAccesses;

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
};

bool isInductionIndex(ir::Value* value, ir::PhiNode* induction) {
    if (value == induction) return true;
    auto* inst = dynamic_cast<ir::Instruction*>(value);
    return inst && inst->getOpcode() == ir::Instruction::ExtSW &&
           !inst->getOperands().empty() && inst->getOperands()[0]->get() == induction;
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

} // anonymous namespace

bool LoopVectorizer::performTransformation(ir::Function& func) {
    bool changed = false;

    logDiag("Analyzing function: " + func.getName());

    for (auto bbIt = func.getBasicBlocks().begin(); bbIt != func.getBasicBlocks().end(); ++bbIt) {
        ir::BasicBlock* headerBB = bbIt->get();

        std::vector<ir::PhiNode*> headerPhis;
        ir::Instruction* sltCond = nullptr;
        ir::Instruction* brInst = nullptr;

        for (auto& inst : headerBB->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(inst.get())) {
                headerPhis.push_back(phi);
            } else if (inst->getOpcode() == ir::Instruction::Cslt || inst->getOpcode() == ir::Instruction::Clt ||
                       inst->getOpcode() == ir::Instruction::Csgt) {
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

        ir::BasicBlock* entryBB = nullptr;
        for (auto* pred : headerBB->getPredecessors()) {
            if (pred != bodyBB) { entryBB = pred; break; }
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
        for (auto& inst : bodyBB->getInstructions()) {
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
                if (access.inst->getOperands().size() > 0) {
                    ir::Value* ptr = access.isStore ? access.inst->getOperands()[1]->get() : access.inst->getOperands()[0]->get();
                    access.base = extractBasePointer(ptr);
                }
                memAccesses.push_back(access);
            }
        }
        if (!isLegal) continue;

        // --- Memory Dependence Analysis ---
        bool memLegal = true;
        for (size_t i = 0; i < memAccesses.size(); ++i) {
            for (size_t j = i + 1; j < memAccesses.size(); ++j) {
                if (memAccesses[i].isStore || memAccesses[j].isStore) {
                    if (memAccesses[i].base == memAccesses[j].base) {
                        logDiag("Rejected loop: loop-carried memory dependence on same base pointer");
                        memLegal = false;
                        break;
                    }
                }
            }
            if (!memLegal) break;
        }
        if (!memLegal) continue;

        VectorizationPlan plan;
        plan.headerBB = headerBB;
        plan.bodyBB = bodyBB;
        plan.preheaderBB = entryBB;
        plan.exitBB = exitBB;
        plan.memoryAccesses = memAccesses;

        // 1. Identify Induction Variable & Step
        ir::PhiNode* iPhi = nullptr;
        ir::Instruction* addINextInst = nullptr;

        for (ir::PhiNode* phi : headerPhis) {
            if (!phi->getType() || !phi->getType()->isInteger()) continue;

            ir::Value* preVal = phi->getIncomingValueForBlock(entryBB);
            if (!preVal) continue;

            ir::Value* latchVal = phi->getIncomingValueForBlock(bodyBB);
            if (!latchVal) continue;
            auto* latchInst = dynamic_cast<ir::Instruction*>(latchVal);
            if (!latchInst) continue;

            if (latchInst->getOpcode() == ir::Instruction::Add && latchInst->getOperands().size() >= 2) {
                ir::Value* op0 = latchInst->getOperands()[0]->get();
                ir::Value* op1 = latchInst->getOperands()[1]->get();
                auto* c1 = dynamic_cast<ir::ConstantInt*>(op1);
                auto* c0 = dynamic_cast<ir::ConstantInt*>(op0);
                if (((op0 == phi && c1 && c1->getValue() == 1) ||
                     (op1 == phi && c0 && c0->getValue() == 1))) {
                    iPhi = phi;
                    addINextInst = latchInst;
                    plan.initVal = preVal;
                    plan.stepConst = 1;
                    plan.stepInst = latchInst;
                    break;
                }
            }
        }

        if (!iPhi || !addINextInst) {
            logDiag("Rejected loop: canonical induction variable (step=1) not found");
            continue;
        }

        ir::Value* condOp0 = sltCond->getOperands()[0]->get();
        ir::Value* condOp1 = sltCond->getOperands()[1]->get();
        const bool normalLess = (sltCond->getOpcode() == ir::Instruction::Cslt ||
                                 sltCond->getOpcode() == ir::Instruction::Clt) && condOp0 == iPhi;
        const bool reversedGreater = sltCond->getOpcode() == ir::Instruction::Csgt && condOp1 == iPhi;
        ir::Value* boundN = normalLess ? condOp1 : (reversedGreater ? condOp0 : nullptr);
        if (!boundN) { logDiag("reject: induction is not the varying operand of the loop comparison"); continue; }

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
            if (accessType->isIntegerTy() && accessType->getSize() != 4) {
                plan.rejectionReason = "only i32 integer memory elements are supported";
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
        }
        if (!isLegal) { logDiag("reject: " + plan.rejectionReason); continue; }

        for (auto& access : plan.memoryAccesses) {
            ir::Value* ptr = access.isStore ? access.inst->getOperands()[1]->get()
                                            : access.inst->getOperands()[0]->get();
            if (!isUnitStrideAddress(ptr, iPhi, plan.elementByteSize)) {
                plan.rejectionReason = "memory address is not base + induction * element size";
                logDiag("reject: " + plan.rejectionReason);
                isLegal = false;
                break;
            }
        }
        if (!isLegal) continue;
        if (!plan.memoryAccesses.empty())
            logDiag("memory: unit stride " + plan.mainElemType->toString());

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

        if (reductionPhi && reductionPhi->getType() && reductionPhi->getType()->isFloatingPoint()) {
            logDiag("Rejected loop: floating-point reductions require reassociation semantics");
            continue;
        }

        bool unsupportedReduction = false;
        uint64_t mulFactor = 1;
        if (reductionPhi && reductionPhi->getType() && reductionPhi->getType()->isInteger()) {
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

                if (reductionPhi->getType()->getSize() == 8) {
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
                            logDiag("Rejected loop: i64 reduction term is not a signed i32 load extension");
                            unsupportedReduction = true;
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

        if (plan.reductions.empty() && plan.memoryAccesses.empty()) {
            logDiag("Rejected loop: no vectorizable reductions or array memory accesses found");
            continue;
        }

        // Target Capability Query & Optimal VF Selection
        auto targetInfo = func.getParent() ? target::TargetResolver::resolve(target::TargetDescriptor{target::Arch::X64, target::OS::Linux}) : nullptr;
        auto ctx = func.getParent()->getContextShared();
        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        if (plan.isWideningReduction) {
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
            plan.vectorWidthBits = 128;
            plan.vectorFactor = plan.vectorWidthBits / (plan.elementByteSize * 8);

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
                unsigned candidateVF = 256 / (plan.elementByteSize * 8);
                ir::VectorType* vec256 = ctx->getVectorType(plan.mainElemType, candidateVF);
                logDiag("candidate 256-bit vector type: <" + std::to_string(candidateVF) + " x " +
                        plan.mainElemType->toString() + ">");
                logDiag("supportsVectorType: " + std::string(targetInfo->supportsVectorType(vec256) ? "true" : "false"));
                logDiag("supportsVectorOperation: " + std::string(targetInfo->supportsVectorOperation(mainVOp, vec256) ? "true" : "false"));

                if (targetInfo->supportsVectorWidth(256) && targetInfo->supportsVectorType(vec256) &&
                    targetInfo->supportsVectorOperation(mainVOp, vec256)) {
                    plan.vectorFactor = candidateVF;
                    plan.vectorWidthBits = 256;
                }
            }
        }

        logDiag("profitability: VF=" + std::to_string(plan.vectorFactor) + " (" + std::to_string(plan.vectorWidthBits) + "-bit) selected");
        plan.legal = true;
        logDiag("plan accepted");

        ir::VectorType* vecTy = ctx->getVectorType(plan.mainElemType, plan.vectorFactor);
        plan.vectorType = vecTy;
        if (!plan.reductions.empty()) {
            auto& reduction = plan.reductions[0];
            if (!plan.isWideningReduction) {
                reduction.vectorType = vecTy;
            }
            const char* kind = reduction.kind == ReductionKind::Add ? "add" :
                               reduction.kind == ReductionKind::Mul ? "product" :
                               reduction.kind == ReductionKind::SignedMin ? "signed min" : "signed max";
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
        ir::Value* inductionInit = dynamic_cast<ir::ConstantInt*>(plan.initVal)
            ? plan.initVal : static_cast<ir::Value*>(builder.createCopy(plan.initVal));

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
        auto* constantStart = dynamic_cast<ir::ConstantInt*>(plan.initVal);
        if (!constantStart || constantStart->getValue() != 0)
            tripCount = builder.createSub(boundNCopy, inductionInit);
        ir::Instruction* hasVec = builder.createCsgt(tripCount, ctx->getConstantInt(i32Ty, plan.vectorFactor - 1));
        ir::Instruction* vectorCount = builder.createAnd(tripCount, ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)plan.vectorFactor)));
        nVec = (constantStart && constantStart->getValue() == 0)
                   ? static_cast<ir::Value*>(vectorCount)
                   : static_cast<ir::Value*>(builder.createAdd(inductionInit, vectorCount));

        ir::BasicBlock* vPreheaderBB = builder.createBasicBlock("v_preheader", &func);
        ir::BasicBlock* vLoopHeaderBB = builder.createBasicBlock("v_loop_header", &func);
        ir::BasicBlock* vLoopBodyBB = builder.createBasicBlock("v_loop_body", &func);
        ir::BasicBlock* vReductionBB = builder.createBasicBlock("v_reduction", &func);
        ir::BasicBlock* epiHeaderBB = builder.createBasicBlock("epi_header", &func);
        ir::BasicBlock* epiBodyBB = builder.createBasicBlock("epi_body", &func);

        builder.createBr(hasVec, vPreheaderBB, epiHeaderBB);

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

        ir::VectorInstruction* vInitI = nullptr;
        ir::VectorInstruction* vStep = nullptr;
        ir::VectorInstruction* vScale = nullptr;
        if (plan.memoryAccesses.empty() && !plan.isWideningReduction) {
            vInitI = buildVectorConst(startValConst, 1);
            vStep = buildVectorConst(plan.vectorFactor, 0);
            vScale = buildVectorConst((uint32_t)mulFactor, 0);
        }

        ir::VectorInstruction* vReductionIdentity = nullptr;
        ir::VectorInstruction* vZeroAcc0 = nullptr;
        ir::VectorInstruction* vZeroAcc1 = nullptr;

        if (!plan.reductions.empty()) {
            if (plan.isWideningReduction) {
                ir::VectorType* v4i64Ty = ctx->getVectorType(i64Ty, 4);
                vZeroAcc0 = builder.createVBroadcast(v4i64Ty, ctx->getConstantInt(i64Ty, 0));
                vZeroAcc1 = builder.createVBroadcast(v4i64Ty, ctx->getConstantInt(i64Ty, 0));
            } else {
                vReductionIdentity = builder.createVBroadcast(
                    vecTy, ctx->getConstantInt(i32Ty,
                        static_cast<uint32_t>(plan.reductions[0].identity)));
            }
        }

        ir::Value* lateTripCount = boundNCopy;
        if (!constantStart || constantStart->getValue() != 0)
            lateTripCount = builder.createSub(boundNCopy, inductionInit);
        ir::Instruction* lateVectorCount = builder.createAnd(
            lateTripCount,
            ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)plan.vectorFactor)));
        nVec = (constantStart && constantStart->getValue() == 0)
                   ? static_cast<ir::Value*>(lateVectorCount)
                   : static_cast<ir::Value*>(builder.createAdd(inductionInit, lateVectorCount));

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

        if (!plan.reductions.empty()) {
            if (plan.isWideningReduction) {
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

        rawPhiICnt->addIncoming(inductionInit, vPreheaderBB);

        ir::Instruction* vCond = builder.createCslt(rawPhiICnt, nVec);
        builder.createBr(vCond, vLoopBodyBB, vReductionBB);

        // Vector Body
        builder.setInsertPoint(vLoopBodyBB);

        if (plan.isWideningReduction) {
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
            ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
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
        } else if (!plan.memoryAccesses.empty()) {
            std::map<ir::Instruction*, ir::Value*> vValueMap;
            ir::Instruction* i64ICnt = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset = builder.createMul(
                i64ICnt, ctx->getConstantInt(i64Ty, plan.elementByteSize));

            for (auto& inst : bodyBB->getInstructions()) {
                auto opc = inst->getOpcode();
                if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load ||
                    opc == ir::Instruction::Loads || opc == ir::Instruction::Loadd) {
                    ir::Value* ptr = inst->getOperands()[0]->get();
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                    ir::VectorInstruction* vLd = builder.createVLoad(vecTy, vPtr);
                    vValueMap[inst.get()] = vLd;
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
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                    builder.createVStore(vVal, vPtr);
                }
            }
        } else if (rawPhiVSum) {
            ir::VectorInstruction* vTerm = builder.createVMul(rawPhiVI, vScale);
            ir::VectorInstruction* vSumNext = builder.createVAdd(rawPhiVSum, vTerm);
            rawPhiVSum->addIncoming(vSumNext, vLoopBodyBB);
        }

        ir::Instruction* iCntNext = builder.createAdd(rawPhiICnt, ctx->getConstantInt(i32Ty, plan.vectorFactor));

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
            }
            return nullptr;
        };
        ir::Instruction* sumReduced = nullptr;
        ir::Value* vectorPathSum = nullptr;

        if (plan.isWideningReduction && rawPhiVSum0 && rawPhiVSum1) {
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

            sumReduced = builder.createLoaduw(redBuf);
            for (unsigned lane = 1; lane < plan.vectorFactor; ++lane) {
                ir::Instruction* pOff = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, lane * 4));
                ir::Instruction* laneVal = builder.createLoaduw(pOff);
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

        ir::Instruction* epiCond = builder.createCslt(rawPhiEpiI, boundNCopy);
        builder.createBr(epiCond, epiBodyBB, exitBB);

        // Epilogue Body
        builder.setInsertPoint(epiBodyBB);

        // Recreate original scalar body in epilogue for remainder iterations
        std::map<ir::Instruction*, ir::Instruction*> epiValueMap;
        for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (inst.get() == addINextInst) continue;
            if (!plan.reductions.empty() && inst.get() == plan.reductions[0].update) continue;
            if (!plan.reductions.empty() && plan.memoryAccesses.empty() &&
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
                ir::Value* eOp0 = (inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0;
                ir::Value* eOp1 = (inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1;
                ir::Instruction* epiAdd = opc == ir::Instruction::FAdd
                    ? builder.createFAdd(eOp0, eOp1) : builder.createAdd(eOp0, eOp1);
                epiValueMap[inst.get()] = epiAdd;
            } else if (opc == ir::Instruction::Sub || opc == ir::Instruction::FSub) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                ir::Value* eOp0 = (inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0;
                ir::Value* eOp1 = (inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1;
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
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
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
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ePtr = builder.createAdd(safeBase, byteOff);
                }
                if (opc == ir::Instruction::Stores) builder.createStores(eVal, ePtr);
                else if (opc == ir::Instruction::Stored) builder.createStored(eVal, ePtr);
                else builder.createStore(eVal, ePtr);
            }
        }

        ir::Instruction* epiINext = builder.createAdd(rawPhiEpiI, ctx->getConstantInt(i32Ty, 1));
        rawPhiEpiI->addIncoming(epiINext, epiBodyBB);

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
                if (inst->getOpcode() == ir::Instruction::Ret && !inst->getOperands().empty()) {
                    inst->getOperands()[0]->set(rawPhiEpiSum);
                }
            }
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

        removeBB(headerBB);
        removeBB(bodyBB);

        CFGBuilder::run(func);
        changed = true;
        logDiag("loop vectorized: " + func.getName() + " (VF=" + std::to_string(plan.vectorFactor) + ")");
        break;
    }

    return changed;
}

} // namespace transforms
