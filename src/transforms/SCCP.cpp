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
#include <unordered_map>
#include <vector>

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

bool SCCP::isFunctionPure(ir::Function* func, std::unordered_map<ir::Function*, bool>& purityCache, std::unordered_set<ir::Function*>& activeVisiting) {
    if (!func || func->getBasicBlocks().empty()) return false;

    auto it = purityCache.find(func);
    if (it != purityCache.end()) return it->second;

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
                if (!callee || !isFunctionPure(callee, purityCache, activeVisiting)) {
                    pure = false;
                    break;
                }
            }
        }
    }

    activeVisiting.erase(func);
    purityCache[func] = pure;
    return pure;
}

ir::Constant* SCCP::evaluatePureFunctionCall(
    ir::Function* callee,
    const std::vector<ir::Constant*>& argConstants,
    int depth,
    int& stepCount,
    std::unordered_map<ir::Function*, bool>& purityCache
) {
    if (!callee || callee->getBasicBlocks().empty()) return nullptr;
    if (depth > 64) return nullptr;

    std::unordered_set<ir::Function*> activeVisiting;
    if (!isFunctionPure(callee, purityCache, activeVisiting)) return nullptr;

    std::unordered_map<ir::Value*, ir::Constant*> frame;
    const auto& params = callee->getParameters();
    size_t pIdx = 0;
    for (auto pIt = params.begin(); pIt != params.end() && pIdx < argConstants.size(); ++pIt, ++pIdx) {
        frame[pIt->get()] = argConstants[pIdx];
    }

    ir::BasicBlock* currentBB = callee->getBasicBlocks().front().get();
    ir::BasicBlock* prevBB = nullptr;

    while (currentBB) {
        ir::BasicBlock* nextBB = nullptr;
        bool blockTerminated = false;

        for (auto& instPtr : currentBB->getInstructions()) {
            if (++stepCount > 100000) return nullptr;

            ir::Instruction* instr = instPtr.get();
            if (!instr) continue;

            ir::Instruction::Opcode op = instr->getOpcode();

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

                if (!constVal) return nullptr;
                frame[instr] = constVal;
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
                if (auto* c = dynamic_cast<ir::Constant*>(retVal)) return c;
                if (frame.count(retVal)) return frame[retVal];
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
                    if (!c) return nullptr;
                    subArgs.push_back(c);
                }

                ir::Constant* res = evaluatePureFunctionCall(subCallee, subArgs, depth + 1, stepCount, purityCache);
                if (!res) return nullptr;
                frame[instr] = res;
                continue;
            }

            std::vector<ir::ConstantInt*> opCIs;
            for (auto& opUse : instr->getOperands()) {
                ir::Value* v = opUse ? opUse->get() : nullptr;
                ir::ConstantInt* ci = nullptr;
                if (auto* c = dynamic_cast<ir::ConstantInt*>(v)) ci = c;
                else if (frame.count(v)) ci = dynamic_cast<ir::ConstantInt*>(frame[v]);
                opCIs.push_back(ci);
            }

            uint64_t width = getBitWidth(instr->getType());
            uint64_t u1 = (!opCIs.empty() && opCIs[0]) ? opCIs[0]->getValue() : 0;
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

            ir::IntegerType* ity = dynamic_cast<ir::IntegerType*>(instr->getType());
            if (!ity) ity = ir::IntegerType::get(width);
            frame[instr] = ir::ConstantInt::get(ity, resU);
        }

        if (!blockTerminated) return nullptr;
        prevBB = currentBB;
        currentBB = nextBB;
    }

    return nullptr;
}

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
                    int stepCount = 0;
                    std::unordered_map<ir::Function*, bool> purityCache;
                    ir::Constant* eval_res = evaluatePureFunctionCall(callee, arg_consts, 0, stepCount, purityCache);
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
