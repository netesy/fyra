#include "transforms/InstructionScheduler.h"
#include "ir/SIMDInstruction.h"
#include "ir/Use.h"
#include <map>
#include <set>
#include <algorithm>
#include <iostream>

namespace transforms {

unsigned InstructionScheduler::getInstructionLatency(ir::Instruction::Opcode op) {
    switch (op) {
        case ir::Instruction::Opcode::Load: return 4;
        case ir::Instruction::Opcode::FMul:
        case ir::Instruction::Opcode::FDiv: return 4;
        case ir::Instruction::Opcode::Mul:
        case ir::Instruction::Opcode::Div:
        case ir::Instruction::Opcode::Rem: return 3;
        case ir::Instruction::Opcode::Call: return 5;
        default: return 1;
    }
}

struct SchedNode {
    ir::Instruction* instr{nullptr};
    unsigned latency{1};
    unsigned height{0};
    unsigned unscheduledPreds{0};
    std::vector<SchedNode*> succs;
    std::vector<SchedNode*> preds;
};

bool InstructionScheduler::run(ir::Function& func) {
    bool changed = false;
    for (auto& bb : func.getBasicBlocks()) {
        if (bb) {
            changed |= scheduleBasicBlock(*bb);
        }
    }
    return changed;
}

bool InstructionScheduler::scheduleBasicBlock(ir::BasicBlock& bb) {
    auto& instrs = bb.getInstructions();
    if (instrs.size() <= 2) return false; // Nothing to schedule

    // Separate non-terminators from terminator (Branch, Jump, Ret)
    std::vector<std::unique_ptr<ir::Instruction>> nonTerminators;
    std::unique_ptr<ir::Instruction> terminator;

    while (!instrs.empty()) {
        auto inst = std::move(instrs.front());
        instrs.pop_front();

        auto op = inst->getOpcode();
        if (op == ir::Instruction::Opcode::Br ||
            op == ir::Instruction::Opcode::Jmp ||
            op == ir::Instruction::Opcode::Ret) {
            terminator = std::move(inst);
        } else {
            nonTerminators.push_back(std::move(inst));
        }
    }

    if (nonTerminators.empty()) {
        if (terminator) instrs.push_back(std::move(terminator));
        return false;
    }

    // Build DAG nodes
    std::vector<std::unique_ptr<SchedNode>> nodes;
    std::map<ir::Instruction*, SchedNode*> nodeMap;

    for (auto& inst : nonTerminators) {
        auto node = std::make_unique<SchedNode>();
        node->instr = inst.get();
        node->latency = getInstructionLatency(inst->getOpcode());
        if (dynamic_cast<ir::VectorInstruction*>(inst.get())) {
            node->latency = 3; // Vector instruction latency
        }
        nodeMap[inst.get()] = node.get();
        nodes.push_back(std::move(node));
    }

    // Build Def-Use & Memory dependency edges
    SchedNode* lastMemoryOp = nullptr;

    for (size_t i = 0; i < nodes.size(); ++i) {
        SchedNode* curr = nodes[i].get();
        ir::Instruction* inst = curr->instr;

        // Data Def-Use edges
        for (const auto& opUse : inst->getOperands()) {
            if (!opUse) continue;
            if (auto* opInst = dynamic_cast<ir::Instruction*>(opUse->get())) {
                if (nodeMap.count(opInst) > 0) {
                    SchedNode* pred = nodeMap[opInst];
                    pred->succs.push_back(curr);
                    curr->preds.push_back(pred);
                    curr->unscheduledPreds++;
                }
            }
        }

        // Memory barriers / order
        bool isMem = (inst->getOpcode() == ir::Instruction::Opcode::Load ||
                      inst->getOpcode() == ir::Instruction::Opcode::Store ||
                      inst->getOpcode() == ir::Instruction::Opcode::Call);
        if (isMem && lastMemoryOp) {
            // Memory edge to maintain side-effect safety
            bool alreadyDep = false;
            for (auto* p : curr->preds) {
                if (p == lastMemoryOp) { alreadyDep = true; break; }
            }
            if (!alreadyDep) {
                lastMemoryOp->succs.push_back(curr);
                curr->preds.push_back(lastMemoryOp);
                curr->unscheduledPreds++;
            }
        }
        if (isMem) {
            lastMemoryOp = curr;
        }
    }

    // Compute Heights (critical path length from bottom up)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        SchedNode* n = nodes[i].get();
        unsigned maxSuccHeight = 0;
        for (auto* s : n->succs) {
            maxSuccHeight = std::max(maxSuccHeight, s->height);
        }
        n->height = n->latency + maxSuccHeight;
    }

    // Ready List (priority queue based on height)
    std::vector<SchedNode*> readyList;
    for (auto& n : nodes) {
        if (n->unscheduledPreds == 0) {
            readyList.push_back(n.get());
        }
    }

    std::map<ir::Instruction*, std::unique_ptr<ir::Instruction>> instOwner;
    for (auto& inst : nonTerminators) {
        instOwner[inst.get()] = std::move(inst);
    }

    // List Scheduling Loop
    while (!readyList.empty()) {
        // Pick node with maximum height
        auto bestIt = std::max_element(readyList.begin(), readyList.end(),
            [](SchedNode* a, SchedNode* b) {
                if (a->height != b->height) return a->height < b->height;
                return a->latency < b->latency;
            });

        SchedNode* chosen = *bestIt;
        readyList.erase(bestIt);

        // Append to basic block
        instrs.push_back(std::move(instOwner[chosen->instr]));

        // Decrement successor unscheduledPreds
        for (auto* s : chosen->succs) {
            s->unscheduledPreds--;
            if (s->unscheduledPreds == 0) {
                readyList.push_back(s);
            }
        }
    }

    if (terminator) {
        instrs.push_back(std::move(terminator));
    }

    return true;
}

} // namespace transforms
