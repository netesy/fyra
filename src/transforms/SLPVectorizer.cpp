#include "transforms/SLPVectorizer.h"
#include "ir/BasicBlock.h"
#include "ir/Constant.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "ir/SIMDInstruction.h"
#include "ir/Use.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetResolver.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

namespace transforms {
namespace {

using O = ir::Instruction::Opcode;

void diag(const std::string& message) {
    if (std::getenv("FYRA_SLP_DIAG"))
        std::cout << "[SLPVectorizer Diag] " << message << '\n';
}

bool scalarOpcode(O op) {
    return op == O::Add || op == O::Sub || op == O::Mul ||
           op == O::FAdd || op == O::FSub || op == O::FMul ||
           op == O::And || op == O::Or || op == O::Xor ||
           op == O::SMin || op == O::SMax;
}

bool isLoad(O op) { return op == O::Load || op == O::Loads || op == O::Loadd; }
bool isStore(O op) { return op == O::Store || op == O::Stores || op == O::Stored; }

bool isSLPBarrier(const ir::Instruction& inst) {
    switch (inst.getOpcode()) {
        case O::Call: case O::ExternCall: case O::Syscall: case O::VAStart: case O::VAArg:
        case O::Blit: case O::Hlt: case O::Br: case O::Jmp: case O::Jz: case O::Jnz:
            return true;
        default: return false;
    }
}

struct Address {
    ir::Value* base = nullptr;
    int64_t offset = 0;
};

bool normalizeAddress(ir::Value* value, Address& address) {
    auto* add = dynamic_cast<ir::Instruction*>(value);
    if (add && add->getOpcode() == O::Add && add->getOperands().size() == 2) {
        auto* lhsConstant = dynamic_cast<ir::ConstantInt*>(add->getOperands()[0]->get());
        auto* rhsConstant = dynamic_cast<ir::ConstantInt*>(add->getOperands()[1]->get());
        if (lhsConstant && !rhsConstant) {
            address.base = add->getOperands()[1]->get();
            address.offset = static_cast<int64_t>(lhsConstant->getValue());
            return true;
        }
        if (rhsConstant && !lhsConstant) {
            address.base = add->getOperands()[0]->get();
            address.offset = static_cast<int64_t>(rhsConstant->getValue());
            return true;
        }
        return false;
    }
    address.base = value;
    address.offset = 0;
    return value != nullptr;
}

bool contiguous(const std::vector<ir::Instruction*>& accesses, bool loads, ir::Type* element,
                SLPVectorizer::SLPMemoryPack& pack) {
    if (accesses.empty() || !element) return false;
    const size_t addressOperand = loads ? 0 : 1;
    Address first;
    if (!normalizeAddress(accesses[0]->getOperands()[addressOperand]->get(), first)) return false;
    const int64_t size = static_cast<int64_t>(element->getSize());
    for (size_t lane = 0; lane < accesses.size(); ++lane) {
        Address current;
        if (!normalizeAddress(accesses[lane]->getOperands()[addressOperand]->get(), current) ||
            current.base != first.base || current.offset != first.offset + static_cast<int64_t>(lane) * size)
            return false;
    }
    pack.base = first.base;
    pack.firstOffset = first.offset;
    pack.elementSize = size;
    pack.lanes = accesses.size();
    pack.isLoad = loads;
    pack.isStore = !loads;
    pack.elementType = element;
    pack.scalarAccesses = accesses;
    return true;
}

bool knownNonAliasing(const SLPVectorizer::SLPMemoryPack& a,
                      const SLPVectorizer::SLPMemoryPack& b) {
    if (a.base != b.base) {
        auto* ai = dynamic_cast<ir::Instruction*>(a.base);
        auto* bi = dynamic_cast<ir::Instruction*>(b.base);
        auto allocation = [](ir::Instruction* i) {
            return i && (i->getOpcode() == O::Alloc || i->getOpcode() == O::Alloc4 || i->getOpcode() == O::Alloc16);
        };
        return allocation(ai) && allocation(bi) && ai != bi;
    }
    int64_t aEnd = a.firstOffset + a.elementSize * a.lanes;
    int64_t bEnd = b.firstOffset + b.elementSize * b.lanes;
    return aEnd <= b.firstOffset || bEnd <= a.firstOffset;
}

O vectorOpcode(O op) {
    switch (op) {
        case O::Add: return O::VAdd;
        case O::Sub: return O::VSub;
        case O::Mul: return O::VMul;
        case O::FAdd: return O::VFAdd;
        case O::FSub: return O::VFSub;
        case O::FMul: return O::VFMul;
        case O::And: return O::VAnd;
        case O::Or: return O::VOr;
        case O::Xor: return O::VXor;
        case O::SMin: return O::VMin;
        case O::SMax: return O::VMax;
        default: return O::VAdd;
    }
}

unsigned elementBits(ir::Type* type) {
    if (auto* integer = dynamic_cast<ir::IntegerType*>(type)) {
        const unsigned width = integer->getBitwidth();
        return width == 8 || width == 16 || width == 32 || width == 64 ? width : 0;
    }
    if (type && type->isFloatTy()) return 32;
    if (type && type->isDoubleTy()) return 64;
    return 0;
}

bool reaches(ir::Value* value, ir::Instruction* sought, std::set<ir::Value*>& seen) {
    if (value == sought) return true;
    if (!seen.insert(value).second) return false;
    auto* instruction = dynamic_cast<ir::Instruction*>(value);
    if (!instruction) return false;
    for (const auto& operand : instruction->getOperands())
        if (operand->get() && reaches(operand->get(), sought, seen)) return true;
    return false;
}

bool independent(const std::vector<ir::Instruction*>& lanes) {
    for (size_t i = 0; i < lanes.size(); ++i) {
        for (size_t j = 0; j < lanes.size(); ++j) {
            if (i == j) continue;
            std::set<ir::Value*> seen;
            if (reaches(lanes[i], lanes[j], seen)) {
                diag("reject: lane " + std::to_string(i) + " depends on lane " + std::to_string(j));
                return false;
            }
        }
    }
    return true;
}

bool isStraightLine(const ir::BasicBlock& block) {
    for (const auto& instruction : block.getInstructions()) {
        switch (instruction->getOpcode()) {
            case O::Phi: case O::Br: case O::Jmp: case O::Jz: case O::Jnz:
            case O::Call: case O::ExternCall: case O::Syscall:
                return false;
            default: break;
        }
    }
    return true;
}

struct SLPCost {
    int scalarCost = 0;
    int inputMaterializationCost = 0;
    int vectorOpCost = 0;
    int outputMaterializationCost = 0;

