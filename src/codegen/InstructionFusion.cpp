#include "codegen/InstructionFusion.h"
#include "codegen/CodeGen.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include <algorithm>
#include <iostream>

namespace codegen {
namespace target {

// InstructionFusion implementation
InstructionFusion::InstructionFusion() {
    registerDefaultPatterns();
}

void InstructionFusion::registerPattern(const FusionPattern& pattern) {
    patterns.push_back(pattern);
}

void InstructionFusion::registerDefaultPatterns() {
    // Register FMA pattern
    FusionPattern fmaPattern;
    fmaPattern.name = "FusedMultiplyAdd";
    fmaPattern.opcodes = {ir::Instruction::Mul, ir::Instruction::Add};
    fmaPattern.matcher = isMultiplyAddPattern;
    fmaPattern.emitter = emitFusedMultiplyAdd;
    fmaPattern.benefit = 1.5; // 50% improvement estimate
    registerPattern(fmaPattern);
    
    // Register Load-Operate pattern
    FusionPattern loadOpPattern;
    loadOpPattern.name = "LoadAndOperate";
    loadOpPattern.opcodes = {ir::Instruction::Load, ir::Instruction::Add};
    loadOpPattern.matcher = isLoadOperatePattern;
    loadOpPattern.emitter = emitLoadAndOperate;
    loadOpPattern.benefit = 1.2; // 20% improvement estimate
    registerPattern(loadOpPattern);
    
    // Register Compare-and-Branch pattern
    FusionPattern cmpBrPattern;
    cmpBrPattern.name = "CompareAndBranch";
    cmpBrPattern.opcodes = {ir::Instruction::Ceq, ir::Instruction::Jnz};
    cmpBrPattern.matcher = isCompareAndBranchPattern;
    cmpBrPattern.emitter = emitCompareAndBranch;
    cmpBrPattern.benefit = 1.3; // 30% improvement estimate
    registerPattern(cmpBrPattern);
}

std::vector<InstructionFusion::FusionCandidate> InstructionFusion::findFusionOpportunities(ir::BasicBlock& block) {
    std::vector<FusionCandidate> candidates;
    
    auto& instructions = block.getInstructions();
    
    // Search for fusion patterns
    for (auto it = instructions.begin(); it != instructions.end(); ++it) {
        for (const auto& pattern : patterns) {
            // Try to match pattern starting at instruction it
            std::vector<ir::Instruction*> sequence;
            bool matched = true;
            auto current_it = it;
            
            for (size_t j = 0; j < pattern.opcodes.size(); ++j) {
                if (current_it == instructions.end()) {
                    matched = false;
                    break;
                }
                auto* inst = current_it->get();
                if (inst->getOpcode() == pattern.opcodes[j]) {
                    sequence.push_back(inst);
                } else {
                    matched = false;
                    break;
                }
                ++current_it;
            }
            
            if (matched && pattern.matcher(sequence)) {
                FusionCandidate candidate;
                candidate.instructions = sequence;
                candidate.pattern = const_cast<FusionPattern*>(&pattern);
                candidate.estimatedBenefit = estimateFusionBenefit(sequence, pattern);
                candidates.push_back(candidate);
            }
        }
    }
    
    // Sort by benefit (highest first)
    std::sort(candidates.begin(), candidates.end());
    
    return candidates;
}

bool InstructionFusion::canFuseInstructions(const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() < 2) return false;
    
    // Check data dependencies
    for (size_t i = 1; i < instructions.size(); ++i) {
        auto* prevInst = instructions[i-1];
        auto* currInst = instructions[i];
        
        // Check if current instruction uses result of previous
        bool hasDataDependency = false;
        for (auto& operand : currInst->getOperands()) {
            if (operand->get() == prevInst) {
                hasDataDependency = true;
                break;
            }
        }
        
        if (!hasDataDependency) {
            return false; // Instructions must be data-dependent for fusion
        }
    }
    
    return true;
}

double InstructionFusion::estimateFusionBenefit(const std::vector<ir::Instruction*>& instructions, 
                                               const FusionPattern& pattern) {
    // Base benefit from pattern
    double benefit = pattern.benefit;
    
    // Adjust based on instruction types and operand complexity
    for (auto* inst : instructions) {
        if (inst->getType()->isFloatingPoint()) {
            benefit *= 1.1; // FP operations benefit more from fusion
        }
        
        // Penalize complex operands
        if (inst->getOperands().size() > 3) {
            benefit *= 0.9;
        }
    }
    
    return benefit;
}

void InstructionFusion::applyFusion(codegen::CodeGen& cg, const FusionCandidate& candidate) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Applying fusion pattern: " << candidate.pattern->name << "\n";
    }
    candidate.pattern->emitter(cg, candidate.instructions);
}

