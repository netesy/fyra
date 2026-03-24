#include "transforms/SCCP.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Module.h"
#include "ir/PhiNode.h"
#include <iostream>
#include <set>
#include <unordered_set>
#include <map>
#include <vector>

namespace transforms {

bool SCCP::performTransformation(ir::Function& func) {
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
                // First time visiting this block, visit all instructions
                for (auto& instr : bb->getInstructions()) {
                    this->visit(instr.get(), executableEdges, executableBlocks, inInstructionWorklist);
                }
            } else {
                // Already visited, but new edges might have been added.
                // In edge-based SCCP, we only visit PHI nodes when a new edge arrives.
                for (auto& instr : bb->getInstructions()) {
                    if (instr->getOpcode() == ir::Instruction::Phi) {
                         this->visit(instr.get(), executableEdges, executableBlocks, inInstructionWorklist);
                    } else break; // PHIs are at the start
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
            
            // Don't replace terminators or PHIs yet
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
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) return {Constant, ci};
    if (auto* cf = dynamic_cast<ir::ConstantFP*>(val)) return {Constant, cf};
    if (lattice.count(val)) return lattice[val];
    return {Bottom, nullptr};
}

void SCCP::setLatticeValue(ir::Instruction* instr, LatticeEntry new_val, std::unordered_set<ir::Instruction*>& inInstructionWorklist) {
    if (!instr || !instr->getType() || instr->getType()->isVoidTy()) return;
    
    LatticeEntry& old_val = lattice[instr];
    
    // Monotonicity: Top -> Constant -> Bottom
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
        bool has_executable = false;
        
        for (size_t i = 0; i < phi->getOperands().size(); i += 2) {
            ir::BasicBlock* pred = static_cast<ir::BasicBlock*>(phi->getOperands()[i]->get());
            if (executableEdges.count({pred, phi->getParent()})) {
                has_executable = true;
                LatticeEntry val = getLatticeValue(phi->getOperands()[i+1]->get());
                if (val.type == Bottom) { result = {Bottom, nullptr}; break; }
                if (val.type == Constant) {
                    if (result.type == Top) result = val;
                    else if (result.constant != val.constant) { result = {Bottom, nullptr}; break; }
                }
            }
        }
        if (has_executable) setLatticeValue(phi, result, inInstructionWorklist);
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

    // Standard binary/unary ops
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
        // Fold instruction
        if (op == ir::Instruction::Add) {
            int64_t v = static_cast<ir::ConstantInt*>(op_vals[0].constant)->getValue() + static_cast<ir::ConstantInt*>(op_vals[1].constant)->getValue();
            setLatticeValue(instr, {Constant, ir::ConstantInt::get(static_cast<ir::IntegerType*>(instr->getType()), v)}, inInstructionWorklist);
        } else if (op == ir::Instruction::Sub) {
            int64_t v = static_cast<ir::ConstantInt*>(op_vals[0].constant)->getValue() - static_cast<ir::ConstantInt*>(op_vals[1].constant)->getValue();
            setLatticeValue(instr, {Constant, ir::ConstantInt::get(static_cast<ir::IntegerType*>(instr->getType()), v)}, inInstructionWorklist);
        } else if (op == ir::Instruction::Mul) {
            int64_t v = static_cast<ir::ConstantInt*>(op_vals[0].constant)->getValue() * static_cast<ir::ConstantInt*>(op_vals[1].constant)->getValue();
            setLatticeValue(instr, {Constant, ir::ConstantInt::get(static_cast<ir::IntegerType*>(instr->getType()), v)}, inInstructionWorklist);
        } else if (op == ir::Instruction::Copy) {
            setLatticeValue(instr, op_vals[0], inInstructionWorklist);
        } else {
            setLatticeValue(instr, {Bottom, nullptr}, inInstructionWorklist);
        }
    }
}

bool SCCP::validatePreconditions(ir::Function& f) { return !f.getBasicBlocks().empty(); }

} // namespace transforms
