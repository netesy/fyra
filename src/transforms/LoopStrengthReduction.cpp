#include "transforms/LoopStrengthReduction.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/CFGBuilder.h"
#include "transforms/AffineAnalysis.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <algorithm>
#include <vector>
#include <set>
#include <map>

namespace transforms {

namespace {

struct PrimaryIndVar {
    ir::PhiNode* phi = nullptr;
    ir::BasicBlock* preheader = nullptr;
    ir::BasicBlock* latch = nullptr;
    ir::Value* initVal = nullptr;
    int64_t stepConst = 0;
    ir::Instruction* stepInst = nullptr;
};

struct AffineExpr {
    ir::Instruction* rootInst = nullptr;
    ir::Value* base = nullptr;            // Loop invariant base or nullptr
    int64_t scale = 1;                    // Scale factor for induction variable
    int64_t displacement = 0;             // Constant displacement
    ir::Type* resultType = nullptr;
};

static bool isLoopInvariant(ir::Value* val, const Loop& loop) {
    if (!val) return true;
    if (dynamic_cast<ir::Constant*>(val) || dynamic_cast<ir::Parameter*>(val) || dynamic_cast<ir::GlobalValue*>(val))
        return true;
    if (auto* inst = dynamic_cast<ir::Instruction*>(val)) {
        return loop.blocks.find(inst->getParent()) == loop.blocks.end();
    }
    return false;
}

static bool parseAffine(ir::Value* val, ir::PhiNode* indPhi, const Loop& loop,
                        ir::Value*& base, int64_t& scale, int64_t& displacement) {
    // Use shared AffineAnalysis for coefficient extraction
    // This handles shift forms, multiplication, addition, etc.
    AffineAnalysis affine;
    auto result = affine.analyze(val);
    
    if (result.isValid) {
        auto indVarCoeff = result.getCoefficient(indPhi);
        if (indVarCoeff.has_value()) {
            scale = indVarCoeff.value();
            displacement = result.constant;
            
            // Find the base term (non-induction, loop-invariant term)
            for (const auto& term : result.terms) {
                ir::Value* candidate = term.rawValue ? term.rawValue : term.value;
                if (term.value != indPhi && term.rawValue != indPhi && isLoopInvariant(candidate, loop)) {
                    base = candidate;
                    break;
                }
            }
            
            return true;
        }
    }

    // LSR-specific loop invariance handling for non-affine values
    if (isLoopInvariant(val, loop)) {
        if (!base) { base = val; return true; }
    }

    return false;
}

} // anonymous namespace

bool LoopStrengthReduction::performTransformation(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    LoopInvariantCodeMotion licm(errorReporter_);
    std::vector<std::unique_ptr<Loop>> loops;
    licm.findLoops(func, loops);

    bool changed = false;
    AffineAnalysis affine;

    for (auto& loopPtr : loops) {
        Loop& loop = *loopPtr;
        if (!loop.header) continue;

        if (!loop.preheader) {
            licm.getOrCreatePreheader(loop, func);
        }
        if (!loop.preheader) continue;

        // Find primary induction variables
        std::vector<PrimaryIndVar> indVars;
        ir::BasicBlock* preheader = loop.preheader;
        ir::BasicBlock* latch = nullptr;

        for (ir::BasicBlock* pred : loop.header->getPredecessors()) {
            if (pred != preheader) { latch = pred; break; }
        }
        if (!latch) continue;

        for (auto& instPtr : loop.header->getInstructions()) {
            auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get());
            if (!phi) break;

            ir::Value* initVal = phi->getIncomingValueForBlock(preheader);
            ir::Value* stepNextVal = phi->getIncomingValueForBlock(latch);
            if (!initVal || !stepNextVal) continue;

            auto stepResult = affine.analyze(stepNextVal);
            if (!stepResult.isValid || stepResult.terms.size() != 1) continue;
            auto phiCoeff = stepResult.getCoefficient(phi);
            if (!phiCoeff.has_value() || phiCoeff.value() != 1) continue;

            int64_t stepConst = stepResult.constant;
            if (stepConst == 0) continue;

            auto* stepInst = dynamic_cast<ir::Instruction*>(stepNextVal);
            if (!stepInst) continue;

            if (stepConst != 0) {
                PrimaryIndVar iv;
                iv.phi = phi;
                iv.preheader = preheader;
                iv.latch = latch;
                iv.initVal = initVal;
                iv.stepConst = stepConst;
                iv.stepInst = stepInst;
                indVars.push_back(iv);
            }
        }

        if (indVars.empty()) continue;

        // Process loop blocks looking for reducible expressions
        for (const auto& iv : indVars) {
            std::vector<AffineExpr> candidates;

            for (ir::BasicBlock* bb : loop.blocks) {
                for (auto& instOwner : bb->getInstructions()) {
                    ir::Instruction* inst = instOwner.get();
                    if (!inst || inst == iv.phi || inst == iv.stepInst) continue;
                    if (inst->getOpcode() != ir::Instruction::Mul && inst->getOpcode() != ir::Instruction::Add) continue;

                    ir::Value* base = nullptr;
                    int64_t scale = 0;
                    int64_t disp = 0;

                    if (parseAffine(inst, iv.phi, loop, base, scale, disp)) {
                        // Profitable to reduce strength when addressing base pointer
                        if (base != nullptr && (base->getType()->isPointerTy() || dynamic_cast<ir::Parameter*>(base) || dynamic_cast<ir::GlobalVariable*>(base))) {
                            AffineExpr expr;
                            expr.rootInst = inst;
                            expr.base = base;
                            expr.scale = scale;
                            expr.displacement = disp;
                            expr.resultType = inst->getType();
                            candidates.push_back(expr);
                        }
                    }
                }
            }

            if (!iv.preheader || !iv.latch || candidates.empty()) continue;

            auto ctx = func.getParent() ? func.getParent()->getContextShared()
                                        : std::make_shared<ir::IRContext>();
            ir::IRBuilder builder(ctx);
            builder.setModule(func.getParent());

            for (const auto& expr : candidates) {
                if (!expr.rootInst || expr.rootInst->use_empty()) continue;

                ir::Type* resTy = expr.resultType;
                if (!resTy || (!resTy->isIntegerTy() && !resTy->isPointerTy())) continue;

                auto* intResTy = dynamic_cast<ir::IntegerType*>(resTy);
                if (!intResTy) {
                    intResTy = ctx->getIntegerType(64);
                }

                // 1. Materialize initial value in preheader
                ir::Instruction* preheaderTerm = preheader->getInstructions().empty() ? nullptr : preheader->getInstructions().back().get();
                if (preheaderTerm) {
                    builder.setInsertPoint(preheader, --preheader->getInstructions().end());
                } else {
                    builder.setInsertPoint(preheader);
                }

                ir::Value* initScaled = nullptr;
                int64_t initC = 0;
                if (auto* cInit = dynamic_cast<ir::ConstantInt*>(iv.initVal)) {
                    initC = cInit->getValue() * expr.scale + expr.displacement;
                    initScaled = ctx->getConstantInt(intResTy, initC);
                } else {
                    ir::Value* initVal64 = iv.initVal;
                    if (iv.initVal->getType() != intResTy) {
                        initVal64 = builder.createExtSW(iv.initVal, intResTy);
                    }
                    ir::Value* scaled = builder.createMul(initVal64, ctx->getConstantInt(intResTy, expr.scale));
                    if (expr.displacement != 0) {
                        scaled = builder.createAdd(scaled, ctx->getConstantInt(intResTy, expr.displacement));
                    }
                    initScaled = scaled;
                }

                if (expr.base) {
                    ir::Value* baseVal = expr.base;
                    if (baseVal->getType() != resTy && baseVal->getType()->isInteger()) {
                        baseVal = builder.createExtSW(baseVal, resTy);
                    }
                    initScaled = builder.createAdd(baseVal, initScaled);
                }

                // 2. Insert derived PHI in loop header
                auto derivedPhiOwner = std::make_unique<ir::PhiNode>(resTy, 0, nullptr, loop.header);
                ir::PhiNode* derivedPhi = derivedPhiOwner.get();
                loop.header->getInstructions().push_front(std::move(derivedPhiOwner));

                derivedPhi->addIncoming(initScaled, preheader);

                // 3. Increment derived PHI in latch
                ir::Instruction* latchTerm = latch->getInstructions().empty() ? nullptr : latch->getInstructions().back().get();
                if (latchTerm) {
                    builder.setInsertPoint(latch, --latch->getInstructions().end());
                } else {
                    builder.setInsertPoint(latch);
                }

                int64_t stepDelta = iv.stepConst * expr.scale;
                ir::Instruction* derivedNext = builder.createAdd(
                    derivedPhi, ctx->getConstantInt(intResTy, stepDelta));

                derivedPhi->addIncoming(derivedNext, latch);

                // 4. Replace uses of rootInst with derivedPhi inside loop
                expr.rootInst->replaceAllUsesWith(derivedPhi);
                changed = true;
            }
        }
    }

    if (changed) {
        CFGBuilder::run(func);
    }
    return changed;
}

} // namespace transforms
