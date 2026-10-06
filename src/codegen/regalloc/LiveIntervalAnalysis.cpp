#include "codegen/regalloc/LiveIntervalAnalysis.h"
#include "codegen/regalloc/LivenessAnalysis.h"
#include "transforms/DominatorTree.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace transforms {

void LiveIntervalAnalysis::run(ir::Function& func) {
    // 1. Run the underlying liveness analysis
    LivenessAnalysis liveness;
    liveness.run(func);
    const auto& liveRanges = liveness.getLiveRanges();

    // 2. Clear out any old data
    intervals.clear();

    // Keep the actual call instructions.  A bounding [first definition, last
    // use] range may contain a call in a CFG-linearized hole; that does not
    // make the value live across the call.
    std::vector<const ir::Instruction*> callSites;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            auto opc = instr->getOpcode();
            if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                callSites.push_back(instr.get());
            }
        }
    }

    // Calculate loop depth for each basic block
    std::map<const ir::BasicBlock*, int> loopDepthMap;
    for (auto& bb : func.getBasicBlocks()) {
        loopDepthMap[bb.get()] = 0;
    }

    DominatorTree domTree;
    domTree.run(func);

    for (auto& bb : func.getBasicBlocks()) {
        for (const auto* successor : bb->getSuccessors()) {
            if (!successor) continue;
            if (domTree.dominates(const_cast<ir::BasicBlock*>(successor), const_cast<ir::BasicBlock*>(bb.get()))) {
                std::set<const ir::BasicBlock*> loopBlocks;
                std::vector<const ir::BasicBlock*> worklist;
                loopBlocks.insert(successor);
                if (bb.get() != successor) {
                    loopBlocks.insert(bb.get());
                    worklist.push_back(bb.get());
                }
                while (!worklist.empty()) {
                    const ir::BasicBlock* curr = worklist.back();
                    worklist.pop_back();
                    for (const auto* pred : curr->getPredecessors()) {
                        if (pred && loopBlocks.find(pred) == loopBlocks.end()) {
                            loopBlocks.insert(pred);
                            worklist.push_back(pred);
                        }
                    }
                }
                for (const auto* lBB : loopBlocks) {
                    loopDepthMap[lBB]++;
                }
            }
        }
    }

    // 3. Create LiveInterval objects with loop-depth sensitive spill weights
    for (const auto& pair : liveRanges) {
        const ir::Instruction* vreg = pair.first;
        const LiveRange& range = pair.second;
        bool crossesCall = false;
        for (const ir::Instruction* call : callSites) {
            if (liveness.isLiveBefore(call, vreg) && liveness.isLiveAfter(call, vreg)) {
                crossesCall = true;
                break;
            }
        }

        double useWeight = 0.0;
        const ir::BasicBlock* defBB = vreg->getParent();
        if (defBB && loopDepthMap.count(defBB)) {
            useWeight += std::pow(10.0, loopDepthMap[defBB]);
        }
        for (auto& bb : func.getBasicBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                for (auto& op : inst->getOperands()) {
                    if (op && op->get() == vreg) {
                        int depth = loopDepthMap.count(bb.get()) ? loopDepthMap[bb.get()] : 0;
                        useWeight += std::pow(10.0, depth);
                    }
                }
            }
        }
        double len = std::max(1.0, static_cast<double>(range.end - range.start + 1));
        double spillWeight = useWeight / len;

        intervals.emplace_back(const_cast<ir::Instruction*>(vreg), range.start, range.end, crossesCall, spillWeight);
    }

    // 4. Sort the intervals by their starting point
    std::sort(intervals.begin(), intervals.end());
}

} // namespace transforms