// Pattern matchers
bool InstructionFusion::isMultiplyAddPattern(const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() != 2) return false;
    
    auto* mul = instructions[0];
    auto* add = instructions[1];
    
    if (mul->getOpcode() != ir::Instruction::Mul && mul->getOpcode() != ir::Instruction::FMul) {
        return false;
    }
    
    if (add->getOpcode() != ir::Instruction::Add && add->getOpcode() != ir::Instruction::FAdd) {
        return false;
    }
    
    // Check if add uses result of multiply
    for (auto& operand : add->getOperands()) {
        if (operand->get() == mul) {
            return true;
        }
    }
    
    return false;
}

bool InstructionFusion::isLoadOperatePattern(const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() != 2) return false;
    
    auto* load = instructions[0];
    auto* op = instructions[1];
    
    if (load->getOpcode() != ir::Instruction::Load) {
        return false;
    }
    
    // Check if operation can work with memory operand
    switch (op->getOpcode()) {
        case ir::Instruction::Add:
        case ir::Instruction::Sub:
        case ir::Instruction::And:
        case ir::Instruction::Or:
        case ir::Instruction::Xor:
            break;
        default:
            return false;
    }
    
    // Check if operation uses loaded value
    for (auto& operand : op->getOperands()) {
        if (operand->get() == load) {
            return true;
        }
    }
    
    return false;
}

bool InstructionFusion::isCompareAndBranchPattern(const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() != 2) return false;
    
    auto* cmp = instructions[0];
    auto* br = instructions[1];
    
    // Check if first instruction is a comparison
    switch (cmp->getOpcode()) {
        case ir::Instruction::Ceq:
        case ir::Instruction::Cne:
        case ir::Instruction::Cslt:
        case ir::Instruction::Csle:
        case ir::Instruction::Csgt:
        case ir::Instruction::Csge:
            break;
        default:
            return false;
    }
    
    // Check if second instruction is a conditional branch
    if (br->getOpcode() != ir::Instruction::Jnz) {
        return false;
    }
    
    // Check if branch uses comparison result
    for (auto& operand : br->getOperands()) {
        if (operand->get() == cmp) {
            return true;
        }
    }
    
    return false;
}

bool InstructionFusion::isAddressCalculationPattern(const std::vector<ir::Instruction*>& instructions) {
    // Look for address calculation sequences like: base + index*scale + offset
    if (instructions.size() < 2) return false;
    
    // Simple heuristic: look for arithmetic operations followed by load/store
    auto* lastInst = instructions.back();
    return (lastInst->getOpcode() == ir::Instruction::Load || 
            lastInst->getOpcode() == ir::Instruction::Store);
}

// Pattern emitters
void InstructionFusion::emitFusedMultiplyAdd(codegen::CodeGen& cg,
                                           const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() != 2) return;
    
    auto* mul = instructions[0];
    auto* add = instructions[1];
    
    // Create fused instruction
    std::vector<ir::Value*> operands;
    operands.push_back(mul->getOperands()[0]->get()); // a
    operands.push_back(mul->getOperands()[1]->get()); // b
    
    // Find the addend (the operand of add that's not the multiply result)
    for (auto& operand : add->getOperands()) {
        if (operand->get() != mul) {
            operands.push_back(operand->get()); // c
            break;
        }
    }
    
    ir::FusedInstruction fusedInst(add->getType(), ir::Instruction::FMA, operands,
                                  ir::FusedInstruction::MultiplyAdd);
    
    // Get target info and emit
    auto* targetInfo = cg.getTargetInfo();
    targetInfo->emitFusedMultiplyAdd(cg, fusedInst);
}

void InstructionFusion::emitLoadAndOperate(codegen::CodeGen& cg,
                                         const std::vector<ir::Instruction*>& instructions) {
    if (instructions.size() != 2) return;
    
    auto* load = instructions[0];
    auto* op = instructions[1];
    
    auto* targetInfo = cg.getTargetInfo();
    targetInfo->emitLoadAndOperate(cg, *load, *op);
}

