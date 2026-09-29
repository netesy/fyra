#include "transforms/SCCP.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Module.h"
#include "ir/PhiNode.h"
#include "ir/SIMDInstruction.h"
#include "transforms/DominatorTree.h"
#include <iostream>
#include <cmath>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace transforms {

static uint64_t getBitWidth(ir::Type* ty) {
    if (!ty) return 32;
    if (auto* ity = dynamic_cast<ir::IntegerType*>(ty)) {
        return ity->getBitwidth();
    }
    return 32;
}

static uint64_t maskValue(uint64_t val, uint64_t width) {
    if (width >= 64) return val;
    return val & ((1ULL << width) - 1ULL);
}

static int64_t signExtend(uint64_t val, uint64_t width) {
    val = maskValue(val, width);
    if (width >= 64) return (int64_t)val;
    uint64_t signBit = 1ULL << (width - 1);
    if (val & signBit) {
        uint64_t mask = (1ULL << width) - 1ULL;
        return (int64_t)(val | ~mask);
    }
    return (int64_t)val;
}

static ir::Constant* foldScalarInstruction(
    ir::Instruction::Opcode op,
    ir::Type* resTy,
    const std::vector<ir::Constant*>& opConsts
) {
    std::vector<ir::ConstantInt*> opCIs;
    for (auto* c : opConsts) {
        auto* ci = dynamic_cast<ir::ConstantInt*>(c);
        if (!ci) return nullptr;
        opCIs.push_back(ci);
    }
    if (opCIs.empty()) return nullptr;

    uint64_t width = getBitWidth(resTy);
    uint64_t u1 = opCIs[0]->getValue();
    uint64_t u2 = (opCIs.size() > 1 && opCIs[1]) ? opCIs[1]->getValue() : 0;
    int64_t s1 = signExtend(u1, width);
    int64_t s2 = signExtend(u2, width);
    uint64_t resU = 0;
    bool evalSuccess = true;

    switch (op) {
        case ir::Instruction::Add: resU = maskValue(u1 + u2, width); break;
        case ir::Instruction::Sub: resU = maskValue(u1 - u2, width); break;
        case ir::Instruction::Mul: resU = maskValue(u1 * u2, width); break;
        case ir::Instruction::Div:
            if (s2 == 0) evalSuccess = false;
            else resU = maskValue((uint64_t)(s1 / s2), width);
            break;
        case ir::Instruction::Udiv:
            if (u2 == 0) evalSuccess = false;
            else resU = maskValue(u1 / u2, width);
            break;
        case ir::Instruction::Rem:
            if (s2 == 0) evalSuccess = false;
            else resU = maskValue((uint64_t)(s1 % s2), width);
            break;
        case ir::Instruction::Urem:
            if (u2 == 0) evalSuccess = false;
            else resU = maskValue(u1 % u2, width);
            break;
        case ir::Instruction::And: resU = maskValue(u1 & u2, width); break;
        case ir::Instruction::Or:  resU = maskValue(u1 | u2, width); break;
        case ir::Instruction::Xor: resU = maskValue(u1 ^ u2, width); break;
        case ir::Instruction::Shl: resU = maskValue(u1 << (u2 & 63), width); break;
        case ir::Instruction::Shr: resU = maskValue(u1 >> (u2 & 63), width); break;
        case ir::Instruction::Sar: resU = maskValue((uint64_t)(s1 >> (u2 & 63)), width); break;
        case ir::Instruction::Neg: resU = maskValue(-u1, width); break;
        case ir::Instruction::Not: resU = maskValue(~u1, width); break;

        case ir::Instruction::Ceq: resU = (u1 == u2) ? 1 : 0; break;
        case ir::Instruction::Cne: resU = (u1 != u2) ? 1 : 0; break;
        case ir::Instruction::Csle: resU = (s1 <= s2) ? 1 : 0; break;
        case ir::Instruction::Cslt: resU = (s1 < s2) ? 1 : 0; break;
        case ir::Instruction::Csge: resU = (s1 >= s2) ? 1 : 0; break;
        case ir::Instruction::Csgt: resU = (s1 > s2) ? 1 : 0; break;
        case ir::Instruction::Cule: resU = (u1 <= u2) ? 1 : 0; break;
        case ir::Instruction::Cult: resU = (u1 < u2) ? 1 : 0; break;
        case ir::Instruction::Cuge: resU = (u1 >= u2) ? 1 : 0; break;
        case ir::Instruction::Cugt: resU = (u1 > u2) ? 1 : 0; break;

        case ir::Instruction::Copy: resU = maskValue(u1, width); break;
        case ir::Instruction::ExtUB: case ir::Instruction::ExtUH: case ir::Instruction::ExtUW:
        case ir::Instruction::Cast:
            resU = maskValue(u1, width); break;
        case ir::Instruction::ExtSB: resU = maskValue((uint64_t)signExtend(u1, 8), width); break;
        case ir::Instruction::ExtSH: resU = maskValue((uint64_t)signExtend(u1, 16), width); break;
        case ir::Instruction::ExtSW: resU = maskValue((uint64_t)signExtend(u1, 32), width); break;
        case ir::Instruction::ExtS:  resU = maskValue((uint64_t)s1, width); break;
        case ir::Instruction::TruncD: resU = maskValue(u1, width); break;

        default: evalSuccess = false; break;
    }

    if (!evalSuccess) return nullptr;

    ir::IntegerType* ity = dynamic_cast<ir::IntegerType*>(resTy);
    if (!ity) ity = ir::IntegerType::get(width);
    return ir::ConstantInt::get(ity, resU);
}

