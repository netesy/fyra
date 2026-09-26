#include "transforms/LoopStrengthReduction.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/CFGBuilder.h"
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

static ir::Value* stripExt(ir::Value* val) {
    while (auto* inst = dynamic_cast<ir::Instruction*>(val)) {
        if (inst->getOpcode() == ir::Instruction::ExtSW || inst->getOpcode() == ir::Instruction::ExtUW) {
            if (!inst->getOperands().empty() && inst->getOperands()[0] && inst->getOperands()[0]->get()) {
                val = inst->getOperands()[0]->get();
            } else break;
        } else break;
    }
    return val;
}

static bool parseAffine(ir::Value* val, ir::PhiNode* indPhi, const Loop& loop,
                        ir::Value*& base, int64_t& scale, int64_t& displacement) {
    val = stripExt(val);
    if (!val) return false;

    if (val == indPhi) {
        scale = 1;
        return true;
    }

    if (auto* c = dynamic_cast<ir::ConstantInt*>(val)) {
        displacement += static_cast<int64_t>(c->getValue());
        return true;
    }

    auto* inst = dynamic_cast<ir::Instruction*>(val);
    if (!inst) {
        if (isLoopInvariant(val, loop)) {
            if (!base) { base = val; return true; }
        }
        return false;
    }

    const auto op = inst->getOpcode();
    if (op == ir::Instruction::Mul && inst->getOperands().size() == 2) {
        ir::Value* op0 = stripExt(inst->getOperands()[0]->get());
        ir::Value* op1 = stripExt(inst->getOperands()[1]->get());
        auto* c0 = dynamic_cast<ir::ConstantInt*>(op0);
        auto* c1 = dynamic_cast<ir::ConstantInt*>(op1);

        if (op0 == indPhi && c1) {
            scale = static_cast<int64_t>(c1->getValue());
            return true;
        }
        if (op1 == indPhi && c0) {
            scale = static_cast<int64_t>(c0->getValue());
            return true;
        }
    } else if (op == ir::Instruction::Add && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();

        int64_t scale0 = 0, scale1 = 0;
        int64_t disp0 = 0, disp1 = 0;
        ir::Value* base0 = nullptr;
        ir::Value* base1 = nullptr;

        bool p0 = parseAffine(op0, indPhi, loop, base0, scale0, disp0);
        bool p1 = parseAffine(op1, indPhi, loop, base1, scale1, disp1);

        if (p0 && p1) {
            if (base0 && base1) return false; // Cannot handle two dynamic bases
            base = base0 ? base0 : base1;
            scale = scale0 + scale1;
            displacement = disp0 + disp1;
            return true;
        }
    } else if (op == ir::Instruction::Sub && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();
        auto* c1 = dynamic_cast<ir::ConstantInt*>(stripExt(op1));

        int64_t scale0 = 0;
        int64_t disp0 = 0;
        ir::Value* base0 = nullptr;
        if (c1 && parseAffine(op0, indPhi, loop, base0, scale0, disp0)) {
            base = base0;
            scale = scale0;
            displacement = disp0 - static_cast<int64_t>(c1->getValue());
            return true;
        }
    }

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

            auto* stepInst = dynamic_cast<ir::Instruction*>(stripExt(stepNextVal));
            if (!stepInst || stepInst->getOperands().size() < 2) continue;

            int64_t stepConst = 0;
            if (stepInst->getOpcode() == ir::Instruction::Add) {
                ir::Value* op0 = stripExt(stepInst->getOperands()[0]->get());
                ir::Value* op1 = stripExt(stepInst->getOperands()[1]->get());
                if (op0 == phi) {
                    if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) stepConst = c1->getValue();
                } else if (op1 == phi) {
                    if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) stepConst = c0->getValue();
                }
            } else if (stepInst->getOpcode() == ir::Instruction::Sub) {
                ir::Value* op0 = stripExt(stepInst->getOperands()[0]->get());
                ir::Value* op1 = stripExt(stepInst->getOperands()[1]->get());
                if (op0 == phi) {
                    if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) stepConst = -static_cast<int64_t>(c1->getValue());
                }
            }

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
                        // Profitable to reduce if scale != 1 or scale != 0 and inst is a Mul or Add inside loop
                        if (scale != 0 && (scale != 1 || disp != 0 || base != nullptr)) { std::cout << "[LSR Match] inst: " << inst->getName() << " op: " << inst->getOpcode() << " scale: " << scale << " disp: " << disp << " base: " << (base ? base->getName() : "null") << std::endl;
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

            if (candidates.empty()) continue;

            auto ctx = func.getParent() ? func.getParent()->getContextShared()
                                        : std::make_shared<ir::IRContext>();
            ir::IRBuilder builder(ctx);
            builder.setModule(func.getParent());

            for (const auto& expr : candidates) {
                if (!expr.rootInst || expr.rootInst->use_empty()) continue;

                ir::Type* resTy = expr.resultType;
                if (!resTy || !resTy->isIntegerTy()) continue;

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
                    initScaled = ctx->getConstantInt(dynamic_cast<ir::IntegerType*>(resTy), initC);
                } else {
                    ir::Value* initVal64 = iv.initVal;
                    if (iv.initVal->getType() != resTy) {
                        initVal64 = builder.createExtSW(iv.initVal, resTy);
                    }
                    ir::Value* scaled = builder.createMul(initVal64, ctx->getConstantInt(dynamic_cast<ir::IntegerType*>(resTy), expr.scale));
                    if (expr.displacement != 0) {
                        scaled = builder.createAdd(scaled, ctx->getConstantInt(dynamic_cast<ir::IntegerType*>(resTy), expr.displacement));
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
                    derivedPhi, ctx->getConstantInt(dynamic_cast<ir::IntegerType*>(resTy), stepDelta));

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
