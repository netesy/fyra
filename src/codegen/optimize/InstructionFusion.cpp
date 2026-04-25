#include "codegen/optimize/InstructionFusion.h"
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


// FMAOptimizer implementation
std::vector<FMAOptimizer::FMAPattern> FMAOptimizer::findFMAPatterns(ir::BasicBlock& block) {
    std::vector<FMAPattern> patterns;
    auto& instructions = block.getInstructions();
    
    auto it = instructions.begin();
    auto next_it = std::next(it);
    
    while (it != instructions.end() && next_it != instructions.end()) {
        auto* inst1 = it->get();
        auto* inst2 = next_it->get();
        
        // Check for multiply followed by add/subtract
        if ((inst1->getOpcode() == ir::Instruction::Mul || inst1->getOpcode() == ir::Instruction::FMul) &&
            (inst2->getOpcode() == ir::Instruction::Add || inst2->getOpcode() == ir::Instruction::FAdd ||
             inst2->getOpcode() == ir::Instruction::Sub || inst2->getOpcode() == ir::Instruction::FSub)) {
            
            if (canFormFMA(inst1, inst2)) {
                FMAPattern pattern;
                pattern.multiply = inst1;
                pattern.add = inst2;
                pattern.isSubtract = (inst2->getOpcode() == ir::Instruction::Sub || 
                                     inst2->getOpcode() == ir::Instruction::FSub);
                pattern.isNegated = false;
                
                // Extract operands
                if (inst1->getOperands().size() >= 2) {
                    pattern.multiplicand1 = inst1->getOperands()[0]->get();
                    pattern.multiplicand2 = inst1->getOperands()[1]->get();
                }
                
                // Find the addend (operand of add that's not the multiply result)
                for (auto& operand : inst2->getOperands()) {
                    if (operand->get() != inst1) {
                        pattern.addend = operand->get();
                        break;
                    }
                }
                
                patterns.push_back(pattern);
            }
        }
        
        ++it;
        ++next_it;
    }
    
    return patterns;
}

bool FMAOptimizer::canFormFMA(ir::Instruction* mul, ir::Instruction* add) {
    if (!mul || !add) return false;
    
    // Check if add uses the result of multiply
    bool usesMultiplyResult = false;
    for (auto& operand : add->getOperands()) {
        if (operand->get() == mul) {
            usesMultiplyResult = true;
            break;
        }
    }
    
    if (!usesMultiplyResult) return false;
    // Validate operands
    if (mul->getOperands().size() < 2) return false;
    if (add->getOperands().size() < 2) return false;
    
    auto* op1 = mul->getOperands()[0]->get();
    auto* op2 = mul->getOperands()[1]->get();
    ir::Value* addend = nullptr;
    
    for (auto& operand : add->getOperands()) {
        if (operand->get() != mul) {
            addend = operand->get();
            break;
        }
    }
    
    return isValidFMAOperands(op1, op2, addend);
}

bool FMAOptimizer::isValidFMAOperands(ir::Value* a, ir::Value* b, ir::Value* c) {
    if (!a || !b || !c) return false;
    
    // Check if all operands are of compatible types
    auto* typeA = a->getType();
    auto* typeB = b->getType();
    auto* typeC = c->getType();
    
    // All should be floating point or all should be integer
    bool allFloat = typeA->isFloatingPoint() && typeB->isFloatingPoint() && typeC->isFloatingPoint();
    bool allInt = !typeA->isFloatingPoint() && !typeB->isFloatingPoint() && !typeC->isFloatingPoint();
    
    return allFloat || allInt;
}

ir::FusedInstruction* FMAOptimizer::createFMAInstruction(const FMAPattern& pattern) {
    std::vector<ir::Value*> operands;
    operands.push_back(pattern.multiplicand1);
    operands.push_back(pattern.multiplicand2);
    operands.push_back(pattern.addend);
    
    ir::Instruction::Opcode opcode = pattern.isSubtract ? ir::Instruction::FMS : ir::Instruction::FMA;
    ir::FusedInstruction::FusedType fusionType = pattern.isSubtract ? 
        ir::FusedInstruction::MultiplySubtract : ir::FusedInstruction::MultiplyAdd;
    
    return new ir::FusedInstruction(pattern.add->getType(), opcode, operands, fusionType, nullptr);
}

void FMAOptimizer::replaceFMAPattern(ir::BasicBlock& block, const FMAPattern& pattern) {
    auto& instructions = block.getInstructions();
    auto it = std::find_if(instructions.begin(), instructions.end(), 
        [&pattern](const auto& inst) { return inst.get() == pattern.multiply; });
    
    if (it != instructions.end()) {
        auto* fusedInst = createFMAInstruction(pattern);
        *it = std::unique_ptr<ir::Instruction>(fusedInst);
        
        // Remove the add instruction
        auto addIt = std::find_if(instructions.begin(), instructions.end(),
            [&pattern](const auto& inst) { return inst.get() == pattern.add; });
        if (addIt != instructions.end()) {
            instructions.erase(addIt);
        }
    }
}