static bool computeScalarOpValueFast(
    ir::Instruction::Opcode op,
    uint64_t width,
    const std::vector<ir::Constant*>& opConsts,
    uint64_t& resU
) {
    std::vector<ir::ConstantInt*> opCIs;
    for (auto* c : opConsts) {
        auto* ci = static_cast<ir::ConstantInt*>(c);
        if (!ci) return false;
        opCIs.push_back(ci);
    }
    if (opCIs.empty()) return false;

    uint64_t u1 = opCIs[0]->getValue();
    uint64_t u2 = (opCIs.size() > 1 && opCIs[1]) ? opCIs[1]->getValue() : 0;
    int64_t s1 = signExtend(u1, width);
    int64_t s2 = signExtend(u2, width);
    bool evalSuccess = true;

    switch (op) {
        case ir::Instruction::Add: resU = maskValue(u1 + u2, width); break;
        case ir::Instruction::Sub: resU = maskValue(u1 - u2, width); break;
        case ir::Instruction::Mul: resU = maskValue(u1 * u2, width); break;
        case ir::Instruction::Div:
            if (s2 == 0) evalSuccess = false;
            else resU = maskValue((uint64_t)(s1 / s2), width);
            break;
        case ir::Instruction::Udiv:
            if (u2 == 0) evalSuccess = false;
            else resU = maskValue(u1 / u2, width);
            break;
        case ir::Instruction::Rem:
            if (s2 == 0) evalSuccess = false;
            else resU = maskValue((uint64_t)(s1 % s2), width);
            break;
        case ir::Instruction::Urem:
            if (u2 == 0) evalSuccess = false;
            else resU = maskValue(u1 % u2, width);
            break;
        case ir::Instruction::And: resU = maskValue(u1 & u2, width); break;
        case ir::Instruction::Or:  resU = maskValue(u1 | u2, width); break;
        case ir::Instruction::Xor: resU = maskValue(u1 ^ u2, width); break;
        case ir::Instruction::Shl: resU = maskValue(u1 << (u2 & 63), width); break;
        case ir::Instruction::Shr: resU = maskValue(u1 >> (u2 & 63), width); break;
        case ir::Instruction::Sar: resU = maskValue((uint64_t)(s1 >> (u2 & 63)), width); break;
        case ir::Instruction::Neg: resU = maskValue(-u1, width); break;
        case ir::Instruction::Not: resU = maskValue(~u1, width); break;

        case ir::Instruction::Ceq: resU = (u1 == u2) ? 1 : 0; break;
        case ir::Instruction::Cne: resU = (u1 != u2) ? 1 : 0; break;
        case ir::Instruction::Csle: resU = (s1 <= s2) ? 1 : 0; break;
        case ir::Instruction::Cslt: resU = (s1 < s2) ? 1 : 0; break;
        case ir::Instruction::Csge: resU = (s1 >= s2) ? 1 : 0; break;
        case ir::Instruction::Csgt: resU = (s1 > s2) ? 1 : 0; break;
        case ir::Instruction::Cule: resU = (u1 <= u2) ? 1 : 0; break;
        case ir::Instruction::Cult: resU = (u1 < u2) ? 1 : 0; break;
        case ir::Instruction::Cuge: resU = (u1 >= u2) ? 1 : 0; break;
        case ir::Instruction::Cugt: resU = (u1 > u2) ? 1 : 0; break;

        case ir::Instruction::Copy: resU = maskValue(u1, width); break;
        case ir::Instruction::ExtUB: case ir::Instruction::ExtUH: case ir::Instruction::ExtUW:
        case ir::Instruction::Cast:
            resU = maskValue(u1, width); break;
        case ir::Instruction::ExtSB: resU = maskValue((uint64_t)signExtend(u1, 8), width); break;
        case ir::Instruction::ExtSH: resU = maskValue((uint64_t)signExtend(u1, 16), width); break;
        case ir::Instruction::ExtSW: resU = maskValue((uint64_t)signExtend(u1, 32), width); break;
        case ir::Instruction::ExtS:  resU = maskValue((uint64_t)s1, width); break;
        case ir::Instruction::TruncD: resU = maskValue(u1, width); break;

        default: evalSuccess = false; break;
    }

    return evalSuccess;
}

CallKey SCCP::createCallKey(ir::Function* callee, const std::vector<ir::Constant*>& argConstants) {
    CallKey key;
    key.callee = callee;
    for (auto* c : argConstants) {
        if (!c) {
            key.argValues.push_back({nullptr, 0});
            continue;
        }
        ir::Type* ty = c->getType();
        uint64_t valBits = 0;
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(c)) {
            valBits = ci->getValue();
        } else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(c)) {
            double d = cfp->getValue();
            static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
            std::memcpy(&valBits, &d, sizeof(double));
        } else {
            valBits = reinterpret_cast<uint64_t>(c);
        }
        key.argValues.push_back({ty, valBits});
    }
    return key;
}

