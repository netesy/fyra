#include "transforms/GVN.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Value.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace transforms {

static std::string getOperandId(const ir::Value* val) {
    if (!val) return "null";
    if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
        return "C:" + std::to_string(ci->getValue());
    }
    if (auto* cfp = dynamic_cast<const ir::ConstantFP*>(val)) {
        return "FP:" + std::to_string(cfp->getValue());
    }
    return "V:" + std::to_string(reinterpret_cast<uintptr_t>(val));
}

static std::string getValueKey(const ir::Instruction* instr) {
    if (!instr) return "";

    std::stringstream key;
    ir::Instruction::Opcode op = instr->getOpcode();
    key << op;

    switch (op) {
        case ir::Instruction::Add:
        case ir::Instruction::Mul:
        case ir::Instruction::And:
        case ir::Instruction::Or:
        case ir::Instruction::Xor:
        case ir::Instruction::FAdd:
        case ir::Instruction::FMul:
        case ir::Instruction::Ceq:
        case ir::Instruction::Cne:
        case ir::Instruction::Ceqf:
        case ir::Instruction::Cnef: {
            std::vector<std::string> opIds;
            for (const auto& use : instr->getOperands()) {
                if (use) opIds.push_back(getOperandId(use->get()));
            }
            std::sort(opIds.begin(), opIds.end());
            for (const auto& id : opIds) {
                key << ":" << id;
            }
            break;
        }
        case ir::Instruction::Sub:
        case ir::Instruction::Div:
        case ir::Instruction::Udiv:
        case ir::Instruction::Rem:
        case ir::Instruction::Urem:
        case ir::Instruction::Shl:
        case ir::Instruction::Shr:
        case ir::Instruction::Sar:
        case ir::Instruction::FSub:
        case ir::Instruction::FDiv:
        case ir::Instruction::Cslt:
        case ir::Instruction::Csle:
        case ir::Instruction::Csgt:
        case ir::Instruction::Csge:
        case ir::Instruction::Cult:
        case ir::Instruction::Cule:
        case ir::Instruction::Cugt:
        case ir::Instruction::Cuge:
        case ir::Instruction::Clt:
        case ir::Instruction::Cle:
        case ir::Instruction::Cgt:
        case ir::Instruction::Cge:
        case ir::Instruction::ExtUB:
        case ir::Instruction::ExtUH:
        case ir::Instruction::ExtUW:
        case ir::Instruction::ExtSB:
        case ir::Instruction::ExtSH:
        case ir::Instruction::ExtSW:
        case ir::Instruction::ExtS:
        case ir::Instruction::TruncD:
        case ir::Instruction::SWtoF:
        case ir::Instruction::UWtoF:
        case ir::Instruction::DToSI:
        case ir::Instruction::DToUI:
        case ir::Instruction::SToSI:
        case ir::Instruction::SToUI:
        case ir::Instruction::Sltof:
        case ir::Instruction::Ultof:
        case ir::Instruction::Cast: {
            for (const auto& use : instr->getOperands()) {
                if (use) key << ":" << getOperandId(use->get());
            }
            break;
        }
        default:
            return "";
    }

    return key.str();
}

static int64_t signExtendValue(uint64_t val, unsigned bitwidth) {
    if (bitwidth >= 64 || bitwidth == 0) return static_cast<int64_t>(val);
    uint64_t mask = 1ULL << (bitwidth - 1);
    return static_cast<int64_t>((val ^ mask) - mask);
}

