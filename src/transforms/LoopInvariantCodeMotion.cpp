#include "transforms/LoopInvariantCodeMotion.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include <map>
#include <set>
namespace transforms {
void LoopInvariantCodeMotion::findLoops(ir::Function& func, std::vector<std::unique_ptr<Loop>>& loops) {}
void LoopInvariantCodeMotion::findBackEdges(ir::Function& func, std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& backEdges) {}
void LoopInvariantCodeMotion::buildLoop(ir::BasicBlock* header, ir::BasicBlock* latch, std::unique_ptr<Loop>& loop) {}
void LoopInvariantCodeMotion::buildDominatorTree(ir::Function& func) {}
bool LoopInvariantCodeMotion::dominates(ir::BasicBlock* dominator, ir::BasicBlock* block) { return true; }
bool LoopInvariantCodeMotion::isLoopInvariant(ir::Instruction* instr, const Loop& loop) { return false; }
bool LoopInvariantCodeMotion::hasSideEffects(ir::Instruction* instr) { return false; }
bool LoopInvariantCodeMotion::mayThrow(ir::Instruction* instr) { return false; }
bool LoopInvariantCodeMotion::isMovableTo(ir::Instruction* instr, ir::BasicBlock* target, const Loop& loop) { return false; }
bool LoopInvariantCodeMotion::mayWriteMemory(ir::Instruction* instr) { return false; }
bool LoopInvariantCodeMotion::mayReadMemory(ir::Instruction* instr) { return false; }
bool LoopInvariantCodeMotion::hasMemoryDependencies(ir::Instruction* instr, const Loop& loop) { return false; }
ir::BasicBlock* LoopInvariantCodeMotion::getOrCreatePreheader(Loop& loop, ir::Function& func) { return nullptr; }
bool LoopInvariantCodeMotion::needsPreheader(const Loop& loop) { return true; }
void LoopInvariantCodeMotion::moveInstruction(ir::Instruction* instr, ir::BasicBlock* target) {}
bool LoopInvariantCodeMotion::canHoistInstruction(ir::Instruction* instr, const Loop& loop) { return false; }
void LoopInvariantCodeMotion::hoistInvariantInstructions(Loop& loop) {}
bool LoopInvariantCodeMotion::dominatesAllExits(ir::Instruction* instr, const Loop& loop) { return true; }
bool LoopInvariantCodeMotion::isExecutedOnAllPaths(ir::Instruction* instr, const Loop& loop) { return true; }
bool LoopInvariantCodeMotion::isTerminator(ir::Instruction* instr) { return false; }
bool LoopInvariantCodeMotion::performTransformation(ir::Function& func) { return true; }
bool LoopInvariantCodeMotion::validatePreconditions(ir::Function& func) { return true; }
}