ir::Constant* SCCP::foldVectorInstruction(
    ir::Instruction* instr,
    const std::vector<ir::Constant*>& opConsts
) {
    if (!instr) return nullptr;
    ir::Instruction::Opcode op = instr->getOpcode();

    auto* resVecTy = dynamic_cast<ir::VectorType*>(instr->getType());

    if (op == ir::Instruction::VBroadcast) {
        if (opConsts.empty() || !opConsts[0]) return nullptr;
        if (!resVecTy) return nullptr;
        ir::Constant* scalarC = opConsts[0];
        std::vector<ir::Constant*> elements(resVecTy->getNumElements(), scalarC);
        return ir::ConstantVector::get(resVecTy, elements);
    }

    if (op == ir::Instruction::VExtract) {
        if (opConsts.size() < 2 || !opConsts[0] || !opConsts[1]) return nullptr;
        auto* vecC = dynamic_cast<ir::ConstantVector*>(opConsts[0]);
        auto* idxC = dynamic_cast<ir::ConstantInt*>(opConsts[1]);
        if (!vecC || !idxC) return nullptr;
        size_t idx = static_cast<size_t>(idxC->getValue());
        if (idx >= vecC->getElements().size()) return nullptr;
        return vecC->getElement(idx);
    }

    if (op == ir::Instruction::VInsert) {
        if (opConsts.size() < 3 || !opConsts[0] || !opConsts[1] || !opConsts[2]) return nullptr;
        auto* vecC = dynamic_cast<ir::ConstantVector*>(opConsts[0]);
        ir::Constant* scalarC = opConsts[1];
        auto* idxC = dynamic_cast<ir::ConstantInt*>(opConsts[2]);
        if (!vecC || !scalarC || !idxC) return nullptr;
        size_t idx = static_cast<size_t>(idxC->getValue());
        if (idx >= vecC->getElements().size()) return nullptr;
        std::vector<ir::Constant*> newElems = vecC->getElements();
        newElems[idx] = scalarC;
        auto* vecTy = dynamic_cast<ir::VectorType*>(vecC->getType());
        if (!vecTy) return nullptr;
        return ir::ConstantVector::get(vecTy, newElems);
    }

    if (op == ir::Instruction::VSelect) {
        if (opConsts.size() < 3 || !opConsts[0] || !opConsts[1] || !opConsts[2]) return nullptr;
        auto* maskC = dynamic_cast<ir::ConstantVector*>(opConsts[0]);
        auto* trueC = dynamic_cast<ir::ConstantVector*>(opConsts[1]);
        auto* falseC = dynamic_cast<ir::ConstantVector*>(opConsts[2]);
        if (!maskC || !trueC || !falseC) return nullptr;
        if (!resVecTy) return nullptr;
        size_t numElem = resVecTy->getNumElements();
        if (maskC->getElements().size() != numElem || trueC->getElements().size() != numElem || falseC->getElements().size() != numElem)
            return nullptr;

        std::vector<ir::Constant*> resElems;
        for (size_t i = 0; i < numElem; ++i) {
            auto* mElem = dynamic_cast<ir::ConstantInt*>(maskC->getElement(i));
            if (!mElem) return nullptr;
            resElems.push_back(mElem->getValue() != 0 ? trueC->getElement(i) : falseC->getElement(i));
        }
        return ir::ConstantVector::get(resVecTy, resElems);
    }

    if (op == ir::Instruction::VCmp) {
        if (opConsts.size() < 3 || !opConsts[0] || !opConsts[1] || !opConsts[2]) return nullptr;
        auto* vec1C = dynamic_cast<ir::ConstantVector*>(opConsts[0]);
        auto* vec2C = dynamic_cast<ir::ConstantVector*>(opConsts[1]);
        auto* predC = dynamic_cast<ir::ConstantInt*>(opConsts[2]);
        if (!vec1C || !vec2C || !predC) return nullptr;
        if (!resVecTy) return nullptr;
        size_t numElem = resVecTy->getNumElements();
        if (vec1C->getElements().size() != numElem || vec2C->getElements().size() != numElem) return nullptr;

        ir::VectorCompareOp pred = static_cast<ir::VectorCompareOp>(predC->getValue());
        auto* elemIntTy = dynamic_cast<ir::IntegerType*>(resVecTy->getElementType());
        if (!elemIntTy) return nullptr;

        std::vector<ir::Constant*> resElems;
        for (size_t i = 0; i < numElem; ++i) {
            ir::Constant* e1 = vec1C->getElement(i);
            ir::Constant* e2 = vec2C->getElement(i);
            bool cond = false;
            if (auto* i1 = dynamic_cast<ir::ConstantInt*>(e1)) {
                auto* i2 = dynamic_cast<ir::ConstantInt*>(e2);
                if (!i2) return nullptr;
                uint64_t u1 = i1->getValue(), u2 = i2->getValue();
                uint64_t bw = getBitWidth(i1->getType());
                int64_t s1 = signExtend(u1, bw), s2 = signExtend(u2, bw);
                switch (pred) {
                    case ir::VectorCompareOp::EQ: cond = (u1 == u2); break;
                    case ir::VectorCompareOp::NE: cond = (u1 != u2); break;
                    case ir::VectorCompareOp::LT: cond = (s1 < s2); break;
                    case ir::VectorCompareOp::LE: cond = (s1 <= s2); break;
                    case ir::VectorCompareOp::GT: cond = (s1 > s2); break;
                    case ir::VectorCompareOp::GE: cond = (s1 >= s2); break;
                    case ir::VectorCompareOp::ULT: cond = (u1 < u2); break;
                    case ir::VectorCompareOp::ULE: cond = (u1 <= u2); break;
                    case ir::VectorCompareOp::UGT: cond = (u1 > u2); break;
                    case ir::VectorCompareOp::UGE: cond = (u1 >= u2); break;
                }
            } else if (auto* f1 = dynamic_cast<ir::ConstantFP*>(e1)) {
                auto* f2 = dynamic_cast<ir::ConstantFP*>(e2);
                if (!f2) return nullptr;
                double d1 = f1->getValue(), d2 = f2->getValue();
                switch (pred) {
                    case ir::VectorCompareOp::EQ: cond = (d1 == d2); break;
                    case ir::VectorCompareOp::NE: cond = (d1 != d2); break;
                    case ir::VectorCompareOp::LT: cond = (d1 < d2); break;
                    case ir::VectorCompareOp::LE: cond = (d1 <= d2); break;
                    case ir::VectorCompareOp::GT: cond = (d1 > d2); break;
                    case ir::VectorCompareOp::GE: cond = (d1 >= d2); break;
                    default: return nullptr;
                }
            } else {
                return nullptr;
            }
            uint64_t maskVal = cond ? ((1ULL << elemIntTy->getBitwidth()) - 1ULL) : 0ULL;
            resElems.push_back(ir::ConstantInt::get(elemIntTy, maskVal));
        }
        return ir::ConstantVector::get(resVecTy, resElems);
    }

    if (opConsts.size() < 2 || !opConsts[0] || !opConsts[1]) return nullptr;
    auto* vec1C = dynamic_cast<ir::ConstantVector*>(opConsts[0]);
    auto* vec2C = dynamic_cast<ir::ConstantVector*>(opConsts[1]);
    if (!vec1C || !vec2C) return nullptr;
    if (!resVecTy) return nullptr;

    size_t numElem = resVecTy->getNumElements();
    if (vec1C->getElements().size() != numElem || vec2C->getElements().size() != numElem) return nullptr;

    std::vector<ir::Constant*> resElems;
    for (size_t i = 0; i < numElem; ++i) {
        ir::Constant* e1 = vec1C->getElement(i);
        ir::Constant* e2 = vec2C->getElement(i);

        if (auto* i1 = dynamic_cast<ir::ConstantInt*>(e1)) {
            auto* i2 = dynamic_cast<ir::ConstantInt*>(e2);
            if (!i2) return nullptr;
            auto* ity = dynamic_cast<ir::IntegerType*>(resVecTy->getElementType());
            if (!ity) return nullptr;
            uint64_t bw = ity->getBitwidth();
            uint64_t u1 = i1->getValue(), u2 = i2->getValue();
            int64_t s1 = signExtend(u1, bw), s2 = signExtend(u2, bw);
            uint64_t resU = 0;

            switch (op) {
                case ir::Instruction::VAdd: resU = maskValue(u1 + u2, bw); break;
                case ir::Instruction::VSub: resU = maskValue(u1 - u2, bw); break;
                case ir::Instruction::VMul: resU = maskValue(u1 * u2, bw); break;
                case ir::Instruction::VAnd: resU = maskValue(u1 & u2, bw); break;
                case ir::Instruction::VOr:  resU = maskValue(u1 | u2, bw); break;
                case ir::Instruction::VXor: resU = maskValue(u1 ^ u2, bw); break;
                case ir::Instruction::VShl: resU = maskValue(u1 << (u2 & 63), bw); break;
                case ir::Instruction::VShr: resU = maskValue(u1 >> (u2 & 63), bw); break;
                case ir::Instruction::VMin: resU = maskValue((s1 < s2 ? s1 : s2), bw); break;
                case ir::Instruction::VMax: resU = maskValue((s1 > s2 ? s1 : s2), bw); break;
                default: return nullptr;
            }
            resElems.push_back(ir::ConstantInt::get(ity, resU));
        } else if (auto* f1 = dynamic_cast<ir::ConstantFP*>(e1)) {
            auto* f2 = dynamic_cast<ir::ConstantFP*>(e2);
            if (!f2) return nullptr;
            auto* fty = resVecTy->getElementType();
            double d1 = f1->getValue(), d2 = f2->getValue();
            double resD = 0;

            switch (op) {
                case ir::Instruction::VFAdd: resD = d1 + d2; break;
                case ir::Instruction::VFSub: resD = d1 - d2; break;
                case ir::Instruction::VFMul: resD = d1 * d2; break;
                case ir::Instruction::VFDiv: if (d2 == 0.0) return nullptr; resD = d1 / d2; break;
                case ir::Instruction::VFMin: resD = std::min(d1, d2); break;
                case ir::Instruction::VFMax: resD = std::max(d1, d2); break;
                default: return nullptr;
            }
            resElems.push_back(ir::ConstantFP::get(fty, resD));
        } else {
            return nullptr;
        }
    }
    return ir::ConstantVector::get(resVecTy, resElems);
}

