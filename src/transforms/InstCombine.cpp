#include "transforms/InstCombine.h"
#include "ir/BasicBlock.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <vector>

namespace transforms {

bool InstCombinePass::performTransformation(ir::Function& func) {
    bool changed = false;
    auto ctx = func.getParent() ? func.getParent()->getContextShared() : nullptr;

    auto isAllOnes = [](ir::ConstantInt* c, ir::Type* ty) -> bool {
        if (!c || !ty) return false;
        uint64_t v = static_cast<uint64_t>(c->getValue());
        if (v == static_cast<uint64_t>(-1)) return true;
        if (auto* iTy = dynamic_cast<ir::IntegerType*>(ty)) {
            unsigned bw = iTy->getBitwidth();
            if (bw > 0 && bw < 64) {
                uint64_t mask = (1ULL << bw) - 1ULL;
                return (v & mask) == mask;
            }
        }
        return false;
    };

    for (auto& bb : func.getBasicBlocks()) {
        auto& instList = bb->getInstructions();
        for (auto it = instList.begin(); it != instList.end(); ) {
            ir::Instruction* inst = it->get();
            if (!inst) { ++it; continue; }

            auto opc = inst->getOpcode();
            size_t numOps = inst->getOperands().size();
            ir::Value* lhs = numOps > 0 && inst->getOperands()[0] ? inst->getOperands()[0]->get() : nullptr;
            ir::Value* rhs = numOps > 1 && inst->getOperands()[1] ? inst->getOperands()[1]->get() : nullptr;

            auto* cLhs = dynamic_cast<ir::ConstantInt*>(lhs);
            auto* cRhs = dynamic_cast<ir::ConstantInt*>(rhs);
            auto* iTy = dynamic_cast<ir::IntegerType*>(inst->getType());

            ir::Value* replacement = nullptr;

            // --- 1. Single operand / Extension / Truncation rules ---
            bool isExt = (opc == ir::Instruction::ExtSW || opc == ir::Instruction::ExtUW ||
                          opc == ir::Instruction::ExtSB || opc == ir::Instruction::ExtUB ||
                          opc == ir::Instruction::ExtSH || opc == ir::Instruction::ExtUH ||
                          opc == ir::Instruction::ExtS);
            if (isExt) {
                if (auto* inner = dynamic_cast<ir::Instruction*>(lhs)) {
                    if (inner->getOpcode() == opc) {
                        // ext (ext x) -> ext x
                        inst->getOperands()[0]->set(inner->getOperands()[0]->get());
                        changed = true;
                    } else if (inner->getOpcode() == ir::Instruction::TruncD) {
                        // ext (trunc x) where original x type matches target type -> x
                        ir::Value* origX = inner->getOperands()[0]->get();
                        if (origX && origX->getType() == inst->getType()) {
                            replacement = origX;
                        }
                    }
                }
                // (A + C1) + (B + C2) -> (A + B) + (C1 + C2)
                else if (opc == ir::Instruction::Add) {
                    if (auto* iLhs = dynamic_cast<ir::Instruction*>(lhs)) {
                        if (auto* iRhs = dynamic_cast<ir::Instruction*>(rhs)) {
                            if (iLhs->getOpcode() == ir::Instruction::Add && iRhs->getOpcode() == ir::Instruction::Add) {
                                ir::Value* aL = iLhs->getOperands()[0]->get();
                                ir::Value* bL = iLhs->getOperands()[1]->get();
                                ir::Value* aR = iRhs->getOperands()[0]->get();
                                ir::Value* bR = iRhs->getOperands()[1]->get();

                                auto* cL = dynamic_cast<ir::ConstantInt*>(bL);
                                auto* cR = dynamic_cast<ir::ConstantInt*>(bR);
                                if (!cL) { cL = dynamic_cast<ir::ConstantInt*>(aL); if (cL) aL = bL; }
                                if (!cR) { cR = dynamic_cast<ir::ConstantInt*>(aR); if (cR) aR = bR; }

                                if (cL && cR && ctx && iTy) {
                                    ir::IRBuilder builder(ctx);
                                    builder.setInsertPoint(bb.get(), it);
                                    ir::Instruction* baseAdd = builder.createAdd(aL, aR);
                                    int64_t combined = cL->getValue() + cR->getValue();
                                    if (combined == 0) {
                                        replacement = baseAdd;
                                    } else {
                                        inst->getOperands()[0]->set(baseAdd);
                                        inst->getOperands()[1]->set(ctx->getConstantInt(iTy, combined));
                                        changed = true;
                                    }
                                }
                            }
                        }
                    }
                }
                // sub (add A, C1), (add A, C2) -> C1 - C2
                else if (opc == ir::Instruction::Sub) {
                    if (auto* iLhs = dynamic_cast<ir::Instruction*>(lhs)) {
                        if (auto* iRhs = dynamic_cast<ir::Instruction*>(rhs)) {
                            if (iLhs->getOpcode() == ir::Instruction::Add && iRhs->getOpcode() == ir::Instruction::Add) {
                                ir::Value* aL = iLhs->getOperands()[0]->get();
                                ir::Value* bL = iLhs->getOperands()[1]->get();
                                ir::Value* aR = iRhs->getOperands()[0]->get();
                                ir::Value* bR = iRhs->getOperands()[1]->get();

                                auto* cL = dynamic_cast<ir::ConstantInt*>(bL);
                                auto* cR = dynamic_cast<ir::ConstantInt*>(bR);
                                if (!cL) { cL = dynamic_cast<ir::ConstantInt*>(aL); if (cL) aL = bL; }
                                if (!cR) { cR = dynamic_cast<ir::ConstantInt*>(aR); if (cR) aR = bR; }

                                auto isSameExpr = [](ir::Value* x, ir::Value* y) -> bool {
                                    if (x == y) return true;
                                    auto* ix = dynamic_cast<ir::Instruction*>(x);
                                    auto* iy = dynamic_cast<ir::Instruction*>(y);
                                    if (ix && iy && ix->getOpcode() == iy->getOpcode() && ix->getOperands().size() == 2) {
                                        return (ix->getOperands()[0]->get() == iy->getOperands()[0]->get() && ix->getOperands()[1]->get() == iy->getOperands()[1]->get()) ||
                                               (ix->getOperands()[0]->get() == iy->getOperands()[1]->get() && ix->getOperands()[1]->get() == iy->getOperands()[0]->get());
                                    }
                                    return false;
                                };

                                if (isSameExpr(aL, aR) && cL && cR && ctx && iTy) {
                                    int64_t diff = cL->getValue() - cR->getValue();
                                    replacement = ctx->getConstantInt(iTy, diff);
                                }
                            }
                        }
                    }
                }
            } else if (opc == ir::Instruction::TruncD) {
                if (auto* inner = dynamic_cast<ir::Instruction*>(lhs)) {
                    if (inner->getOpcode() == ir::Instruction::ExtSW || inner->getOpcode() == ir::Instruction::ExtUW) {
                        // trunc (ext x) -> x if x type equals inst type
                        ir::Value* origX = inner->getOperands()[0]->get();
                        if (origX && origX->getType() == inst->getType()) {
                            replacement = origX;
                        }
                    } else if (inner->getOpcode() == ir::Instruction::TruncD) {
                        // trunc (trunc x) -> trunc x
                        inst->getOperands()[0]->set(inner->getOperands()[0]->get());
                        changed = true;
                    }
                }
            }

            // --- 2. Two-operand Arithmetic and Bitwise Identities ---
            if (!replacement && numOps >= 2 && lhs && rhs) {
                // x + 0 -> x, x - 0 -> x
                if ((opc == ir::Instruction::Add || opc == ir::Instruction::Sub) && cRhs && cRhs->getValue() == 0) {
                    replacement = lhs;
                }
                // 0 + x -> x
                else if (opc == ir::Instruction::Add && cLhs && cLhs->getValue() == 0) {
                    replacement = rhs;
                }
                // x * 1 -> x, x / 1 -> x, x /u 1 -> x
                else if ((opc == ir::Instruction::Mul || opc == ir::Instruction::Div || opc == ir::Instruction::Udiv) && cRhs && cRhs->getValue() == 1) {
                    replacement = lhs;
                }
                // 1 * x -> x
                else if (opc == ir::Instruction::Mul && cLhs && cLhs->getValue() == 1) {
                    replacement = rhs;
                }
                // x * 0 -> 0, 0 * x -> 0, x & 0 -> 0, 0 & x -> 0
                else if ((opc == ir::Instruction::Mul || opc == ir::Instruction::And) && ((cRhs && cRhs->getValue() == 0) || (cLhs && cLhs->getValue() == 0))) {
                    if (ctx && iTy) replacement = ctx->getConstantInt(iTy, 0);
                }
                // x & allones -> x
                else if (opc == ir::Instruction::And && cRhs && isAllOnes(cRhs, inst->getType())) {
                    replacement = lhs;
                }
                // allones & x -> x
                else if (opc == ir::Instruction::And && cLhs && isAllOnes(cLhs, inst->getType())) {
                    replacement = rhs;
                }
                // x | 0 -> x, x ^ 0 -> x
                else if ((opc == ir::Instruction::Or || opc == ir::Instruction::Xor) && cRhs && cRhs->getValue() == 0) {
                    replacement = lhs;
                }
                // 0 | x -> x, 0 ^ x -> x
                else if ((opc == ir::Instruction::Or || opc == ir::Instruction::Xor) && cLhs && cLhs->getValue() == 0) {
                    replacement = rhs;
                }
                // x | allones -> allones, allones | x -> allones
                else if (opc == ir::Instruction::Or && (isAllOnes(cRhs, inst->getType()) || isAllOnes(cLhs, inst->getType()))) {
                    replacement = isAllOnes(cRhs, inst->getType()) ? cRhs : cLhs;
                }
                // x ^ x -> 0, x - x -> 0
                else if ((opc == ir::Instruction::Xor || opc == ir::Instruction::Sub) && lhs == rhs) {
                    if (ctx && iTy) replacement = ctx->getConstantInt(iTy, 0);
                }
                // x & x -> x, x | x -> x
                else if ((opc == ir::Instruction::And || opc == ir::Instruction::Or) && lhs == rhs) {
                    replacement = lhs;
                }
                // x << 0 -> x, x >> 0 -> x, x >>> 0 -> x
                else if ((opc == ir::Instruction::Shl || opc == ir::Instruction::Shr || opc == ir::Instruction::Sar) && cRhs && cRhs->getValue() == 0) {
                    replacement = lhs;
                }
                // 0 << x -> 0, 0 >> x -> 0
                else if ((opc == ir::Instruction::Shl || opc == ir::Instruction::Shr || opc == ir::Instruction::Sar) && cLhs && cLhs->getValue() == 0) {
                    if (ctx && iTy) replacement = ctx->getConstantInt(iTy, 0);
                }
                // (x + C1) + C2 -> x + (C1 + C2)
                else if (opc == ir::Instruction::Add && cRhs) {
                    if (auto* innerAdd = dynamic_cast<ir::Instruction*>(lhs)) {
                        if (innerAdd->getOpcode() == ir::Instruction::Add && innerAdd->getOperands().size() == 2) {
                            if (auto* cInner = dynamic_cast<ir::ConstantInt*>(innerAdd->getOperands()[1]->get())) {
                                if (ctx && iTy) {
                                    int64_t combined = cInner->getValue() + cRhs->getValue();
                                    ir::Value* baseX = innerAdd->getOperands()[0]->get();
                                    if (combined == 0) {
                                        replacement = baseX;
                                    } else {
                                        inst->getOperands()[0]->set(baseX);
                                        inst->getOperands()[1]->set(ctx->getConstantInt(iTy, combined));
                                        changed = true;
                                    }
                                }
                            }
                        }
                    }
                }
                // Comparisons where lhs == rhs
                else if (lhs == rhs) {
                    if (ctx && iTy) {
                        if (opc == ir::Instruction::Ceq || opc == ir::Instruction::Csle || opc == ir::Instruction::Cule || opc == ir::Instruction::Csge || opc == ir::Instruction::Cuge) {
                            replacement = ctx->getConstantInt(iTy, 1);
                        } else if (opc == ir::Instruction::Cne || opc == ir::Instruction::Cslt || opc == ir::Instruction::Cult || opc == ir::Instruction::Csgt || opc == ir::Instruction::Cugt) {
                            replacement = ctx->getConstantInt(iTy, 0);
                        }
                    }
                }
            }

            // --- 3. Select / Boolean canonicalization ---
            if (!replacement && opc == ir::Instruction::VSelect && numOps >= 3) {
                ir::Value* cond = inst->getOperands()[0]->get();
                ir::Value* trueVal = inst->getOperands()[1]->get();
                ir::Value* falseVal = inst->getOperands()[2]->get();

                if (trueVal == falseVal) {
                    replacement = trueVal;
                } else if (auto* cCond = dynamic_cast<ir::ConstantInt*>(cond)) {
                    replacement = (cCond->getValue() != 0) ? trueVal : falseVal;
                }
            }

            if (replacement) {
                inst->replaceAllUsesWith(replacement);
                it = instList.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

} // namespace transforms
