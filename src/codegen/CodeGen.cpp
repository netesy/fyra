#include "codegen/CodeGen.h"
#include "codegen/execgen/Assembler.h"
#include "ir/Constant.h"
#include "ir/GlobalValue.h"
#include "ir/Function.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "codegen/debug/DWARFGenerator.h"
#include <algorithm>
#include "codegen/InstructionFusion.h"
#include "codegen/target/Wasm32.h"
#include <cstring>
#include <iostream>

namespace codegen {

CodeGen::CodeGen(ir::Module& module, std::unique_ptr<target::TargetInfo> ti, std::ostream* os)
    : module(module), targetInfo(std::move(ti)),
      assembler(std::make_unique<execgen::Assembler>()),
      rodataAssembler(std::make_unique<execgen::Assembler>()), os(os),
      fusionCoordinator(std::make_unique<target::FusionCoordinator>()),
      debugInfoManager(std::make_unique<debug::DebugInfoManager>()) {
    initializeInstructionPatterns();

    // Initialize available registers from target info
    auto& intRegs = targetInfo->getRegisters(target::RegisterClass::Integer);
    availableRegisters.insert(availableRegisters.end(), intRegs.begin(), intRegs.end());

    // Initialize register usage map
    for (const auto& reg : availableRegisters) {
        registerUsage[reg] = false;
    }

    // Configure fusion coordinator with target info
    fusionCoordinator->setTargetArchitecture(targetInfo->getName());
}

CodeGen::~CodeGen() = default;

void CodeGen::emit(bool forExecutable) {
    // Scan module for feature usage
    usesHeap = false;
    usesFPNeg = false;
    for (auto& func : module.getFunctions()) {
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getOpcode() == ir::Instruction::Alloc ||
                    instr->getOpcode() == ir::Instruction::Alloc4 ||
                    instr->getOpcode() == ir::Instruction::Alloc16) {
                    usesHeap = true;
                }
                if (instr->getOpcode() == ir::Instruction::Neg &&
                    instr->getType()->isFloatingPoint()) {
                    usesFPNeg = true;
                }
            }
        }
    }

    if (targetInfo->getName() == "wasm32" && !os) {
        // Binary format
        auto* wasmTarget = static_cast<target::Wasm32*>(targetInfo.get());

        for (auto& func : module.getFunctions()) {
            emitFunction(*func);
        }

        wasmTarget->emitHeader(*this);
        wasmTarget->emitTypeSection(*this);
        wasmTarget->emitFunctionSection(*this);
        wasmTarget->emitExportSection(*this);
        wasmTarget->emitCodeSection(*this);
    } else {
        if (targetInfo->getName() != "wasm32") {
            // Collect all float constants
            int float_const_count = 0;
            for (auto& func : module.getFunctions()) {
                for (auto& bb : func->getBasicBlocks()) {
                    for (auto& instr : bb->getInstructions()) {
                        for (auto& operand : instr->getOperands()) {
                            if (auto* constFP = dynamic_cast<ir::ConstantFP*>(operand->get())) {
                                if (floatConstantLabels.find(constFP) == floatConstantLabels.end()) {
                                    std::string label = targetInfo->getLabelPrefix() + "CF" + std::to_string(float_const_count++);
                                    floatConstantLabels[constFP] = label;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Enhanced assembly emission
        emitTargetSpecificHeader();
        emitDataSection();
        emitTextSection();

        if (forExecutable) {
            targetInfo->emitStartFunction(*this);
        }

        if (targetInfo->getName() == "wasm32") {
            // Already handled in emitTargetSpecificHeader
        } else {
            // Enable debug info if requested
            debugInfoManager->enableDebugInfo(true);
            if (os) debugInfoManager->getDWARFGenerator()->beginCompileUnit("main.fy", "Fyra Compiler");
        }

        for (auto& func : module.getFunctions()) {
            performTargetOptimizations(*func);
            // Instruction fusion is currently experimental and may break binary emission
            // performInstructionFusion(*func);
            emitFunction(*func);
        }

        if (targetInfo->getName() == "wasm32") {
            if (os) *os << ")\n";
        } else {
            // Generate debug information
            if (os) debugInfoManager->generateDebugInfo(*this, *os, module);
        }
    }
}

void CodeGen::performInstructionFusion(ir::Function& func) {
    // Apply instruction fusion optimizations
    fusionCoordinator->optimizeFunction(func);
}

void CodeGen::applyFusionOptimizations(ir::BasicBlock& block) {
    // Apply fusion optimizations to a basic block
    fusionCoordinator->optimizeBasicBlock(block);
}

void CodeGen::enableDebugInfo(bool enable) {
    debugInfoManager->enableDebugInfo(enable);
}

void CodeGen::setCurrentLocation(const std::string& file, unsigned line) {
    debugInfoManager->setCurrentLocation(file, line);
}

void CodeGen::emitDebugInfo() {
    // This would be called at the end of emission to output debug sections
}

void CodeGen::emitFunction(ir::Function& func) {

    // Set current function for WASM parameter resolution
    currentFunction = &func;

    // Reset stack offsets for each function to ensure fresh local mapping
    stackOffsets.clear();
    
    if (targetInfo->getName() == "wasm32" && !os) {
        if (auto* funcType = dynamic_cast<const ir::FunctionType*>(func.getType())) {
            if (wasmTypeIndices.find(funcType) == wasmTypeIndices.end()) {
                wasmTypeIndices[funcType] = wasmTypeIndices.size();
            }
        }
        if (wasmFunctionIndices.find(&func) == wasmFunctionIndices.end()) {
            wasmFunctionIndices[&func] = wasmFunctionIndices.size();
        }

        auto funcBodyAssembler = std::make_unique<execgen::Assembler>();
        auto oldAssembler = std::move(assembler);
        assembler = std::move(funcBodyAssembler);

        for (auto& bb : func.getBasicBlocks()) {
            emitBasicBlock(*bb);
        }

        wasmFunctionBodies.push_back(assembler->getCode());

        assembler = std::move(oldAssembler);
    } else if (targetInfo->getName() == "wasm32") {
        emitWasmFunctionSignature(func);
        targetInfo->emitStructuredFunctionBody(*this, func);
        if (os) *os << "  )\n";

        // Add export if function is exported
        if (func.isExported()) {
            std::string funcName = func.getName();
            if (funcName.rfind("$", 0) == 0) {
                funcName = funcName.substr(1);
            }
            if (os) *os << "  (export \"" << funcName << "\" (func $" << funcName << "))\n";
        }
    } else {
        uint64_t func_start_offset = 0;
        if (!os) {
            func_start_offset = getAssembler().getCodeSize();
            SymbolInfo func_sym;
            func_sym.name = func.getName();
            func_sym.sectionName = ".text";
            func_sym.value = func_start_offset;
            func_sym.type = 2; // STT_FUNC
            func_sym.binding = 1; // STB_GLOBAL
            symbols.push_back(func_sym);
        }

        if (os) {
            *os << "\n# --- Function: " << func.getName() << " ---\n";
            debugInfoManager->beforeFunctionEmission(*this, *os, func);
            *os << func.getName() << ":\n";
        }

        stackOffsets.clear();

        targetInfo->emitFunctionPrologue(*this, func);

        for (auto& bb : func.getBasicBlocks()) {
            emitBasicBlock(*bb);
        }

        if (os) {
            debugInfoManager->afterFunctionEmission(*this, *os, func, 0x1000);
        } else {
            for (auto& sym : symbols) {
                if (sym.name == func.getName()) {
                    sym.size = getAssembler().getCodeSize() - func_start_offset;
                    break;
                }
            }
        }
    }
}

void CodeGen::emitBasicBlock(ir::BasicBlock& bb) {
    if (targetInfo->getName() != "wasm32") {
        if (!os) {
            // Binary mode: emit label symbol
            SymbolInfo sym;
            sym.name = bb.getParent()->getName() + "_" + bb.getName();
            sym.sectionName = ".text";
            sym.value = getAssembler().getCodeSize();
            sym.type = 0; // NOTYPE
            sym.binding = 0; // LOCAL
            sym.size = 0;
            symbols.push_back(sym);
        }

        if (os) {
            *os << "\n" << bb.getParent()->getName() << "_" << bb.getName() << ":\n";
        }
        for (auto it = bb.getInstructions().begin(); it != bb.getInstructions().end(); ++it) {
            auto& instr = *it;
            // Before emitting the terminator, resolve PHI nodes in successors
            bool isTerm = false;
            switch (instr->getOpcode()) {
                case ir::Instruction::Ret:
                case ir::Instruction::Jmp:
                case ir::Instruction::Jnz:
                case ir::Instruction::Br:
                    isTerm = true;
                    break;
                default: break;
            }

            if (isTerm) {
                for (auto* succ : bb.getSuccessors()) {
                    for (auto& succInstr : succ->getInstructions()) {
                        if (auto* phi = dynamic_cast<ir::PhiNode*>(succInstr.get())) {
                            ir::Value* val = phi->getIncomingValueForBlock(&bb);
                            if (val) {
                                if (os) {
                                    *os << "  # Resolve PHI for " << phi->getName() << " from " << bb.getName() << "\n";
                                    std::string valOp = getValueAsOperand(val);
                                    std::string phiOp = getValueAsOperand(phi);
                                    if (phi->getType()->isFloatingPoint()) {
                                        *os << "  movss " << valOp << ", %xmm0\n";
                                        *os << "  movss %xmm0, " << phiOp << "\n";
                                    } else {
                                        std::string reg = (phi->getType()->getSize() > 4) ? "%rax" : "%eax";
                                        std::string mov = (phi->getType()->getSize() > 4) ? "movq" : "movl";
                                        *os << "  " << mov << " " << valOp << ", " << reg << "\n";
                                        *os << "  " << mov << " " << reg << ", " << phiOp << "\n";
                                    }
                                } else {
                                    // Binary mode
                                    auto& assembler = getAssembler();
                                    int32_t valOffset = getStackOffset(val);
                                    int32_t phiOffset = getStackOffset(phi);
                                    bool is64 = phi->getType()->getSize() > 4;

                                    // Use a temporary register (e.g., rax) to move value to phi slot
                                    // We need a way to load the value regardless of whether it's on stack, global, or constant.
                                    // This is tricky because emitLoadValue is private in SystemV_x64.cpp.
                                    // For now, let's assume we can at least handle stack values correctly.
                                    // Actually, we should probably add a helper to TargetInfo for this.

                                    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
                                        if (is64) assembler.emitByte(0x48);
                                        assembler.emitByte(0xB8);
                                        if (is64) assembler.emitQWord(constInt->getValue());
                                        else assembler.emitDWord(constInt->getValue());
                                    } else if (stackOffsets.count(val)) {
                                        if (is64) assembler.emitByte(0x48);
                                        assembler.emitByte(0x8B);
                                        if (valOffset >= -128 && valOffset <= 127) {
                                            assembler.emitByte(0x45); assembler.emitByte(static_cast<int8_t>(valOffset));
                                        } else {
                                            assembler.emitByte(0x85); assembler.emitDWord(static_cast<uint32_t>(valOffset));
                                        }
                                    } else {
                                        // Global or other - simplified for now
                                        if (is64) assembler.emitByte(0x48);
                                        assembler.emitByte(0xC7); // mov imm32, r/m64
                                        assembler.emitByte(0xC0); // rax
                                        assembler.emitDWord(0); // placeholder
                                    }

                                    // mov %rax/eax, phiOffset(%rbp)
                                    if (is64) assembler.emitByte(0x48);
                                    assembler.emitByte(0x89);
                                    if (phiOffset >= -128 && phiOffset <= 127) {
                                        assembler.emitByte(0x45); assembler.emitByte(static_cast<int8_t>(phiOffset));
                                    } else {
                                        assembler.emitByte(0x85); assembler.emitDWord(static_cast<uint32_t>(phiOffset));
                                    }
                                }
                            }
                        } else {
                            break; // PHIs are always at the start
                        }
                    }
                }
            }
            emitInstruction(*instr);
        }
    } else if (os) {
        // WASM structured control flow
        // For WASM, emit all instructions - the branch instruction will handle structured control flow
        for (auto& instr : bb.getInstructions()) {
            emitInstruction(*instr);
        }
    } else {
        // Binary mode
        for (auto& instr : bb.getInstructions()) {
            emitInstruction(*instr);
        }
    }
}

void CodeGen::emitInstruction(ir::Instruction& instr) {
    if (os) {
        // Debug info before instruction
        debugInfoManager->beforeInstructionEmission(*this, *os, instr, 0x1000); // Placeholder address
    }
    
    // Dispatch to the target for instruction emission
    switch (instr.getOpcode()) {
        case ir::Instruction::Ret:
            targetInfo->emitRet(*this, instr);
            break;
        case ir::Instruction::Add:
            targetInfo->emitAdd(*this, instr);
            break;
        case ir::Instruction::Sub:
            targetInfo->emitSub(*this, instr);
            break;
        case ir::Instruction::Mul:
            targetInfo->emitMul(*this, instr);
            break;
        case ir::Instruction::Div:
        case ir::Instruction::Udiv:
            targetInfo->emitDiv(*this, instr);
            break;
        case ir::Instruction::Rem:
        case ir::Instruction::Urem:
            targetInfo->emitRem(*this, instr);
            break;
        case ir::Instruction::And:
            targetInfo->emitAnd(*this, instr);
            break;
        case ir::Instruction::Or:
            targetInfo->emitOr(*this, instr);
            break;
        case ir::Instruction::Xor:
            targetInfo->emitXor(*this, instr);
            break;
        case ir::Instruction::Shl:
            targetInfo->emitShl(*this, instr);
            break;
        case ir::Instruction::Shr:
            targetInfo->emitShr(*this, instr);
            break;
        case ir::Instruction::Sar:
            targetInfo->emitSar(*this, instr);
            break;
        case ir::Instruction::Syscall:
            targetInfo->emitSyscall(*this, instr);
            break;
        case ir::Instruction::Neg:
            targetInfo->emitNeg(*this, instr);
            break;
        case ir::Instruction::Not:
            targetInfo->emitNot(*this, instr);
            break;
        case ir::Instruction::Copy:
            targetInfo->emitCopy(*this, instr);
            break;
        case ir::Instruction::Call:
            targetInfo->emitCall(*this, instr);
            break;
        case ir::Instruction::Jmp:
            targetInfo->emitJmp(*this, instr);
            break;
        case ir::Instruction::Jnz:
        case ir::Instruction::Br:
            targetInfo->emitBr(*this, instr);
            break;
        case ir::Instruction::FAdd:
            targetInfo->emitFAdd(*this, instr);
            break;
        case ir::Instruction::FSub:
            targetInfo->emitFSub(*this, instr);
            break;
        case ir::Instruction::FMul:
            targetInfo->emitFMul(*this, instr);
            break;
        case ir::Instruction::FDiv:
            targetInfo->emitFDiv(*this, instr);
            break;
        case ir::Instruction::Alloc:
        case ir::Instruction::Alloc4:
        case ir::Instruction::Alloc16:
            targetInfo->emitAlloc(*this, instr);
            break;
        case ir::Instruction::Load:
        case ir::Instruction::Loadd:
        case ir::Instruction::Loads:
        case ir::Instruction::Loadl:
        case ir::Instruction::Loaduw:
        case ir::Instruction::Loadsh:
        case ir::Instruction::Loaduh:
        case ir::Instruction::Loadsb:
        case ir::Instruction::Loadub:
            targetInfo->emitLoad(*this, instr);
            break;
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
            targetInfo->emitStore(*this, instr);
            break;
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
        case ir::Instruction::Cast:
            {
                const ir::Type* fromType = instr.getOperands()[0]->get()->getType();
                const ir::Type* toType = instr.getType();
                targetInfo->emitCast(*this, instr, fromType, toType);
            }
            break;
        // Vector instructions
        case ir::Instruction::VAdd:
        case ir::Instruction::VSub:
        case ir::Instruction::VMul:
        case ir::Instruction::VDiv:
        case ir::Instruction::VFAdd:
        case ir::Instruction::VFSub:
        case ir::Instruction::VFMul:
        case ir::Instruction::VFDiv:
            if (auto* vecInstr = dynamic_cast<ir::VectorInstruction*>(&instr)) {
                targetInfo->emitVectorArithmetic(*this, *vecInstr);
            } else {
                if (os) *os << "  # Error: Vector instruction expected\n";
            }
            break;
        case ir::Instruction::VLoad:
            if (auto* vecInstr = dynamic_cast<ir::VectorInstruction*>(&instr)) {
                targetInfo->emitVectorLoad(*this, *vecInstr);
            } else {
                if (os) *os << "  # Error: Vector instruction expected\n";
            }
            break;
        case ir::Instruction::VStore:
            if (auto* vecInstr = dynamic_cast<ir::VectorInstruction*>(&instr)) {
                targetInfo->emitVectorStore(*this, *vecInstr);
            } else {
                if (os) *os << "  # Error: Vector instruction expected\n";
            }
            break;
        // Fused instructions
        case ir::Instruction::FMA:
            if (auto* fusedInstr = dynamic_cast<ir::FusedInstruction*>(&instr)) {
                targetInfo->emitFusedMultiplyAdd(*this, *fusedInstr);
            } else {
                if (os) *os << "  # Error: Fused instruction expected\n";
            }
            break;
        case ir::Instruction::Ceq:
        case ir::Instruction::Cne:
        case ir::Instruction::Csle:
        case ir::Instruction::Cslt:
        case ir::Instruction::Csge:
        case ir::Instruction::Csgt:
        case ir::Instruction::Cule:
        case ir::Instruction::Cult:
        case ir::Instruction::Cuge:
        case ir::Instruction::Cugt:
        case ir::Instruction::Ceqf:
        case ir::Instruction::Cnef:
        case ir::Instruction::Cle:
        case ir::Instruction::Clt:
        case ir::Instruction::Cge:
        case ir::Instruction::Cgt:
        case ir::Instruction::Co:
        case ir::Instruction::Cuo:
            targetInfo->emitCmp(*this, instr);
            break;
        case ir::Instruction::Phi:
            // Phi is handled by TargetInfo during block emission
            break;
        default:
            if (os) *os << "  # Unimplemented instruction " << instr.getOpcode() << "\n";
            break;
    }
}

std::string CodeGen::getValueAsOperand(const ir::Value* value) {
    if (!value) return "NULL_VALUE";
    // Check if this is WASM target - use different handling
    if (targetInfo->getName() == "wasm32") {
        return getWasmValueAsOperand(value);
    }
    
    // Is it a constant?
    if (auto* constInt = dynamic_cast<const ir::ConstantInt*>(value)) {
        return targetInfo->formatConstant(constInt);
    }
    if (auto* constFP = dynamic_cast<const ir::ConstantFP*>(value)) {
        auto it = floatConstantLabels.find(const_cast<ir::ConstantFP*>(constFP));
        if (it != floatConstantLabels.end()) {
            return targetInfo->formatGlobalOperand(it->second);
        }
    }
    if (auto* global = dynamic_cast<const ir::GlobalValue*>(value)) {
        return targetInfo->formatGlobalOperand(global->getName());
    }

    // Is it a temporary stored on the stack?
    if (stackOffsets.count(const_cast<ir::Value*>(value))) {
        return targetInfo->formatStackOperand(stackOffsets[const_cast<ir::Value*>(value)]);
    }

    // Handle BasicBlocks (labels)
    if (auto* bb = dynamic_cast<const ir::BasicBlock*>(value)) {
        return bb->getParent()->getName() + "_" + bb->getName();
    }

    // Fallback for unknown values
    return "UNKNOWN_VALUE";
}

std::string CodeGen::getWasmValueAsOperand(const ir::Value* value) {
    if (!value) return "";

    // Handle constants
    if (auto* constInt = dynamic_cast<const ir::ConstantInt*>(value)) {
        return targetInfo->formatConstant(constInt);
    }

    if (auto* constFP = dynamic_cast<const ir::ConstantFP*>(value)) {
        if (constFP->getType()->isFloatTy()) {
            return "f32.const " + std::to_string(constFP->getValue());
        } else {
            return "f64.const " + std::to_string(constFP->getValue());
        }
    }

    if (auto* constStr = dynamic_cast<const ir::ConstantString*>(value)) {
        return ";; [string literal]";
    }
    
    // Handle function parameters
    if (auto* param = dynamic_cast<const ir::Parameter*>(value)) {
        // Check if we have a stack offset (local index) assigned
        if (stackOffsets.count(const_cast<ir::Value*>(value))) {
            return "local.get " + std::to_string(stackOffsets.at(const_cast<ir::Value*>(value)));
        }

        // Fallback: Find parameter index in current function
        if (currentFunction) {
            const auto& params = currentFunction->getParameters();
            size_t i = 0;
            for (auto it = params.begin(); it != params.end(); ++it, ++i) {
                if (it->get() == param) {
                    return "local.get " + std::to_string(i);
                }
            }
        }
    }
    
    // Handle global values (functions)
    if (auto* func = dynamic_cast<const ir::Function*>(value)) {
        if (!os) {
            auto it = wasmFunctionIndices.find(func);
            if (it != wasmFunctionIndices.end()) {
                return std::to_string(it->second);
            }
        }
        std::string name = func->getName();
        if (name.rfind("$", 0) == 0) name = name.substr(1);
        return "$" + name;
    }

    if (auto* gv = dynamic_cast<const ir::GlobalVariable*>(value)) {
        std::string name = gv->getName();
        if (name.rfind("$", 0) == 0) name = name.substr(1);
        return "$" + name;
    }

    if (auto* global = dynamic_cast<const ir::GlobalValue*>(value)) {
        if (auto* gFunc = module.getFunction(global->getName())) {
             if (!os) {
                auto it = wasmFunctionIndices.find(gFunc);
                if (it != wasmFunctionIndices.end()) {
                    return std::to_string(it->second);
                }
            }
        }
        std::string name = global->getName();
        if (name.rfind("$", 0) == 0) name = name.substr(1);
        return "$" + name;
    }

    if (auto* gv = dynamic_cast<const ir::GlobalVariable*>(value)) {
        // For WASM, global variables are just offsets or actual globals.
        // For now, let's just return their name as a label.
        std::string name = gv->getName();
        if (name.rfind("$", 0) == 0) name = name.substr(1);
        return "$" + name;
    }

    // Is it a temporary stored on the stack (WASM local)?
    if (stackOffsets.count(const_cast<ir::Value*>(value))) {
        return "local.get " + std::to_string(stackOffsets.at(const_cast<ir::Value*>(value)));
    }

    // Handle BasicBlocks (labels)
    if (auto* bb = dynamic_cast<const ir::BasicBlock*>(value)) {
        return "$" + bb->getName();
    }
    
    return "";
}

void CodeGen::emitWasmFunctionSignature(ir::Function& func) {
    if (!os) return;
    
    std::string funcName = func.getName();
    // Remove $ prefix if present
    if (funcName.rfind("$", 0) == 0) {
        funcName = funcName.substr(1);
    }
    
    *os << "  (func $" << funcName;
    
    // Emit parameters
    const auto& params = func.getParameters();
    size_t i = 0;
    for (auto it = params.begin(); it != params.end(); ++it, ++i) {
        auto* paramType = (*it)->getType();
        std::string wasmType = "i32"; // Default to i32
        
        if (auto* intTy = dynamic_cast<const ir::IntegerType*>(paramType)) {
            wasmType = (intTy->getBitwidth() <= 32) ? "i32" : "i64";
        } else if (paramType->isFloatTy()) {
            wasmType = "f32";
        } else if (paramType->isDoubleTy()) {
            wasmType = "f64";
        }
        
        *os << " (param $" << i << " " << wasmType << ")";
    }
    
    // Emit return type - get it from function type
    if (auto* funcType = dynamic_cast<const ir::FunctionType*>(func.getType())) {
        auto* returnType = funcType->getReturnType();
        if (!returnType->isVoidTy()) {
            std::string wasmRetType = "i32"; // Default to i32
            
            if (auto* intTy = dynamic_cast<const ir::IntegerType*>(returnType)) {
                wasmRetType = (intTy->getBitwidth() <= 32) ? "i32" : "i64";
            } else if (returnType->isFloatTy()) {
                wasmRetType = "f32";
            } else if (returnType->isDoubleTy()) {
                wasmRetType = "f64";
            }
            
            *os << " (result " << wasmRetType << ")";
        }
    }
    
    *os << "\n";
}

// Enhanced instruction selection methods
void CodeGen::initializeInstructionPatterns() {
    // Initialize patterns for optimized instruction selection
    // This is a simplified version - real implementation would have many more patterns
}

void CodeGen::selectInstruction(ir::Instruction& instr) {
    // Try to match against instruction patterns for optimization
    for (const auto& pattern : instructionPatterns) {
        if (matchPattern(instr, pattern)) {
            pattern.emitter(*this, instr);
            return;
        }
    }
    // If no pattern matches, fall back to standard emission
}

bool CodeGen::matchPattern(ir::Instruction& instr, const InstructionPattern& pattern) {
    if (instr.getOpcode() != pattern.opcode) {
        return false;
    }
    
    // Check operand types match
    if (instr.getOperands().size() != pattern.operandTypes.size()) {
        return false;
    }
    
    for (size_t i = 0; i < instr.getOperands().size(); ++i) {
        if (instr.getOperands()[i]->get()->getType()->getTypeID() != pattern.operandTypes[i]) {
            return false;
        }
    }
    
    return true;
}

void CodeGen::selectArithmeticInstruction(ir::Instruction& instr) {
    (void)instr;
    if (os) *os << "  # Unhandled arithmetic instruction\n";
}

void CodeGen::selectMemoryInstruction(ir::Instruction& instr) {
    (void)instr;
    if (!os) return;
    // Enhanced memory instruction selection with optimizations
    *os << "  # Enhanced memory instruction selection\n";
}

void CodeGen::selectComparisonInstruction(ir::Instruction& instr) {
    (void)instr;
    if (os) *os << "  # Unhandled comparison\n";
}

void CodeGen::selectControlFlowInstruction(ir::Instruction& instr) {
    (void)instr;
    if (!os) return;
    // Enhanced control flow instruction selection
    *os << "  # Enhanced control flow instruction selection\n";
}

void CodeGen::performTargetOptimizations(ir::Function& func) {
    (void)func;
    // Perform target-specific optimizations
}

std::string CodeGen::allocateRegister(ir::Value* value) {
    // Simple register allocation - find first available register
    for (const auto& reg : availableRegisters) {
        if (!registerUsage[reg]) {
            registerUsage[reg] = true;
            return reg;
        }
    }
    
    // If no registers available, spill to stack
    return getValueAsOperand(value);
}

void CodeGen::deallocateRegister(const std::string& reg) {
    if (registerUsage.count(reg)) {
        registerUsage[reg] = false;
    }
}

bool CodeGen::isImmediateValue(ir::Value* value, int64_t& immediate) {
    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(value)) {
        immediate = constInt->getValue();
        return true;
    }
    return false;
}

bool CodeGen::canFoldIntoMemoryOperand(ir::Value* value) {
    // Check if value can be folded into a memory operand
    return dynamic_cast<ir::GlobalValue*>(value) != nullptr;
}

std::string CodeGen::getOptimizedOperand(ir::Value* value) {
    // Get optimized operand representation
    int64_t immediate;
    if (isImmediateValue(value, immediate)) {
        return "$" + std::to_string(immediate);
    }
    
    if (canFoldIntoMemoryOperand(value)) {
        if (auto* global = dynamic_cast<ir::GlobalValue*>(value)) {
            return global->getName() + "(%rip)";
        }
    }
    
    return getValueAsOperand(value);
}

void CodeGen::emitTargetSpecificHeader() {
    if (!emittedHeader && os) {
        // Emit target-specific assembly directives
        if (targetInfo->getName() == "wasm32") {
            *os << "(module\n";
        } else if (targetInfo->getName().find("windows") != std::string::npos) {
            if (targetInfo->getName().find("aarch64") != std::string::npos) {
                *os << "# Windows ARM64 assembly\n";
            } else {
                *os << "# Windows x64 assembly\n";
                *os << ".intel_syntax noprefix\n";
            }
        } else if (targetInfo->getName() == "aarch64") {
            *os << "# AArch64 assembly\n";
        } else if (targetInfo->getName() == "riscv64") {
            *os << "# RISC-V 64 assembly\n";
        } else {
            *os << "# System V x64 assembly\n";
            *os << ".att_syntax prefix\n";
        }
        
        emittedHeader = true;
    }
}

void CodeGen::emitDataSection() {
    if (targetInfo->getName() == "wasm32") return;
    if (!emittedDataSection && os) {
        bool anyData = !floatConstantLabels.empty() || usesFPNeg || usesHeap || !module.getGlobalVariables().empty();
        if (!anyData) return;

        bool hasRodata = !floatConstantLabels.empty() || usesFPNeg;
        if (hasRodata) {
            *os << ".section .rodata\n";

            // Emit floating-point masks
            if (usesFPNeg) {
                *os << targetInfo->getLabelPrefix() << "neg_mask_float:\n";
                *os << "  .long 0x80000000\n";
                *os << "  .long 0\n";
                *os << "  .long 0\n";
                *os << "  .long 0\n";
                *os << targetInfo->getLabelPrefix() << "neg_mask_double:\n";
                *os << "  .quad 0x8000000000000000\n";
                *os << "  .quad 0\n";
            }

            // Emit float constants
            for (auto const& [konst, label] : floatConstantLabels) {
                *os << label << ":\n";
                if (konst->getType()->isFloatTy()) {
                    *os << "  .float " << konst->getValue() << "\n";
                } else {
                    *os << "  .double " << konst->getValue() << "\n";
                }
            }
        }

        bool hasData = usesHeap || !module.getGlobalVariables().empty();
        if (hasData) {
            *os << ".section .data\n";

            // Emit heap management globals
            if (usesHeap) {
                *os << "__heap_base:\n";
                *os << "  .zero 1048576  # 1MB Heap\n";
                *os << "__heap_ptr:\n";
                *os << "  .quad __heap_base\n";
            }

            // Emit globals
            for (auto& global : module.getGlobalVariables()) {
                *os << global->getName() << ":\n";
                if (auto* init = global->getInitializer()) {
                    if (auto* str = dynamic_cast<ir::ConstantString*>(init)) {
                        *os << "  .asciz \"" << str->getValue() << "\"\n";
                    } else if (auto* arr = dynamic_cast<ir::ConstantArray*>(init)) {
                        for (auto* elem : arr->getValues()) {
                            if (auto* ci = dynamic_cast<ir::ConstantInt*>(elem)) {
                                auto* intTy = static_cast<const ir::IntegerType*>(ci->getType());
                                if (intTy->getBitwidth() == 8) *os << "  .byte " << ci->getValue() << "\n";
                                else if (intTy->getBitwidth() == 16) *os << "  .short " << ci->getValue() << "\n";
                                else if (intTy->getBitwidth() == 32) *os << "  .long " << ci->getValue() << "\n";
                                else if (intTy->getBitwidth() == 64) *os << "  .quad " << ci->getValue() << "\n";
                            }
                        }
                    }
                }
            }
        }
        
        emittedDataSection = true;
    } else if (!emittedDataSection) {
        auto& rodataAsm = getRodataAssembler();

        // Emit heap management globals for binary mode
        if (usesHeap) {
            SymbolInfo base_sym;
            base_sym.name = "__heap_base";
            base_sym.sectionName = ".data";
            base_sym.value = rodataAsm.getCodeSize();
            base_sym.type = 1; // STT_OBJECT
            base_sym.binding = 1; // STB_GLOBAL
            for (int i = 0; i < 4096; ++i) rodataAsm.emitByte(0); // 4KB heap
            base_sym.size = 4096;
            symbols.push_back(base_sym);

            SymbolInfo ptr_sym;
            ptr_sym.name = "__heap_ptr";
            ptr_sym.sectionName = ".data";
            ptr_sym.value = rodataAsm.getCodeSize();
            ptr_sym.type = 1; // STT_OBJECT
            ptr_sym.binding = 1; // STB_GLOBAL
            rodataAsm.emitQWord(0); // Placeholder for __heap_base address
            ptr_sym.size = 8;
            symbols.push_back(ptr_sym);

            // Relocation for __heap_ptr = __heap_base
            RelocationInfo heap_reloc;
            heap_reloc.offset = ptr_sym.value;
            heap_reloc.symbolName = "__heap_base";
            heap_reloc.type = targetInfo->getDataRelocationType();
            heap_reloc.sectionName = ".data";
            heap_reloc.addend = 0;
            addRelocation(heap_reloc);
        }

        // Register global symbols
        for (auto& global : module.getGlobalVariables()) {
            // Align globals to 8 bytes
            while (rodataAsm.getCodeSize() % 8 != 0) rodataAsm.emitByte(0);

            SymbolInfo sym;
            sym.name = global->getName();
            sym.sectionName = ".data";
            sym.value = rodataAsm.getCodeSize();
            sym.type = 1; // STT_OBJECT
            sym.binding = 1; // STB_GLOBAL

            if (auto* init = global->getInitializer()) {
                if (auto* str = dynamic_cast<ir::ConstantString*>(init)) {
                    for (char c : str->getValue()) rodataAsm.emitByte(c);
                    rodataAsm.emitByte(0);
                } else if (auto* arr = dynamic_cast<ir::ConstantArray*>(init)) {
                    for (auto* elem : arr->getValues()) {
                        if (auto* ci = dynamic_cast<ir::ConstantInt*>(elem)) {
                            auto* intTy = static_cast<const ir::IntegerType*>(ci->getType());
                            if (intTy->getBitwidth() == 8) rodataAsm.emitByte(ci->getValue());
                            else if (intTy->getBitwidth() == 16) rodataAsm.emitWord(ci->getValue());
                            else if (intTy->getBitwidth() == 32) rodataAsm.emitDWord(ci->getValue());
                            else if (intTy->getBitwidth() == 64) rodataAsm.emitQWord(ci->getValue());
                        }
                    }
                } else if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    auto* intTy = static_cast<const ir::IntegerType*>(ci->getType());
                    if (intTy->getBitwidth() == 8) rodataAsm.emitByte(ci->getValue());
                    else if (intTy->getBitwidth() == 16) rodataAsm.emitWord(ci->getValue());
                    else if (intTy->getBitwidth() == 32) rodataAsm.emitDWord(ci->getValue());
                    else if (intTy->getBitwidth() == 64) rodataAsm.emitQWord(ci->getValue());
                }
            }
            sym.size = rodataAsm.getCodeSize() - sym.value;
            symbols.push_back(sym);
        }

        for (auto const& [konst, label] : floatConstantLabels) {
            (void)label;
            if (konst->getType()->isFloatTy()) {
                float f = konst->getValue();
                uint32_t bval;
                std::memcpy(&bval, &f, sizeof(float));
                rodataAsm.emitDWord(bval);
            } else {
                double d = konst->getValue();
                uint64_t bval;
                std::memcpy(&bval, &d, sizeof(double));
                rodataAsm.emitQWord(bval);
            }
        }
        emittedDataSection = true;
    }
}

void CodeGen::emitTextSection() {
    if (targetInfo->getName() == "wasm32") return;
    if (!emittedTextSection && os) {
        *os << ".text\n";
        *os << ".globl main\n"; // Entry point
        emittedTextSection = true;
    } else if (!emittedTextSection) {
        // Alignment for .text section in binary mode
        while (assembler->getCodeSize() % 16 != 0) assembler->emitByte(0x90);
        emittedTextSection = true;
    }
}

void CodeGen::emitBasicBlockContent(ir::BasicBlock& bb) {
    // Emit basic block content for WASM structured control flow
    for (auto& instr : bb.getInstructions()) {
        emitInstruction(*instr);
    }
}


void CodeGen::emitFunctionAlignment() {
    if (os) *os << ".align 16\n"; // Align functions to 16-byte boundaries
}

int32_t CodeGen::getStackOffset(ir::Value* val) const {
    return targetInfo->getStackOffset(*this, val);
}

} // namespace codegen