bool SCCP::isFunctionPure(ir::Function* func, std::unordered_set<ir::Function*>& activeVisiting) {
    if (!func || func->getBasicBlocks().empty()) return false;

    auto it = evalCtx.purityCache.find(func);
    if (it != evalCtx.purityCache.end()) return it->second;

    if (!activeVisiting.insert(func).second) {
        return true;
    }

    bool pure = true;
    for (const auto& bbPtr : func->getBasicBlocks()) {
        if (!pure) break;
        for (const auto& instPtr : bbPtr->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            ir::Instruction::Opcode op = inst->getOpcode();

            if (op == ir::Instruction::Alloc || op == ir::Instruction::Alloc4 || op == ir::Instruction::Alloc16 ||
                op == ir::Instruction::Load || op == ir::Instruction::Loadd || op == ir::Instruction::Loads ||
                op == ir::Instruction::Loadl || op == ir::Instruction::Loaduw || op == ir::Instruction::Loadsh ||
                op == ir::Instruction::Loaduh || op == ir::Instruction::Loadsb || op == ir::Instruction::Loadub ||
                op == ir::Instruction::Store || op == ir::Instruction::Stored || op == ir::Instruction::Stores ||
                op == ir::Instruction::Storel || op == ir::Instruction::Storeh || op == ir::Instruction::Storeb ||
                op == ir::Instruction::Syscall || op == ir::Instruction::ExternCall ||
                op == ir::Instruction::VAStart || op == ir::Instruction::VAArg ||
                op == ir::Instruction::Blit || op == ir::Instruction::Hlt) {
                pure = false;
                break;
            }

            if (op == ir::Instruction::Call) {
                if (inst->getOperands().empty()) { pure = false; break; }
                ir::Function* callee = dynamic_cast<ir::Function*>(inst->getOperands()[0]->get());
                if (!callee || !isFunctionPure(callee, activeVisiting)) {
                    pure = false;
                    break;
                }
            }
        }
    }

    activeVisiting.erase(func);
    evalCtx.purityCache[func] = pure;
    return pure;
}