void InstructionFusion::emitCompareAndBranch(codegen::CodeGen& cg,
                                           const std::vector<ir::Instruction*>& instructions) {
    if (auto* os = cg.getTextStream()) {
        if (instructions.size() != 2) return;

        auto* cmp = instructions[0];
        auto* br = instructions[1];

        // Emit fused compare-and-branch
        std::string lhs = cg.getValueAsOperand(cmp->getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(cmp->getOperands()[1]->get());
        std::string label = cg.getValueAsOperand(br->getOperands()[0]->get());

        // Get condition from comparison opcode
        std::string condition;
        switch (cmp->getOpcode()) {
            case ir::Instruction::Ceq: condition = "eq"; break;
            case ir::Instruction::Cne: condition = "ne"; break;
            case ir::Instruction::Cslt: condition = "lt"; break;
            case ir::Instruction::Csle: condition = "le"; break;
            case ir::Instruction::Csgt: condition = "gt"; break;
            case ir::Instruction::Csge: condition = "ge"; break;
            default: condition = "eq";
        }

        *os << "  cmp " << lhs << ", " << rhs << "\n";
        *os << "  b." << condition << " " << label << "\n";
    }
}

void InstructionFusion::emitComplexAddressing(codegen::CodeGen& cg,
                                            const std::vector<ir::Instruction*>& instructions) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Complex addressing mode fusion not yet implemented\n";
    }
}

// AddressingModeAnalyzer implementation
AddressingModeAnalyzer::AddressExpression 
AddressingModeAnalyzer::analyzeAddress(ir::Instruction* addrInst) {
    AddressExpression expr;
    
    if (!addrInst) return expr;
    
    // Simple analysis for now - detect base + offset patterns
    if (addrInst->getOpcode() == ir::Instruction::Add && addrInst->getOperands().size() == 2) {
        auto* op1 = addrInst->getOperands()[0]->get();
        auto* op2 = addrInst->getOperands()[1]->get();
        
        // Check if one operand is a constant
        if (auto* constant = dynamic_cast<ir::Constant*>(op2)) {
            expr.base = op1;
            expr.offset = 0; // Would extract actual constant value
            expr.isValid = true;
        } else if (auto* constant = dynamic_cast<ir::Constant*>(op1)) {
            expr.base = op2;
            expr.offset = 0; // Would extract actual constant value
            expr.isValid = true;
        } else {
            // Both are variables - might be base + index
            expr.base = op1;
            expr.index = op2;
            expr.scale = 1;
            expr.isValid = true;
        }
    }
    
    return expr;
}

bool AddressingModeAnalyzer::canOptimizeAddressing(ir::Instruction* loadStore) {
    if (!loadStore) return false;
    
    if (loadStore->getOpcode() != ir::Instruction::Load && 
        loadStore->getOpcode() != ir::Instruction::Store) {
        return false;
    }
    
    auto* addrOperand = loadStore->getOperands()[0]->get();
    if (auto* addrInst = dynamic_cast<ir::Instruction*>(addrOperand)) {
        AddressExpression expr = analyzeAddress(addrInst);
        return expr.isValid;
    }
    
    return false;
}

AddressingMode AddressingModeAnalyzer::generateAddressingMode(const AddressExpression& expr, 
                                                            const std::string& targetArch) {
    AddressingMode mode;
    
    if (!expr.isValid) return mode;
    
    if (expr.index && expr.offset == 0) {
        if (expr.scale > 1) {
            mode.type = AddressingMode::ScaleIndex;
            mode.scale = expr.scale;
        } else {
            mode.type = AddressingMode::ScaleIndex;
            mode.scale = 1;
        }
        mode.indexReg = "x1"; // Simplified
    } else if (expr.offset != 0) {
        mode.type = AddressingMode::Offset;
        mode.offset = expr.offset;
    } else {
        mode.type = AddressingMode::Simple;
    }
    
    mode.baseReg = "x0"; // Simplified
    return mode;
}

// Additional helper implementations would go here...

// Stub implementations for complex classes that would be fully implemented
std::vector<FMAOptimizer::FMAPattern> FMAOptimizer::findFMAPatterns(ir::BasicBlock& block) {
    return {}; // Simplified stub
}

bool FMAOptimizer::isProfitableToFuse(const FMAPattern& pattern) {
    return true; // Simplified
}

std::vector<ir::Instruction*> AddressingModeAnalyzer::findAddressingOptimizations(ir::BasicBlock& block) {
    return {}; // Simplified stub
}

} // namespace target
} // namespace codegen