double FMAOptimizer::estimateFMABenefit(const FMAPattern& pattern) {
    // FMA typically provides 2x speedup for multiply-add sequences
    double benefit = 2.0;
    
    // Additional benefit for floating point operations
    if (pattern.multiply->getType()->isFloatingPoint()) {
        benefit *= 1.2;
    }
    
    // Penalty if operands are complex (e.g., memory loads)
    if (pattern.multiplicand1->getType()->isPointerTy() || 
        pattern.multiplicand2->getType()->isPointerTy()) {
        benefit *= 0.8;
    }
    
    return benefit;
}

bool FMAOptimizer::isProfitableToFuse(const FMAPattern& pattern) {
    return estimateFMABenefit(pattern) > 1.0;
}

bool FMAOptimizer::targetSupportsFMA(const std::string& targetArch) {
    // Common architectures that support FMA
    static const std::vector<std::string> fmaArchs = {
        "x86_64", "x64", "aarch64", "arm64", "riscv64"
    };
    
    for (const auto& arch : fmaArchs) {
        if (targetArch.find(arch) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string FMAOptimizer::getFMAInstruction(const FMAPattern& pattern, const std::string& targetArch) {
    if (targetArch.find("x86_64") != std::string::npos || targetArch.find("x64") != std::string::npos) {
        return pattern.isSubtract ? "vfmsub132sd" : "vfmadd132sd";
    } else if (targetArch.find("aarch64") != std::string::npos || targetArch.find("arm64") != std::string::npos) {
        return pattern.isSubtract ? "fmsub" : "fmadd";
    } else if (targetArch.find("riscv64") != std::string::npos) {
        return pattern.isSubtract ? "fmadd.d" : "fmadd.d"; // RISC-V uses fmadd for both with negation
    }
    
    return "fma";
}

// LoadOperateFusion implementation
std::vector<LoadOperateFusion::LoadOperatePattern> LoadOperateFusion::findLoadOperatePatterns(ir::BasicBlock& block) {
    std::vector<LoadOperatePattern> patterns;
    auto& instructions = block.getInstructions();
    
    auto it = instructions.begin();
    auto next_it = std::next(it);
    
    while (it != instructions.end() && next_it != instructions.end()) {
        auto* load = it->get();
        auto* op = next_it->get();
        
        if (load->getOpcode() == ir::Instruction::Load && canFuseLoadWithOperation(load, op)) {
            LoadOperatePattern pattern;
            pattern.load = load;
            pattern.operate = op;
            pattern.canFuseWithMemory = supportsMemoryOperand(op->getOpcode());
            
            // Analyze addressing mode
            if (auto* addrInst = dynamic_cast<ir::Instruction*>(load->getOperands()[0]->get())) {
                auto expr = AddressingModeAnalyzer::analyzeAddress(addrInst);
                pattern.addressing = AddressingModeAnalyzer::generateAddressingMode(expr, "x86_64");
            }
            
            patterns.push_back(pattern);
        }
        
        ++it;
        ++next_it;
    }
    
    return patterns;
}

bool LoadOperateFusion::canFuseLoadWithOperation(ir::Instruction* load, ir::Instruction* op) {
    if (!load || !op) return false;
    if (load->getOpcode() != ir::Instruction::Load) return false;
    
    // Check if operation uses loaded value
    bool usesLoadedValue = false;
    for (auto& operand : op->getOperands()) {
        if (operand->get() == load) {
            usesLoadedValue = true;
            break;
        }
    }
    
    if (!usesLoadedValue) return false;
    
    return supportsMemoryOperand(op->getOpcode());
}

bool LoadOperateFusion::supportsMemoryOperand(ir::Instruction::Opcode opcode) {
    // Operations that typically support memory operands on x86/x64
    static const std::vector<ir::Instruction::Opcode> memoryOps = {
        ir::Instruction::Add, ir::Instruction::Sub, ir::Instruction::And,
        ir::Instruction::Or, ir::Instruction::Xor, ir::Instruction::Mul
    };
    
    return std::find(memoryOps.begin(), memoryOps.end(), opcode) != memoryOps.end();
}

void LoadOperateFusion::applyLoadOperateFusion(ir::BasicBlock& block, const LoadOperatePattern& pattern) {
    auto& instructions = block.getInstructions();
    auto it = std::find_if(instructions.begin(), instructions.end(),
        [&pattern](const auto& inst) { return inst.get() == pattern.load; });
    
    if (it != instructions.end()) {
        // Replace the operation with a memory-operate version
        // This would require target-specific implementation
        // For now, we'll just leave it as-is since the actual fusion
        // would be done during code generation
    }
}

std::string LoadOperateFusion::generateFusedInstruction(const LoadOperatePattern& pattern, const std::string& targetArch) {
    std::string opcode;
    
    switch (pattern.operate->getOpcode()) {
        case ir::Instruction::Add: opcode = "add"; break;
        case ir::Instruction::Sub: opcode = "sub"; break;
        case ir::Instruction::And: opcode = "and"; break;
        case ir::Instruction::Or: opcode = "or"; break;
        case ir::Instruction::Xor: opcode = "xor"; break;
        case ir::Instruction::Mul: opcode = "mul"; break;
        default: opcode = "mov"; break;
    }
    
    return opcode + " [mem], reg"; // Simplified representation
}

double LoadOperateFusion::estimateLoadOperateBenefit(const LoadOperatePattern& pattern) {
    // Load-operate fusion typically saves one instruction
    double benefit = 1.5;
    
    // Additional benefit if addressing mode is complex
    if (pattern.addressing.type != AddressingMode::Simple) {
        benefit *= 1.2;
    }
    
    return benefit;
}

bool LoadOperateFusion::isWorthFusing(const LoadOperatePattern& pattern) {
    return estimateLoadOperateBenefit(pattern) > 1.0;
}

// CompareAndBranchFusion implementation
std::vector<CompareAndBranchFusion::CompareAndBranchPattern> CompareAndBranchFusion::findCompareAndBranchPatterns(ir::BasicBlock& block) {
    std::vector<CompareAndBranchPattern> patterns;
    auto& instructions = block.getInstructions();
    
    auto it = instructions.begin();
    auto next_it = std::next(it);
    
    while (it != instructions.end() && next_it != instructions.end()) {
        auto* cmp = it->get();
        auto* br = next_it->get();
        
        if (canFuseCompareWithBranch(cmp, br)) {
            CompareAndBranchPattern pattern;
            pattern.compare = cmp;
            pattern.branch = br;
            pattern.condition = extractCondition(cmp);
            
            if (cmp->getOperands().size() >= 2) {
                pattern.lhs = cmp->getOperands()[0]->get();
                pattern.rhs = cmp->getOperands()[1]->get();
            }
            
            patterns.push_back(pattern);
        }
        
        ++it;
        ++next_it;
    }
    
    return patterns;
}

bool CompareAndBranchFusion::canFuseCompareWithBranch(ir::Instruction* cmp, ir::Instruction* br) {
    if (!cmp || !br) return false;
    
    // Check if cmp is a comparison
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
    
    // Check if br is a conditional branch
    if (br->getOpcode() != ir::Instruction::Jnz && br->getOpcode() != ir::Instruction::Jz) {
        return false;
    }
    
    // Check if branch uses comparison result
    bool usesCmpResult = false;
    for (auto& operand : br->getOperands()) {
        if (operand->get() == cmp) {
            usesCmpResult = true;
            break;
        }
    }
    
    return usesCmpResult;
}

std::string CompareAndBranchFusion::extractCondition(ir::Instruction* cmp) {
    switch (cmp->getOpcode()) {
        case ir::Instruction::Ceq: return "eq";
        case ir::Instruction::Cne: return "ne";
        case ir::Instruction::Cslt: return "lt";
        case ir::Instruction::Csle: return "le";
        case ir::Instruction::Csgt: return "gt";
        case ir::Instruction::Csge: return "ge";
        default: return "eq";
    }
}

void CompareAndBranchFusion::applyCompareAndBranchFusion(ir::BasicBlock& block, const CompareAndBranchPattern& pattern) {
    auto& instructions = block.getInstructions();
    auto it = std::find_if(instructions.begin(), instructions.end(),
        [&pattern](const auto& inst) { return inst.get() == pattern.compare; });
    
    if (it != instructions.end()) {
        // Replace the compare-branch sequence with a fused instruction
        // This would require target-specific implementation
        // For now, we'll just leave it as-is since the actual fusion
        // would be done during code generation
    }
}

std::string CompareAndBranchFusion::generateFusedBranch(const CompareAndBranchPattern& pattern, const std::string& targetArch) {
    if (targetArch.find("x86_64") != std::string::npos || targetArch.find("x64") != std::string::npos) {
        return "cmp " + pattern.condition + " [reg], [reg]";
    } else if (targetArch.find("aarch64") != std::string::npos || targetArch.find("arm64") != std::string::npos) {
        return "cmp " + pattern.condition + " w0, w1";
    }
    
    return "cmp " + pattern.condition;
}

double CompareAndBranchFusion::estimateCompareAndBranchBenefit(const CompareAndBranchPattern& pattern) {
    // Compare-and-branch fusion typically saves one instruction and reduces branch latency
    return 1.3;
}

bool CompareAndBranchFusion::shouldFuseCompareAndBranch(const CompareAndBranchPattern& pattern) {
    return estimateCompareAndBranchBenefit(pattern) > 1.0;
}

// AddressingModeAnalyzer implementation
std::vector<ir::Instruction*> AddressingModeAnalyzer::findAddressingOptimizations(ir::BasicBlock& block) {
    std::vector<ir::Instruction*> optimizations;
    auto& instructions = block.getInstructions();
    
    for (auto& inst : instructions) {
        if (canOptimizeAddressing(inst.get())) {
            optimizations.push_back(inst.get());
        }
    }
    
    return optimizations;
}

} // namespace target
} // namespace codegen