struct FastOpInfo {
    bool isConst[4] = {false, false, false, false};
    ir::Constant* constVal[4] = {nullptr, nullptr, nullptr, nullptr};
    int srcInstIdx[4] = {-1, -1, -1, -1};
    size_t numOps = 0;
};

struct ExecInst {
    ir::Instruction* rawInst = nullptr;
    ir::Instruction::Opcode op;
    uint64_t bw = 32;
    ir::ConstantInt* cObj = nullptr;
    FastOpInfo opInfo;
};

ir::Constant* SCCP::evaluatePureFunctionCall(
    ir::Function* callee,
    const std::vector<ir::Constant*>& argConstants,
    int depth,
    int& callStepCount,
    int& callBackedgeCount
) {
    if (!callee || callee->getBasicBlocks().empty()) return nullptr;
    if (depth > evalCtx.maxRecursionDepth) return nullptr;

    std::unordered_set<ir::Function*> activeVisiting;
    if (!isFunctionPure(callee, activeVisiting)) return nullptr;

    CallKey key = createCallKey(callee, argConstants);
    if (depth == 0) {
        auto cacheIt = evalCtx.evalCache.find(key);
        if (cacheIt != evalCtx.evalCache.end()) {
            return cacheIt->second;
        }
    }

    // Assign integer index to each instruction in callee for flat array lookup
    std::unordered_map<ir::Instruction*, int> instIdxMap;
    int instCounter = 0;
    for (const auto& bbPtr : callee->getBasicBlocks()) {
        for (const auto& instPtr : bbPtr->getInstructions()) {
            if (instPtr) {
                instIdxMap[instPtr.get()] = instCounter++;
            }
        }
    }

    std::vector<ExecInst> execList(instCounter);
    std::vector<std::unique_ptr<ir::ConstantInt>> frameValObjects;
    std::vector<ir::Constant*> frameValArray(instCounter, nullptr);

    const auto& params = callee->getParameters();

    for (const auto& bbPtr : callee->getBasicBlocks()) {
        for (const auto& instPtr : bbPtr->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            int idx = instIdxMap[inst];
            ExecInst& ei = execList[idx];
            ei.rawInst = inst;
            ei.op = inst->getOpcode();
            ei.bw = getBitWidth(inst->getType());

            if (inst->getType() && !inst->getType()->isVoidTy()) {
                auto* ity = dynamic_cast<ir::IntegerType*>(inst->getType());
                if (!ity) ity = ir::IntegerType::get(ei.bw);
                auto cObj = std::unique_ptr<ir::ConstantInt>(new ir::ConstantInt(ity, 0));
                ei.cObj = cObj.get();
                frameValObjects.push_back(std::move(cObj));
            }

            size_t numOps = std::min(inst->getOperands().size(), (size_t)4);
            ei.opInfo.numOps = numOps;
            for (size_t oIdx = 0; oIdx < numOps; ++oIdx) {
                ir::Value* v = inst->getOperands()[oIdx] ? inst->getOperands()[oIdx]->get() : nullptr;
                if (auto* c = dynamic_cast<ir::Constant*>(v)) {
                    ei.opInfo.isConst[oIdx] = true;
                    ei.opInfo.constVal[oIdx] = c;
                } else if (auto* param = dynamic_cast<ir::Parameter*>(v)) {
                    size_t pIdx = 0;
                    for (auto pIt = params.begin(); pIt != params.end(); ++pIt, ++pIdx) {
                        if (pIt->get() == param) {
                            if (pIdx < argConstants.size()) {
                                ei.opInfo.isConst[oIdx] = true;
                                ei.opInfo.constVal[oIdx] = argConstants[pIdx];
                            }
                            break;
                        }
                    }
                } else if (auto* srcInst = dynamic_cast<ir::Instruction*>(v)) {
                    ei.opInfo.isConst[oIdx] = false;
                    ei.opInfo.srcInstIdx[oIdx] = instIdxMap[srcInst];
                }
            }
        }
    }

    auto& backedges = evalCtx.backedgeCache[callee];
    if (evalCtx.domTreeCache.find(callee) == evalCtx.domTreeCache.end()) {
        auto domTree = std::make_shared<DominatorTree>();
        domTree->run(*callee);
        evalCtx.domTreeCache[callee] = domTree;

        for (const auto& bbPtr : callee->getBasicBlocks()) {
            ir::BasicBlock* bb = bbPtr.get();
            for (auto* succ : bb->getSuccessors()) {
                if (domTree->dominates(succ, bb)) {
                    backedges.insert({bb, succ});
                }
            }
        }
    }

    std::unordered_map<ir::Value*, ir::Constant*> frame;
    size_t pIdx = 0;
    for (auto pIt = params.begin(); pIt != params.end() && pIdx < argConstants.size(); ++pIt, ++pIdx) {
        frame[pIt->get()] = argConstants[pIdx];
    }

    ir::BasicBlock* currentBB = callee->getBasicBlocks().front().get();
    ir::BasicBlock* prevBB = nullptr;

    std::vector<ir::Constant*> fastOpConsts(4, nullptr);

    while (currentBB) {
        ir::BasicBlock* nextBB = nullptr;
        bool blockTerminated = false;

        for (auto& instPtr : currentBB->getInstructions()) {
            callStepCount++;
            evalCtx.totalModuleInstructionCount++;

            if (callStepCount > evalCtx.maxCallInstructionBudget ||
                evalCtx.totalModuleInstructionCount > evalCtx.maxModuleInstructionBudget) {
                return nullptr;
            }

            ir::Instruction* instr = instPtr.get();
            if (!instr) continue;

            int instIdx = instIdxMap[instr];
            const ExecInst& ei = execList[instIdx];
            ir::Instruction::Opcode op = ei.op;

            if (op == ir::Instruction::Phi) {
                ir::PhiNode* phi = static_cast<ir::PhiNode*>(instr);
                ir::Value* incomingVal = nullptr;
                if (prevBB) {
                    incomingVal = phi->getIncomingValueForBlock(prevBB);
                }
                if (!incomingVal) {
                    for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
                        ir::Value* op1 = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
                        ir::Value* op2 = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                        ir::BasicBlock* p = dynamic_cast<ir::BasicBlock*>(op1);
                        ir::Value* v = op2;
                        if (!p) { p = dynamic_cast<ir::BasicBlock*>(op2); v = op1; }
                        if (p == prevBB) { incomingVal = v; break; }
                    }
                }
                if (!incomingVal) return nullptr;

                ir::Constant* constVal = nullptr;
                if (auto* c = dynamic_cast<ir::Constant*>(incomingVal)) constVal = c;
                else if (frame.count(incomingVal)) constVal = frame[incomingVal];
                else if (auto* srcInst = dynamic_cast<ir::Instruction*>(incomingVal)) {
                    int srcIdx = instIdxMap[srcInst];
                    constVal = frameValArray[srcIdx];
                }

                if (!constVal) return nullptr;
                if (ei.cObj && dynamic_cast<ir::ConstantInt*>(constVal)) {
                    ei.cObj->value = static_cast<ir::ConstantInt*>(constVal)->getValue();
                    frame[instr] = ei.cObj;
                    frameValArray[instIdx] = ei.cObj;
                } else {
                    frame[instr] = constVal;
                    frameValArray[instIdx] = constVal;
                }
                continue;
            }

            if (op == ir::Instruction::Jmp) {
                if (instr->getOperands().empty()) return nullptr;
                nextBB = dynamic_cast<ir::BasicBlock*>(instr->getOperands()[0]->get());
                if (!nextBB) return nullptr;
                blockTerminated = true;
                break;
            }

            if (op == ir::Instruction::Br || op == ir::Instruction::Jnz || op == ir::Instruction::Jz) {
                if (instr->getOperands().empty()) return nullptr;
                ir::Value* condVal = instr->getOperands()[0]->get();
                ir::ConstantInt* condCI = nullptr;
                if (auto* c = dynamic_cast<ir::ConstantInt*>(condVal)) condCI = c;
                else if (frame.count(condVal)) condCI = dynamic_cast<ir::ConstantInt*>(frame[condVal]);
                else if (auto* srcInst = dynamic_cast<ir::Instruction*>(condVal)) {
                    int srcIdx = instIdxMap[srcInst];
                    condCI = dynamic_cast<ir::ConstantInt*>(frameValArray[srcIdx]);
                }

                if (!condCI) return nullptr;

                ir::BasicBlock* t_dest = dynamic_cast<ir::BasicBlock*>(instr->getOperands()[1]->get());
                ir::BasicBlock* f_dest = (instr->getOperands().size() > 2) ? dynamic_cast<ir::BasicBlock*>(instr->getOperands()[2]->get()) : nullptr;

                bool is_true = (op == ir::Instruction::Jz) ? (condCI->getValue() == 0) : (condCI->getValue() != 0);
                nextBB = is_true ? t_dest : f_dest;
                if (!nextBB) return nullptr;
                blockTerminated = true;
                break;
            }

            if (op == ir::Instruction::Ret) {
                if (instr->getOperands().empty()) return nullptr;
                ir::Value* retVal = instr->getOperands()[0]->get();
                ir::Constant* rawVal = nullptr;
                if (auto* c = dynamic_cast<ir::Constant*>(retVal)) rawVal = c;
                else if (frame.count(retVal)) rawVal = frame[retVal];
                else if (auto* srcInst = dynamic_cast<ir::Instruction*>(retVal)) {
                    int srcIdx = instIdxMap[srcInst];
                    rawVal = frameValArray[srcIdx];
                }

                if (rawVal) {
                    ir::Constant* finalRes = rawVal;
                    if (auto* ci = dynamic_cast<ir::ConstantInt*>(rawVal)) {
                        auto* ity = dynamic_cast<ir::IntegerType*>(ci->getType());
                        if (!ity) ity = ir::IntegerType::get(getBitWidth(ci->getType()));
                        finalRes = ir::ConstantInt::get(ity, ci->getValue());
                    }
                    if (depth == 0) {
                        evalCtx.evalCache[key] = finalRes;
                    }
                    return finalRes;
                }
                return nullptr;
            }

            if (op == ir::Instruction::Call) {
                if (instr->getOperands().empty()) return nullptr;
                ir::Function* subCallee = dynamic_cast<ir::Function*>(instr->getOperands()[0]->get());
                if (!subCallee) return nullptr;

                std::vector<ir::Constant*> subArgs;
                for (size_t i = 1; i < instr->getOperands().size(); ++i) {
                    ir::Value* argVal = instr->getOperands()[i]->get();
                    ir::Constant* c = nullptr;
                    if (auto* ci = dynamic_cast<ir::Constant*>(argVal)) c = ci;
                    else if (frame.count(argVal)) c = frame[argVal];
                    else if (auto* srcInst = dynamic_cast<ir::Instruction*>(argVal)) {
                        int srcIdx = instIdxMap[srcInst];
                        c = frameValArray[srcIdx];
                    }
                    if (!c) return nullptr;
                    subArgs.push_back(c);
                }

                ir::Constant* res = evaluatePureFunctionCall(subCallee, subArgs, depth + 1, callStepCount, callBackedgeCount);
                if (!res) return nullptr;
                frame[instr] = res;
                frameValArray[instIdx] = res;
                continue;
            }

            fastOpConsts.resize(ei.opInfo.numOps);
            for (size_t oIdx = 0; oIdx < ei.opInfo.numOps; ++oIdx) {
                if (ei.opInfo.isConst[oIdx]) {
                    fastOpConsts[oIdx] = ei.opInfo.constVal[oIdx];
                } else {
                    int srcIdx = ei.opInfo.srcInstIdx[oIdx];
                    fastOpConsts[oIdx] = (srcIdx >= 0 && srcIdx < (int)frameValArray.size()) ? frameValArray[srcIdx] : nullptr;
                }
            }

            if (ir::Constant* vecRes = foldVectorInstruction(instr, fastOpConsts)) {
                frame[instr] = vecRes;
                frameValArray[instIdx] = vecRes;
                continue;
            }

            uint64_t resU = 0;
            if (computeScalarOpValueFast(op, ei.bw, fastOpConsts, resU)) {
                if (ei.cObj) {
                    ei.cObj->value = resU;
                    frame[instr] = ei.cObj;
                    frameValArray[instIdx] = ei.cObj;
                } else {
                    auto* ity = dynamic_cast<ir::IntegerType*>(instr->getType());
                    if (!ity) ity = ir::IntegerType::get(ei.bw);
                    auto* cRes = ir::ConstantInt::get(ity, resU);
                    frame[instr] = cRes;
                    frameValArray[instIdx] = cRes;
                }
                continue;
            }

            return nullptr;
        }

        if (!blockTerminated) return nullptr;

        if (nextBB && backedges.count({currentBB, nextBB})) {
            callBackedgeCount++;
            if (callBackedgeCount > evalCtx.maxCallIterationBudget) {
                return nullptr;
            }
        }

        prevBB = currentBB;
        currentBB = nextBB;
    }

    return nullptr;
}