static ir::ConstantInt* tryConstantFold(ir::Instruction* instr, ir::Function* func) {
    if (!instr || instr->getOperands().size() < 2) return nullptr;
    if (!instr->getOperands()[0] || !instr->getOperands()[1]) return nullptr;

    auto* c1 = dynamic_cast<ir::ConstantInt*>(instr->getOperands()[0]->get());
    auto* c2 = dynamic_cast<ir::ConstantInt*>(instr->getOperands()[1]->get());
    if (!c1 || !c2) return nullptr;

    auto ctx = func->getParent() ? func->getParent()->getContextShared() : std::make_shared<ir::IRContext>();
    ir::IntegerType* intTy = dynamic_cast<ir::IntegerType*>(instr->getType());
    if (!intTy) intTy = ctx->getIntegerType(32);

    unsigned bitwidth = intTy->getBitwidth();
    uint64_t v1 = c1->getValue();
    uint64_t v2 = c2->getValue();
    int64_t s1 = signExtendValue(v1, bitwidth);
    int64_t s2 = signExtendValue(v2, bitwidth);
    uint64_t res = 0;

    switch (instr->getOpcode()) {
        case ir::Instruction::Add: res = v1 + v2; break;
        case ir::Instruction::Sub: res = v1 - v2; break;
        case ir::Instruction::Mul: res = v1 * v2; break;
        case ir::Instruction::Div: case ir::Instruction::Udiv: if (v2 == 0) return nullptr; res = v1 / v2; break;
        case ir::Instruction::Rem: case ir::Instruction::Urem: if (v2 == 0) return nullptr; res = v1 % v2; break;
        case ir::Instruction::And: res = v1 & v2; break;
        case ir::Instruction::Or:  res = v1 | v2; break;
        case ir::Instruction::Xor: res = v1 ^ v2; break;
        case ir::Instruction::Shl: res = v1 << v2; break;
        case ir::Instruction::Shr: res = v1 >> v2; break;
        case ir::Instruction::Sar: res = static_cast<uint64_t>(s1 >> s2); break;
        case ir::Instruction::Ceq: res = (v1 == v2) ? 1 : 0; break;
        case ir::Instruction::Cne: res = (v1 != v2) ? 1 : 0; break;
        case ir::Instruction::Cslt: res = (s1 < s2) ? 1 : 0; break;
        case ir::Instruction::Csle: res = (s1 <= s2) ? 1 : 0; break;
        case ir::Instruction::Csgt: res = (s1 > s2) ? 1 : 0; break;
        case ir::Instruction::Csge: res = (s1 >= s2) ? 1 : 0; break;
        case ir::Instruction::Cult: res = (v1 < v2) ? 1 : 0; break;
        case ir::Instruction::Cule: res = (v1 <= v2) ? 1 : 0; break;
        case ir::Instruction::Cugt: res = (v1 > v2) ? 1 : 0; break;
        case ir::Instruction::Cuge: res = (v1 >= v2) ? 1 : 0; break;
        default: return nullptr;
    }

    return ctx->getConstantInt(intTy, res);
}

static void processBlockGlobalGVN(ir::BasicBlock* bb, const DominatorTree& domTree, std::map<std::string, ir::Instruction*>& exprTable, bool& changed) {
    if (!bb) return;

    std::vector<ir::Instruction*> to_remove;

    for (auto& instrPtr : bb->getInstructions()) {
        ir::Instruction* instr = instrPtr.get();
        if (!instr) continue;

        // Fold Copy
        if (instr->getOpcode() == ir::Instruction::Copy && !instr->getOperands().empty() && instr->getOperands()[0]) {
            ir::Value* src = instr->getOperands()[0]->get();
            if (src && src != instr) {
                instr->replaceAllUsesWith(src);
                to_remove.push_back(instr);
                changed = true;
                continue;
            }
        }

        // Fold Constant expressions
        if (ir::ConstantInt* folded = tryConstantFold(instr, bb->getParent())) {
            instr->replaceAllUsesWith(folded);
            to_remove.push_back(instr);
            changed = true;
            continue;
        }

        std::string key = getValueKey(instr);
        if (key.empty()) continue;

        if (exprTable.count(key)) {
            ir::Instruction* original = exprTable[key];
            if (original && original != instr && domTree.dominates(original->getParent(), bb)) {
                instr->replaceAllUsesWith(original);
                to_remove.push_back(instr);
                changed = true;
                continue;
            }
        }

        exprTable[key] = instr;
    }

    if (!to_remove.empty()) {
            for (auto* r : to_remove) {
                std::cout << "[GVN REMOVE] removing op=" << r->getOpcode() << " in " << bb->getName() << std::endl;
            }
        bb->removeInstructions(to_remove);
    }

    for (ir::BasicBlock* child : domTree.getChildren(bb)) {
        std::map<std::string, ir::Instruction*> childScope = exprTable;
        processBlockGlobalGVN(child, domTree, childScope, changed);
    }
}

bool GVN::run(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    CFGBuilder::run(func);
    DominatorTree domTree;
    domTree.run(func);

    bool overallChanged = false;
    bool passChanged = true;
    int maxPasses = 5;

    while (passChanged && maxPasses-- > 0) {
        passChanged = false;
        std::map<std::string, ir::Instruction*> exprTable;
        ir::BasicBlock* entry = func.getBasicBlocks().front().get();
        processBlockGlobalGVN(entry, domTree, exprTable, passChanged);
        if (passChanged) overallChanged = true;
    }

    return overallChanged;
}

} // namespace transforms
