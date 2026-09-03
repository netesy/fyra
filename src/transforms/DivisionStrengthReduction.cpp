#include "transforms/DivisionStrengthReduction.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include <cmath>
#include <vector>
#include <iostream>

namespace transforms {

DivisionStrengthReduction::UnsignedMagic DivisionStrengthReduction::computeUnsignedMagic32(uint32_t d) {
    UnsignedMagic m;
    uint64_t nc = ((1ULL << 32) - 1) - ((1ULL << 32) - 1) % d;
    uint32_t p = 31;
    uint64_t q1 = (1ULL << p) / nc;
    uint64_t r1 = (1ULL << p) - q1 * nc;
    uint64_t q2 = (1ULL << p) / d;
    uint64_t r2 = (1ULL << p) - q2 * d;

    do {
        p++;
        if (r1 >= nc - r1) {
            q1 = 2 * q1 + 1;
            r1 = 2 * r1 - nc;
        } else {
            q1 = 2 * q1;
            r1 = 2 * r1;
        }
        if (r2 >= d - r2) {
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 - d;
        } else {
            q2 = 2 * q2;
            r2 = 2 * r2;
        }
        uint64_t delta = d - 1 - r2;
        if (p < 64 && (1ULL << p) <= delta * (q1 + 1)) {
            // continue loop
        } else {
            break;
        }
    } while (p < 64);

    m.magic = q2 + 1;
    if (m.magic < (1ULL << 32)) {
        m.add_indicator = false;
        m.shift = p - 32;
    } else {
        m.add_indicator = true;
        m.shift = p - 32;
    }
    return m;
}

DivisionStrengthReduction::SignedMagic DivisionStrengthReduction::computeSignedMagic32(int32_t d_in) {
    SignedMagic m;
    uint32_t d = (d_in < 0) ? -d_in : d_in;
    uint32_t ad = d;
    uint32_t anc = (1U << 31) - 1 - (1U << 31) % ad;
    uint32_t p = 31;
    uint64_t q1 = (1ULL << p) / anc;
    uint64_t r1 = (1ULL << p) - q1 * anc;
    uint64_t q2 = (1ULL << p) / ad;
    uint64_t r2 = (1ULL << p) - q2 * ad;

    do {
        p++;
        if (r1 >= anc - r1) {
            q1 = 2 * q1 + 1;
            r1 = 2 * r1 - anc;
        } else {
            q1 = 2 * q1;
            r1 = 2 * r1;
        }
        if (r2 >= ad - r2) {
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 - ad;
        } else {
            q2 = 2 * q2;
            r2 = 2 * r2;
        }
        uint64_t delta = ad - r2;
        if (p < 64 && (1ULL << p) <= delta * (q1 + 1)) {
            // continue
        } else {
            break;
        }
    } while (p < 64);

    int64_t magic = q2 + 1;
    if (d_in < 0) magic = -magic;
    m.magic = magic;
    m.shift = p - 32;
    return m;
}

bool DivisionStrengthReduction::performTransformation(ir::Function& func) {
    bool changed = false;

    for (auto& bb_ptr : func.getBasicBlocks()) {
        auto& instrs = bb_ptr->getInstructions();
        for (auto it = instrs.begin(); it != instrs.end(); ) {
            ir::Instruction* instr = it->get();
            ir::Instruction::Opcode opc = instr->getOpcode();
            if (opc == ir::Instruction::Div || opc == ir::Instruction::Udiv ||
                opc == ir::Instruction::Rem || opc == ir::Instruction::Urem) {

                auto cur_it = it;
                it++; // Advance iterator before possible instruction removal

                ir::IRBuilder builder;
                builder.setInsertPoint(bb_ptr.get(), cur_it);

                if (processInstruction(instr, builder, func)) {
                    changed = true;
                }
            } else {
                it++;
            }
        }
    }

    return changed;
}

bool DivisionStrengthReduction::processInstruction(ir::Instruction* instr, ir::IRBuilder& builder, ir::Function& func) {
    if (instr->getOperands().size() < 2) return false;

    ir::Value* N = instr->getOperands()[0]->get();
    ir::Value* D_val = instr->getOperands()[1]->get();
    ir::ConstantInt* C = dynamic_cast<ir::ConstantInt*>(D_val);
    if (!C) return false;

    ir::Type* rawType = instr->getType();
    ir::IntegerType* type = dynamic_cast<ir::IntegerType*>(rawType);
    if (!type) return false;

    uint32_t bitWidth = type->getBitwidth();
    if (bitWidth != 32 && bitWidth != 64) return false;

    ir::Instruction::Opcode opc = instr->getOpcode();
    bool isSigned = (opc == ir::Instruction::Div || opc == ir::Instruction::Rem);
    bool isRem = (opc == ir::Instruction::Rem || opc == ir::Instruction::Urem);

    int64_t constVal = C->getValue();
    if (constVal == 0) return false; // Division by zero undefined

    ir::Value* Q = nullptr;

    if (!isSigned) {
        uint64_t uD = static_cast<uint64_t>(constVal);
        if (bitWidth == 32) uD &= 0xFFFFFFFFULL;

        if (uD == 1) {
            Q = N;
        } else if ((uD & (uD - 1)) == 0) {
            // Power of two
            uint32_t shift = 0;
            while ((1ULL << shift) < uD) shift++;
            ir::ConstantInt* shiftConst = ir::ConstantInt::get(type, shift);
            Q = builder.createShr(N, shiftConst);
        } else if (bitWidth == 32) {
            UnsignedMagic m = computeUnsignedMagic32(static_cast<uint32_t>(uD));

            // High multiply: mul N, magic
            ir::IntegerType* i64Ty = ir::IntegerType::get(64);
            ir::Value* nExt = builder.createExtUW(N, i64Ty);
            ir::Value* mExt = ir::ConstantInt::get(i64Ty, m.magic);
            ir::Value* mul64 = builder.createMul(nExt, mExt);
            ir::Value* c32 = ir::ConstantInt::get(i64Ty, 32);
            ir::Value* high64 = builder.createShr(mul64, c32);
            ir::Value* high32 = builder.createTruncD(high64, type); // trunc to 32

            if (m.add_indicator) {
                // (N - high32) >> 1 + high32 >> m.shift
                ir::Value* sub = builder.createSub(N, high32);
                ir::Value* c1 = ir::ConstantInt::get(type, 1);
                ir::Value* shr1 = builder.createShr(sub, c1);
                ir::Value* add1 = builder.createAdd(high32, shr1);
                ir::ConstantInt* shiftConst = ir::ConstantInt::get(type, m.shift - 1);
                Q = builder.createShr(add1, shiftConst);
            } else {
                ir::ConstantInt* shiftConst = ir::ConstantInt::get(type, m.shift);
                Q = builder.createShr(high32, shiftConst);
            }
        }
    } else {
        // Signed division
        int64_t sD = constVal;
        if (sD == 1) {
            Q = N;
        } else if (sD == -1) {
            ir::Value* zero = ir::ConstantInt::get(type, 0);
            Q = builder.createSub(zero, N);
        } else if (bitWidth == 32) {
            int32_t d32 = static_cast<int32_t>(sD);
            uint32_t absD = (d32 < 0) ? -d32 : d32;
            if ((absD & (absD - 1)) == 0) {
                // Power of 2 signed division
                uint32_t shift = 0;
                while ((1U << shift) < absD) shift++;

                // Add sign adjustment: (N < 0 ? absD - 1 : 0)
                // In Fyra IR: (N >> 31) u>> (32 - shift)
                ir::ConstantInt* c31 = ir::ConstantInt::get(type, 31);
                ir::Value* sign = builder.createSar(N, c31);
                ir::ConstantInt* cMask = ir::ConstantInt::get(type, (32 - shift));
                ir::Value* adj = builder.createShr(sign, cMask);
                ir::Value* nAdj = builder.createAdd(N, adj);
                ir::ConstantInt* cShift = ir::ConstantInt::get(type, shift);
                ir::Value* qAbs = builder.createSar(nAdj, cShift);

                if (d32 < 0) {
                    ir::Value* zero = ir::ConstantInt::get(type, 0);
                    Q = builder.createSub(zero, qAbs);
                } else {
                    Q = qAbs;
                }
            } else {
                SignedMagic m = computeSignedMagic32(d32);
                ir::IntegerType* i64Ty = ir::IntegerType::get(64);
                ir::Value* nExt = builder.createExtSW(N, i64Ty);
                ir::Value* mExt = ir::ConstantInt::get(i64Ty, static_cast<uint64_t>(m.magic) & 0xFFFFFFFFULL);
                ir::Value* mul64 = builder.createMul(nExt, mExt);
                ir::Value* c32 = ir::ConstantInt::get(i64Ty, 32);
                ir::Value* high64 = builder.createSar(mul64, c32); // Signed shift for high 32
                ir::Value* high32 = builder.createTruncD(high64, type);

                if (d32 > 0 && m.magic < 0) {
                    high32 = builder.createAdd(high32, N);
                } else if (d32 < 0 && m.magic > 0) {
                    high32 = builder.createSub(high32, N);
                }

                if (m.shift > 0) {
                    ir::ConstantInt* shiftConst = ir::ConstantInt::get(type, m.shift);
                    high32 = builder.createSar(high32, shiftConst);
                }

                // Add 1 if N < 0 (sign bit)
                ir::ConstantInt* c31 = ir::ConstantInt::get(type, 31);
                ir::Value* signBit = builder.createShr(N, c31);
                Q = builder.createAdd(high32, signBit);
            }
        }
    }

    if (!Q) return false;

    ir::Value* finalVal = Q;
    if (isRem) {
        // Remainder = N - Q * D
        ir::Value* prod = builder.createMul(Q, C);
        finalVal = builder.createSub(N, prod);
    }

    instr->replaceAllUsesWith(finalVal);
    // Remove instruction from basic block
    instr->getParent()->removeInstructions({instr});
    return true;
}

} // namespace transforms