bool SCCP::performTransformation(ir::Function& func) {
    evalCtx.reset();

    this->initialize(func);

    std::set<ir::BasicBlock*> executableBlocks;
    std::set<std::pair<ir::BasicBlock*, ir::BasicBlock*>> executableEdges;
    std::unordered_set<ir::Instruction*> inInstructionWorklist;

    if (func.getBasicBlocks().empty()) return false;
    ir::BasicBlock* entry = func.getBasicBlocks().front().get();

    blockWorklist.push_back(entry);

    while (!blockWorklist.empty() || !instructionWorklist.empty()) {
        while (!blockWorklist.empty()) {
            ir::BasicBlock* bb = blockWorklist.back();
            blockWorklist.pop_back();

            if (executableBlocks.insert(bb).second) {
                for (auto& instr : bb->getInstructions()) {
                    this->visit(instr.get(), executableEdges, executableBlocks, inInstructionWorklist);
                }
            } else {
                for (auto& instr : bb->getInstructions()) {
                    if (instr->getOpcode() == ir::Instruction::Phi) {
                         this->visit(instr.get(), executableEdges, executableBlocks, inInstructionWorklist);
                    } else break;
                }
            }
        }

        if (!instructionWorklist.empty()) {
            ir::Instruction* instr = instructionWorklist.back();
            instructionWorklist.pop_back();
            inInstructionWorklist.erase(instr);

            if (instr->getParent() && executableBlocks.count(instr->getParent())) {
                this->visit(instr, executableEdges, executableBlocks, inInstructionWorklist);
            }
        }
    }

    bool changed = false;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        if (!bb || !executableBlocks.count(bb)) continue;

        auto& instrs = bb->getInstructions();
        auto it = instrs.begin();
        while (it != instrs.end()) {
            ir::Instruction* instr = it->get();

            ir::Instruction::Opcode op = instr->getOpcode();
            if (op == ir::Instruction::Ret || op == ir::Instruction::Br ||
                op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
                op == ir::Instruction::Jz || op == ir::Instruction::Phi) {
                ++it;
                continue;
            }

            auto entry = getLatticeValue(instr);
            if (entry.type == Constant && entry.constant) {
                instr->replaceAllUsesWith(entry.constant);
                it = instrs.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }
    }
    return changed;
}

