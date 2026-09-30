#include "transforms/InstructionScheduler.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
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

    // Separate phi nodes, non-terminators, and terminators (Branch, Jump, Ret)
    std::vector<std::unique_ptr<ir::Instruction>> phiNodes;
    std::vector<std::unique_ptr<ir::Instruction>> nonTerminators;
    std::unique_ptr<ir::Instruction> terminator;

    while (!instrs.empty()) {
        auto inst = std::move(instrs.front());
        instrs.pop_front();

        if (dynamic_cast<ir::PhiNode*>(inst.get())) {
            phiNodes.push_back(std::move(inst));
            continue;
        }

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
        for (auto& phi : phiNodes) instrs.push_back(std::move(phi));
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
    std::vector<SchedNode*> memoryOps;

    for (size_t i = 0; i < nodes.size(); ++i) {
        SchedNode* curr = nodes[i].get();
        ir::Instruction* inst = curr->instr;

        // Data Def-Use edges
        std::set<SchedNode*> visitedPreds;
        for (const auto& opUse : inst->getOperands()) {
            if (!opUse) continue;
            if (auto* opInst = dynamic_cast<ir::Instruction*>(opUse->get())) {
                if (nodeMap.count(opInst) > 0) {
                    SchedNode* pred = nodeMap[opInst];
                    if (visitedPreds.insert(pred).second) {
                        pred->succs.push_back(curr);
                        curr->preds.push_back(pred);
                        curr->unscheduledPreds++;
                    }
                }
            }
        }

        // Memory barriers / order
        auto opc = inst->getOpcode();
        bool isMem = (opc == ir::Instruction::Opcode::Load ||
                      opc == ir::Instruction::Opcode::Loadub ||
                      opc == ir::Instruction::Opcode::Loadsb ||
                      opc == ir::Instruction::Opcode::Loaduh ||
                      opc == ir::Instruction::Opcode::Loadsh ||
                      opc == ir::Instruction::Opcode::Loaduw ||
                      opc == ir::Instruction::Opcode::Loadl ||
                      opc == ir::Instruction::Opcode::Loads ||
                      opc == ir::Instruction::Opcode::Loadd ||
                      opc == ir::Instruction::Opcode::Store ||
                      opc == ir::Instruction::Opcode::Storeb ||
                      opc == ir::Instruction::Opcode::Storeh ||
                      opc == ir::Instruction::Opcode::Storel ||
                      opc == ir::Instruction::Opcode::Stores ||
                      opc == ir::Instruction::Opcode::Stored ||
                      opc == ir::Instruction::Opcode::Call ||
                      opc == ir::Instruction::Opcode::Syscall ||
                      opc == ir::Instruction::Opcode::ExternCall ||
                      opc == ir::Instruction::Opcode::Alloc ||
                      opc == ir::Instruction::Opcode::Alloc4 ||
                      opc == ir::Instruction::Opcode::Alloc16 ||
                      opc == ir::Instruction::Opcode::VLoad ||
                      opc == ir::Instruction::Opcode::VStore ||
                      opc == ir::Instruction::Opcode::VGather ||
                      opc == ir::Instruction::Opcode::VScatter);
        if (isMem) {
            for (auto* prevMem : memoryOps) {
                bool alreadyDep = false;
                for (auto* p : curr->preds) {
                    if (p == prevMem) { alreadyDep = true; break; }
                }
                if (!alreadyDep) {
                    prevMem->succs.push_back(curr);
                    curr->preds.push_back(prevMem);
                    curr->unscheduledPreds++;
                }
            }
            memoryOps.push_back(curr);
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

    // 1. First append phi nodes (Phi nodes must remain at the top of basic block in SSA form)
    for (auto& phi : phiNodes) {
        instrs.push_back(std::move(phi));
    }

    // 2. List Scheduling Loop for general instructions
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
        if (instOwner[chosen->instr]) {
            instrs.push_back(std::move(instOwner[chosen->instr]));
        }

        // Decrement successor unscheduledPreds
        for (auto* s : chosen->succs) {
            s->unscheduledPreds--;
            if (s->unscheduledPreds == 0) {
                readyList.push_back(s);
            }
        }
    }

    // Fallback: append any remaining unscheduled instructions to prevent instruction loss
    for (auto& inst : nonTerminators) {
        if (instOwner[inst.get()]) {
            instrs.push_back(std::move(instOwner[inst.get()]));
        }
    }

    if (terminator) {
        instrs.push_back(std::move(terminator));
    }

    return true;
}

} // namespace transforms