    int totalVectorCost() const {
        return inputMaterializationCost + vectorOpCost + outputMaterializationCost;
    }

    bool isProfitable(int margin = 1) const {
        return totalVectorCost() + margin <= scalarCost;
    }
};

SLPCost evaluateArithmeticPackCost(const std::vector<ir::Instruction*>& lanes, unsigned vectorWidthBits) {
    SLPCost cost;
    if (lanes.empty()) return cost;

    cost.scalarCost = static_cast<int>(lanes.size());
    cost.vectorOpCost = 1;

    unsigned numLanes = static_cast<unsigned>(lanes.size());

    // Evaluate input materialization costs
    for (size_t opIdx = 0; opIdx < 2; ++opIdx) {
        bool allSame = true;
        bool allVectorProducers = true;
        ir::Value* firstOp = lanes[0]->getOperands()[opIdx]->get();

        for (size_t i = 0; i < numLanes; ++i) {
            ir::Value* opVal = lanes[i]->getOperands()[opIdx]->get();
            if (opVal != firstOp) allSame = false;
            if (!dynamic_cast<ir::VectorInstruction*>(opVal)) {
                allVectorProducers = false;
            }
        }

        if (allVectorProducers) {
            // Operands are produced by vector instructions (e.g. VLoad, VAdd) -> zero materialization cost
            cost.inputMaterializationCost += 0;
        } else if (allSame) {
            // Single scalar broadcast -> 1 instruction cost
            cost.inputMaterializationCost += 1;
        } else {
            // Disparate scalar inputs -> VInsert sequence (1 broadcast + (numLanes - 1) inserts)
            cost.inputMaterializationCost += static_cast<int>(numLanes);
        }
    }

    // Evaluate output materialization costs
    bool allVectorConsumers = true;
    for (size_t i = 0; i < numLanes; ++i) {
        for (const auto* use : lanes[i]->getUseList()) {
            if (use && !dynamic_cast<ir::VectorInstruction*>(use->getUser())) {
                allVectorConsumers = false;
                break;
            }
        }
        if (!allVectorConsumers) break;
    }

    if (allVectorConsumers) {
        // Output feeds vector consumers (e.g. VStore or subsequent vector ALU) -> zero extraction cost
        cost.outputMaterializationCost = 0;
    } else {
        // Scalar consumers require extraction -> VExtract per lane
        cost.outputMaterializationCost = static_cast<int>(numLanes);
    }

    return cost;
}

} // namespace

bool SLPVectorizer::performTransformation(ir::Function& func) {
    auto* module = func.getParent();
    if (!module) return false;
    auto* context = module->getContext();
    auto target = target::TargetResolver::resolve(target_);
    if (!target) return false;
    bool changed = false;

    for (auto& blockOwner : func.getBasicBlocks()) {
        auto& block = *blockOwner;
        if (!isStraightLine(block)) continue;

        // Stores are the natural roots for useful SLP trees.  Discovery below
        // records the complete load/arithmetic/store plan before changing IR.
        std::vector<ir::Instruction*> storeRoots;
        for (auto& owner : block.getInstructions()) {
            if (isSLPBarrier(*owner)) storeRoots.clear();
            else if (isStore(owner->getOpcode())) storeRoots.push_back(owner.get());
        }
        size_t storeCursor = 0;
        while (storeCursor < storeRoots.size()) {
            auto* firstValue = dynamic_cast<ir::Instruction*>(storeRoots[storeCursor]->getOperands()[0]->get());
            if (!firstValue || !scalarOpcode(firstValue->getOpcode())) { ++storeCursor; continue; }
            ir::Type* scalarType = firstValue->getType();
            unsigned bits = elementBits(scalarType);
            unsigned laneCount = 0;
            for (unsigned width : {512u, 256u, 128u, 64u}) {
                if (!target->supportsVectorWidth(width)) continue;
                unsigned count = bits ? width / bits : 0;
                auto* vt = count ? context->getVectorType(scalarType, count) : nullptr;
                if (count && storeCursor + count <= storeRoots.size() && target->supportsVectorWidth(width) &&
                    target->supportsVectorType(vt) && target->supportsVectorOperation(vectorOpcode(firstValue->getOpcode()), vt)) {
                    laneCount = count;
                    break;
                }
            }
            if (!laneCount) break;

            std::vector<ir::Instruction*> stores(storeRoots.begin() + storeCursor,
                                                 storeRoots.begin() + storeCursor + laneCount);
            std::vector<ir::Instruction*> arithmetic;
            std::vector<ir::Instruction*> leftLoads, rightLoads;
            bool valid = true;
            for (auto* store : stores) {
                auto* op = dynamic_cast<ir::Instruction*>(store->getOperands()[0]->get());
                if (!op || op->getOpcode() != firstValue->getOpcode() || op->getType() != scalarType ||
                    op->getOperands().size() != 2) { valid = false; break; }
                arithmetic.push_back(op);
                auto* left = dynamic_cast<ir::Instruction*>(op->getOperands()[0]->get());
                auto* right = dynamic_cast<ir::Instruction*>(op->getOperands()[1]->get());
                if (!left || !right || !isLoad(left->getOpcode()) || !isLoad(right->getOpcode()) ||
                    left->getType() != scalarType || right->getType() != scalarType) { valid = false; break; }
                leftLoads.push_back(left);
                rightLoads.push_back(right);
            }
            if (!valid || !independent(arithmetic)) { ++storeCursor; continue; }

            SLPMemoryPack storePack, leftPack, rightPack;
            if (!contiguous(stores, false, scalarType, storePack) ||
                !contiguous(leftLoads, true, scalarType, leftPack) ||
                !contiguous(rightLoads, true, scalarType, rightPack)) {
                diag("reject: non-contiguous memory lanes");
                ++storeCursor;
                continue;
            }
            if (!knownNonAliasing(storePack, leftPack) || !knownNonAliasing(storePack, rightPack)) {
                diag("reject: memory dependence across lanes");
                ++storeCursor;
                continue;
            }

            unsigned width = laneCount * bits;
            auto* vectorType = context->getVectorType(scalarType, laneCount);
            auto insertion = std::find_if(block.getInstructions().begin(), block.getInstructions().end(),
                [&](const std::unique_ptr<ir::Instruction>& instruction) {
                    return instruction.get() == stores.front();
                });
            auto emit = [&](std::unique_ptr<ir::Instruction> instruction) {
                auto* raw = instruction.get();
                block.addInstruction(insertion, std::move(instruction));
                return raw;
            };
            ir::Value* leftVector = emit(std::make_unique<ir::VectorInstruction>(vectorType, O::VLoad,
                std::vector<ir::Value*>{leftLoads.front()->getOperands()[0]->get()}, width, &block));
            ir::Value* rightVector = emit(std::make_unique<ir::VectorInstruction>(vectorType, O::VLoad,
                std::vector<ir::Value*>{rightLoads.front()->getOperands()[0]->get()}, width, &block));
            ir::Value* result = emit(std::make_unique<ir::VectorInstruction>(vectorType,
                vectorOpcode(firstValue->getOpcode()), std::vector<ir::Value*>{leftVector, rightVector}, width, &block));
            emit(std::make_unique<ir::VectorInstruction>(context->getVoidType(), O::VStore,
                std::vector<ir::Value*>{result, stores.front()->getOperands()[1]->get()}, width, &block));

            block.removeInstructions(stores);
            std::vector<ir::Instruction*> dead;
            for (auto* op : arithmetic) if (op->use_empty()) dead.push_back(op);
            block.removeInstructions(dead);
            dead.clear();
            for (auto* load : leftLoads) if (load->use_empty()) dead.push_back(load);
            for (auto* load : rightLoads) if (load->use_empty()) dead.push_back(load);
            block.removeInstructions(dead);
            diag("SLP root: " + std::to_string(laneCount) + " contiguous stores; type: " +
                 scalarType->toString() + "; first offset: " + std::to_string(storePack.firstOffset) +
                 "; stride: " + std::to_string(storePack.elementSize) + "; lanes: " +
                 std::to_string(laneCount) + "; source load pack A: contiguous; source load pack B: contiguous; " +
                 "vector width: " + std::to_string(width) + "; vector opcode: " +
                 std::to_string(vectorOpcode(firstValue->getOpcode())) + "; accepted");
            changed = true;
            storeCursor += laneCount;
        }

        std::vector<ir::Instruction*> run;
        auto flush = [&]() {
            size_t cursor = 0;
            while (cursor < run.size()) {
                ir::Type* scalarType = run[cursor]->getType();
                const unsigned bits = elementBits(scalarType);
                unsigned lanes = 0;
                for (unsigned candidate : {512u, 256u, 128u, 64u}) {
                    unsigned count = bits ? candidate / bits : 0;
                    if (count && cursor + count <= run.size()) {
                        auto* type = context->getVectorType(scalarType, count);
                        O op = vectorOpcode(run[cursor]->getOpcode());
                        if (target->supportsVectorWidth(candidate) && target->supportsVectorType(type) &&
                            target->supportsVectorOperation(op, type)) {
                            lanes = count;
                            break;
                        }
                    }
                }
                if (!lanes) break;

                SLPPack plan;
                plan.lanes.assign(run.begin() + cursor, run.begin() + cursor + lanes);
                plan.widthBits = lanes * bits;
                plan.type = context->getVectorType(scalarType, lanes);
                plan.vectorOpcode = vectorOpcode(run[cursor]->getOpcode());
                if (!independent(plan.lanes)) { ++cursor; continue; }

                // Cost Model Evaluation
                SLPCost cost = evaluateArithmeticPackCost(plan.lanes, plan.widthBits);
                if (!cost.isProfitable()) {
                    diag("SLP candidate:\n"
                         "  opcode: " + std::to_string(plan.vectorOpcode) + "\n"
                         "  scalar type: " + scalarType->toString() + "\n"
                         "  lanes: " + std::to_string(lanes) + "\n"
                         "  vector width: " + std::to_string(plan.widthBits) + "\n"
                         "  scalar instructions eliminated: " + std::to_string(cost.scalarCost) + "\n"
                         "  scalar cost: " + std::to_string(cost.scalarCost) + "\n"
                         "  input materialization cost: " + std::to_string(cost.inputMaterializationCost) + "\n"
                         "  vector operation cost: " + std::to_string(cost.vectorOpCost) + "\n"
                         "  output materialization cost: " + std::to_string(cost.outputMaterializationCost) + "\n"
                         "  total vector cost: " + std::to_string(cost.totalVectorCost()) + "\n"
                         "  decision: REJECT\n"
                         "  reason: insert/extract materialization exceeds scalar cost");
                    ++cursor;
                    continue;
                }

                auto insertAt = std::find_if(block.getInstructions().begin(), block.getInstructions().end(),
                    [&](const std::unique_ptr<ir::Instruction>& item) { return item.get() == plan.lanes.front(); });
                auto emit = [&](std::unique_ptr<ir::Instruction> instruction) {
                    auto* result = instruction.get();
                    block.addInstruction(insertAt, std::move(instruction));
                    return result;
                };
                auto* i32 = context->getIntegerType(32);
                ir::Value* lhs = emit(std::make_unique<ir::VectorInstruction>(
                    plan.type, O::VBroadcast, std::vector<ir::Value*>{plan.lanes[0]->getOperands()[0]->get()}, plan.widthBits, &block));
                ir::Value* rhs = emit(std::make_unique<ir::VectorInstruction>(
                    plan.type, O::VBroadcast, std::vector<ir::Value*>{plan.lanes[0]->getOperands()[1]->get()}, plan.widthBits, &block));
                for (unsigned lane = 1; lane < lanes; ++lane) {
                    auto* index = context->getConstantInt(i32, lane);
                    lhs = emit(std::make_unique<ir::VectorInstruction>(plan.type, O::VInsert,
                        std::vector<ir::Value*>{lhs, plan.lanes[lane]->getOperands()[0]->get(), index}, plan.widthBits, &block));
                    rhs = emit(std::make_unique<ir::VectorInstruction>(plan.type, O::VInsert,
                        std::vector<ir::Value*>{rhs, plan.lanes[lane]->getOperands()[1]->get(), index}, plan.widthBits, &block));
                }
                ir::Value* result = emit(std::make_unique<ir::VectorInstruction>(plan.type, plan.vectorOpcode,
                    std::vector<ir::Value*>{lhs, rhs}, plan.widthBits, &block));
                for (unsigned lane = 0; lane < lanes; ++lane) {
                    auto* extract = emit(std::make_unique<ir::VectorInstruction>(scalarType, O::VExtract,
                        std::vector<ir::Value*>{result, context->getConstantInt(i32, lane)}, plan.widthBits, &block));
                    plan.lanes[lane]->replaceAllUsesWith(extract);
                }
                block.removeInstructions(plan.lanes);
                diag("SLP candidate:\n"
                     "  opcode: " + std::to_string(plan.vectorOpcode) + "\n"
                     "  scalar type: " + scalarType->toString() + "\n"
                     "  lanes: " + std::to_string(lanes) + "\n"
                     "  vector width: " + std::to_string(plan.widthBits) + "\n"
                     "  scalar instructions eliminated: " + std::to_string(cost.scalarCost) + "\n"
                     "  scalar cost: " + std::to_string(cost.scalarCost) + "\n"
                     "  input materialization cost: " + std::to_string(cost.inputMaterializationCost) + "\n"
                     "  vector operation cost: " + std::to_string(cost.vectorOpCost) + "\n"
                     "  output materialization cost: " + std::to_string(cost.outputMaterializationCost) + "\n"
                     "  total vector cost: " + std::to_string(cost.totalVectorCost()) + "\n"
                     "  decision: ACCEPT\n"
                     "  reason: vectorization cost cheaper than scalar cost");
                changed = true;
                cursor += lanes;
            }
            run.clear();
        };

        O runOpcode = O::Ret;
        ir::Type* runType = nullptr;
        for (auto& owner : block.getInstructions()) {
            auto* instruction = owner.get();
            if (dynamic_cast<ir::VectorInstruction*>(instruction) || !scalarOpcode(instruction->getOpcode()) ||
                elementBits(instruction->getType()) == 0 || instruction->getOperands().size() != 2) {
                flush();
                runOpcode = O::Ret;
                runType = nullptr;
                continue;
            }
            if (!run.empty() && (instruction->getOpcode() != runOpcode || instruction->getType() != runType)) flush();
            if (run.empty()) { runOpcode = instruction->getOpcode(); runType = instruction->getType(); }
            run.push_back(instruction);
        }
        flush();
    }
    return changed;
}

} // namespace transforms