void SCCP::initialize(ir::Function& func) {
    lattice.clear();
    instructionWorklist.clear();
    blockWorklist.clear();
    for (auto& bb_ptr : func.getBasicBlocks()) {
        for (auto& instr : bb_ptr->getInstructions()) {
            if (instr->getType() && !instr->getType()->isVoidTy()) {
                lattice[instr.get()] = {Top, nullptr};
            }
        }
    }
}

SCCP::LatticeEntry SCCP::getLatticeValue(ir::Value* val) {
    if (auto* c = dynamic_cast<ir::Constant*>(val)) return {Constant, c};
    if (lattice.count(val)) return lattice[val];
    return {Bottom, nullptr};
}

void SCCP::setLatticeValue(ir::Instruction* instr, LatticeEntry new_val, std::unordered_set<ir::Instruction*>& inInstructionWorklist) {
    if (!instr || !instr->getType() || instr->getType()->isVoidTy()) return;

    LatticeEntry& old_val = lattice[instr];

    if (old_val.type == Bottom) return;
    if (new_val.type == Top) return;
    if (old_val.type == Constant && new_val.type == Constant) {
        if (old_val.constant == new_val.constant) return;
        new_val.type = Bottom;
        new_val.constant = nullptr;
    }

    if (old_val.type != new_val.type || old_val.constant != new_val.constant) {
        old_val = new_val;
        for (auto& use : instr->getUseList()) {
            if (auto* user_instr = dynamic_cast<ir::Instruction*>(use->getUser())) {
                if (inInstructionWorklist.insert(user_instr).second) {
                    instructionWorklist.push_back(user_instr);
                }
            }
        }
    }
}

void SCCP::visit(ir::Instruction* instr, std::set<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& executableEdges, std::set<ir::BasicBlock*>& executableBlocks, std::unordered_set<ir::Instruction*>& inInstructionWorklist) {
    ir::Instruction::Opcode op = instr->getOpcode();

    if (op == ir::Instruction::Phi) {
        ir::PhiNode* phi = static_cast<ir::PhiNode*>(instr);
        LatticeEntry result = {Top, nullptr};
        bool all_preds_executable = true;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* op1 = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* op2 = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            ir::BasicBlock* pred = dynamic_cast<ir::BasicBlock*>(op1);
            ir::Value* incVal = op2;
            if (!pred) {
                pred = dynamic_cast<ir::BasicBlock*>(op2);
                incVal = op1;
            }
            if (!pred || !incVal) {
                all_preds_executable = false;
                continue;
            }

            if (executableEdges.count({pred, phi->getParent()})) {
                LatticeEntry val = getLatticeValue(incVal);
                if (val.type == Bottom) { result = {Bottom, nullptr}; break; }
                if (val.type == Constant) {
                    if (result.type == Top) result = val;
                    else if (result.constant != val.constant) { result = {Bottom, nullptr}; break; }
                }
            } else {
                all_preds_executable = false;
            }
        }
        if (!all_preds_executable) {
            result = {Bottom, nullptr};
        }
        setLatticeValue(phi, result, inInstructionWorklist);
        return;
    }

    if (op == ir::Instruction::Jmp) {
        ir::BasicBlock* target = static_cast<ir::BasicBlock*>(instr->getOperands()[0]->get());
        if (executableEdges.insert({instr->getParent(), target}).second) {
            blockWorklist.push_back(target);
        }
        return;
    }

    if (op == ir::Instruction::Br || op == ir::Instruction::Jnz || op == ir::Instruction::Jz) {
        LatticeEntry cond = getLatticeValue(instr->getOperands()[0]->get());
        ir::BasicBlock* t_dest = static_cast<ir::BasicBlock*>(instr->getOperands()[1]->get());
        ir::BasicBlock* f_dest = (instr->getOperands().size() > 2) ? static_cast<ir::BasicBlock*>(instr->getOperands()[2]->get()) : nullptr;

        if (cond.type == Constant) {
            int64_t val = static_cast<ir::ConstantInt*>(cond.constant)->getValue();
            bool is_true = (op == ir::Instruction::Jz) ? (val == 0) : (val != 0);
            ir::BasicBlock* taken = is_true ? t_dest : f_dest;
            if (taken && executableEdges.insert({instr->getParent(), taken}).second) {
                blockWorklist.push_back(taken);
            }
        } else if (cond.type == Bottom) {
            if (t_dest && executableEdges.insert({instr->getParent(), t_dest}).second) blockWorklist.push_back(t_dest);
            if (f_dest && executableEdges.insert({instr->getParent(), f_dest}).second) blockWorklist.push_back(f_dest);
        }
        return;
    }

    if (op == ir::Instruction::Call) {
        if (!instr->getOperands().empty()) {
            ir::Function* callee = dynamic_cast<ir::Function*>(instr->getOperands()[0]->get());
            if (callee) {
                bool all_args_const = true;
                std::vector<ir::Constant*> arg_consts;
                for (size_t i = 1; i < instr->getOperands().size(); ++i) {
                    LatticeEntry arg_lat = getLatticeValue(instr->getOperands()[i]->get());
                    if (arg_lat.type == Constant && arg_lat.constant) {
                        arg_consts.push_back(arg_lat.constant);
                    } else {
                        all_args_const = false;
                        break;
                    }
                }
                if (all_args_const) {
                    int callStepCount = 0;
                    int callBackedgeCount = 0;
                    ir::Constant* eval_res = evaluatePureFunctionCall(callee, arg_consts, 0, callStepCount, callBackedgeCount);
                    if (eval_res) {
                        setLatticeValue(instr, {Constant, eval_res}, inInstructionWorklist);
                        return;
                    }
                }
            }
        }
        setLatticeValue(instr, {Bottom, nullptr}, inInstructionWorklist);
        return;
    }

    if (op == ir::Instruction::Syscall || op == ir::Instruction::ExternCall ||
        op == ir::Instruction::Alloc || op == ir::Instruction::Load ||
        op == ir::Instruction::Store || op == ir::Instruction::VAArg) {
        setLatticeValue(instr, {Bottom, nullptr}, inInstructionWorklist);
        return;
    }

    if (instr->getOperands().empty()) return;

    bool all_const = true;
    bool any_bottom = false;
    std::vector<LatticeEntry> op_vals;
    for (auto& op_use : instr->getOperands()) {
        LatticeEntry v = getLatticeValue(op_use->get());
        if (v.type == Bottom) any_bottom = true;
        if (v.type != Constant) all_const = false;
        op_vals.push_back(v);
    }

    if (any_bottom) {
        setLatticeValue(instr, {Bottom, nullptr}, inInstructionWorklist);
    } else if (all_const) {
        std::vector<ir::Constant*> opConsts;
        for (const auto& v : op_vals) opConsts.push_back(v.constant);

        if (ir::Constant* vecRes = foldVectorInstruction(instr, opConsts)) {
            setLatticeValue(instr, {Constant, vecRes}, inInstructionWorklist);
            return;
        }

        if (ir::Constant* scalRes = foldScalarInstruction(op, instr->getType(), opConsts)) {
            setLatticeValue(instr, {Constant, scalRes}, inInstructionWorklist);
            return;
        }

        setLatticeValue(instr, {Bottom, nullptr}, inInstructionWorklist);
    }
}

bool SCCP::validatePreconditions(ir::Function& f) { return !f.getBasicBlocks().empty(); }

} // namespace transforms
