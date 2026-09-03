#include "codegen/CodeGen.h"
#include "codegen/asm/Assembler.h"
#include "ir/Constant.h"
#include "ir/GlobalValue.h"
#include "ir/Function.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Module.h"
#include "codegen/debug/DWARFGenerator.h"
#include "codegen/optimize/InstructionFusion.h"
#include <algorithm>
#include "target/core/TargetResolver.h"
#include "target/core/TargetDescriptor.h"
#include <cstring>
#include <iostream>
#include <sstream>

namespace codegen {
namespace {
std::unique_ptr<target::TargetInfo> createTargetInfoForName(const std::string& targetName) {
    auto desc = ::target::TargetDescriptor::fromString(targetName);
    if (!desc) {
        // Fallback for simple names
        std::string n = targetName;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        ::target::TargetDescriptor d;
        d.artifact = std::nullopt;

        if (n == "linux" || n == "systemv" || n == "x86_64" || n == "x64") {
            d.arch = ::target::Arch::X64; d.os = ::target::OS::Linux;
        } else if (n == "windows" || n == "win64" || n == "windows-amd64" || n == "windows-x64") {
            d.arch = ::target::Arch::X64; d.os = ::target::OS::Windows;
        } else if (n == "windows-arm64") {
            d.arch = ::target::Arch::AArch64; d.os = ::target::OS::Windows;
        } else if (n == "macos" || n == "darwin" || n == "macos-x64") {
            d.arch = ::target::Arch::X64; d.os = ::target::OS::MacOS;
        } else if (n == "macos-aarch64" || n == "macos-arm64") {
            d.arch = ::target::Arch::AArch64; d.os = ::target::OS::MacOS;
        } else if (n == "wasm32") {
            d.arch = ::target::Arch::WASM32; d.os = ::target::OS::WASI;
        } else if (n == "aarch64") {
            d.arch = ::target::Arch::AArch64; d.os = ::target::OS::Linux;
        } else if (n == "riscv64") {
            d.arch = ::target::Arch::RISCV64; d.os = ::target::OS::Linux;
        } else {
            return nullptr;
        }
        desc = d;
    }
    return target::TargetResolver::resolve(*desc);
}
}

std::unique_ptr<target::TargetInfo> CodeGenFactory::createTargetInfo(const std::string& targetName) {
    return createTargetInfoForName(targetName);
}

CodeGen::CodeGen(ir::Module& module, std::unique_ptr<target::TargetInfo> ti, std::ostream* os)
    : module(module), targetInfo(std::move(ti)),
      assembler(std::make_unique<asm_::Assembler>()),
      rodataAssembler(std::make_unique<asm_::Assembler>()), os(os),
      fusionCoordinator(std::make_unique<target::FusionCoordinator>()),
      debugInfoManager(std::make_unique<debug::DebugInfoManager>()),
      validator_(std::make_unique<validation::ASMValidator>()),
      objectGenerator_(std::make_unique<objectgen::ObjectFileGenerator>()) {
    initializeInstructionPatterns();
    auto& intRegs = targetInfo->getRegisters(target::RegisterClass::Integer);
    availableRegisters.insert(availableRegisters.end(), intRegs.begin(), intRegs.end());
    for (const auto& reg : availableRegisters) registerUsage[reg] = false;
    fusionCoordinator->setTargetArchitecture(targetInfo->getName());
}

CodeGen::~CodeGen() = default;

void CodeGen::emit(bool forExecutable) {
    if (forExecutable && !os && !rodataAssembler) {
        rodataAssembler = std::make_unique<asm_::Assembler>();
    }
    usesHeap = false; usesFPNeg = false;
    for (auto& func : module.getFunctions()) {
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                if (instr->getOpcode() == ir::Instruction::Alloc) usesHeap = true;
                if (instr->getOpcode() == ir::Instruction::Neg && instr->getType() && instr->getType()->isFloatingPoint()) usesFPNeg = true;
            }
        }
    }

    emitTargetSpecificHeader(); emitDataSection(); emitTextSection();
    if (forExecutable) targetInfo->emitStartFunction(*this);
    for (auto& func : module.getFunctions()) emitFunction(*func);
    targetInfo->emitFooter(*this);
    emitDebugInfo();
}

void CodeGen::emitFunction(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return;
    currentFunction = &func;
    stackOffsets.clear();
    lastStoreOp = "";
    liveness.run(func);
    if (targetInfo->getName() == "wasm32" && !os) {
        auto funcBodyAsm = std::make_unique<asm_::Assembler>();
        auto oldAsm = std::move(assembler); assembler = std::move(funcBodyAsm);
        for (auto& bb : func.getBasicBlocks()) emitBasicBlock(*bb);
        wasmFunctionBodies.push_back(assembler->getCode());
        assembler = std::move(oldAsm);
    } else {
        if (debugInfoManager->isDebugEnabled() && os) {
            debugInfoManager->beforeFunctionEmission(*this, *os, func);
        }
        if (os) {
            *os << "\n.globl " << func.getName() << "\n" << func.getName() << ":\n";
        } else if (assembler) {
            SymbolInfo func_sym;
            func_sym.name = func.getName();
            func_sym.sectionName = ".text";
            func_sym.value = assembler->getCodeSize();
            func_sym.type = 2; // STT_FUNC
            func_sym.binding = 1; // STB_GLOBAL
            addSymbol(func_sym);
        }
        targetInfo->emitFunctionPrologue(*this, func);
        for (auto& bb : func.getBasicBlocks()) emitBasicBlock(*bb);
        targetInfo->emitFunctionEpilogue(*this, func);
        if (os) {
            *os << ".Lfunc_end_" << func.getName() << ":\n";
        } else if (assembler) {
            SymbolInfo end_sym;
            end_sym.name = ".Lfunc_end_" + func.getName();
            end_sym.sectionName = ".text";
            end_sym.value = assembler->getCodeSize();
            end_sym.type = 0;
            end_sym.binding = 0; // Local
            addSymbol(end_sym);
        }
    }
}

void CodeGen::emitBasicBlock(ir::BasicBlock& bb) {
    lastStoreOp = "";
    if (os) {
        *os << bb.getParent()->getName() << "_" << bb.getName() << ":\n";
    } else if (assembler) {
        SymbolInfo bb_sym;
        bb_sym.name = bb.getParent()->getName() + "_" + bb.getName();
        bb_sym.sectionName = ".text";
        bb_sym.value = assembler->getCodeSize();
        bb_sym.type = 0; // STT_NOTYPE
        bb_sym.binding = 0; // STB_LOCAL
        addSymbol(bb_sym);
    }

    auto isCompareOpcode = [](ir::Instruction::Opcode op) {
        switch (op) {
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
            case ir::Instruction::Co:
            case ir::Instruction::Cuo:
                return true;
            default:
                return false;
        }
    };

    auto& instructions = bb.getInstructions();
    for (auto it = instructions.begin(); it != instructions.end(); ++it) {
        ir::Instruction* current = it->get();
        auto next = std::next(it);

        if (next != instructions.end()) {
            ir::Instruction* nextInst = next->get();
            const bool isBranch = (nextInst->getOpcode() == ir::Instruction::Br || nextInst->getOpcode() == ir::Instruction::Jnz);
            const bool compareFeedsBranch =
                isCompareOpcode(current->getOpcode()) &&
                isBranch &&
                !nextInst->getOperands().empty() &&
                nextInst->getOperands()[0]->get() == current;

            if (compareFeedsBranch && targetInfo->emitCmpAndBranchFusion(*this, *current, *nextInst)) {
                ++it; // Skip branch; it was emitted by fusion hook.
                continue;
            }

            const bool isMulAdd = (current->getOpcode() == ir::Instruction::Mul) &&
                                  (nextInst->getOpcode() == ir::Instruction::Add) &&
                                  !nextInst->getOperands().empty() &&
                                  (nextInst->getOperands()[0]->get() == current ||
                                   (nextInst->getOperands().size() > 1 && nextInst->getOperands()[1]->get() == current));

            if (isMulAdd && targetInfo->emitMulAddFusion(*this, *current, *nextInst)) {
                ++it; // Advance past Add
                continue; // Skip emitInstruction for current (Mul) as well!
            }

            const bool isCallFollowedByRet = (current->getOpcode() == ir::Instruction::Call) &&
                                              (nextInst->getOpcode() == ir::Instruction::Ret);

            if (isCallFollowedByRet && targetInfo->emitTailCall(*this, *current, *nextInst)) {
                ++it; // Consume Ret
                continue; // Skip standard emitInstruction for Call
            }
        }

        emitInstruction(*current);
    }
}

void CodeGen::emitInstruction(ir::Instruction& instr) {
    if (debugInfoManager->isDebugEnabled()) {
        uint64_t address = os ? 0 : assembler->getCodeSize();
        debugInfoManager->beforeInstructionEmission(*this, os ? *os : std::cerr, instr, address);
        if (os) debugInfoManager->emitInstructionDebugInfo(*this, *os, instr, address);
    }
    switch (instr.getOpcode()) {
        case ir::Instruction::Ret: targetInfo->emitRet(*this, instr); break;
        case ir::Instruction::Add: targetInfo->emitAdd(*this, instr); break;
        case ir::Instruction::Sub: targetInfo->emitSub(*this, instr); break;
        case ir::Instruction::Mul: targetInfo->emitMul(*this, instr); break;
        case ir::Instruction::Smulh: targetInfo->emitSmulh(*this, instr); break;
        case ir::Instruction::Umulh: targetInfo->emitUmulh(*this, instr); break;
        case ir::Instruction::Div: case ir::Instruction::Udiv: targetInfo->emitDiv(*this, instr); break;
        case ir::Instruction::Rem: case ir::Instruction::Urem: targetInfo->emitRem(*this, instr); break;
        case ir::Instruction::And: targetInfo->emitAnd(*this, instr); break;
        case ir::Instruction::Or: targetInfo->emitOr(*this, instr); break;
        case ir::Instruction::Xor: targetInfo->emitXor(*this, instr); break;
        case ir::Instruction::Shl: targetInfo->emitShl(*this, instr); break;
        case ir::Instruction::Shr: targetInfo->emitShr(*this, instr); break;
        case ir::Instruction::Sar: targetInfo->emitSar(*this, instr); break;
        case ir::Instruction::FAdd: targetInfo->emitFAdd(*this, instr); break;
        case ir::Instruction::FSub: targetInfo->emitFSub(*this, instr); break;
        case ir::Instruction::FMul: targetInfo->emitFMul(*this, instr); break;
        case ir::Instruction::FDiv: targetInfo->emitFDiv(*this, instr); break;
        case ir::Instruction::Neg: targetInfo->emitNeg(*this, instr); break;
        case ir::Instruction::Not: targetInfo->emitNot(*this, instr); break;
        case ir::Instruction::Copy: targetInfo->emitCopy(*this, instr); break;
        case ir::Instruction::Syscall: targetInfo->emitSyscall(*this, instr); break;
        case ir::Instruction::ExternCall: targetInfo->emitExternCall(*this, instr); break;
        case ir::Instruction::Call: targetInfo->emitCall(*this, instr); break;
        case ir::Instruction::Jmp: targetInfo->emitJmp(*this, instr); break;
        case ir::Instruction::Br: case ir::Instruction::Jnz: targetInfo->emitBr(*this, instr); break;
        case ir::Instruction::Load: case ir::Instruction::Loadd: case ir::Instruction::Loads: case ir::Instruction::Loadl: case ir::Instruction::Loaduw: case ir::Instruction::Loadsh: case ir::Instruction::Loaduh: case ir::Instruction::Loadsb: case ir::Instruction::Loadub: targetInfo->emitLoad(*this, instr); break;
        case ir::Instruction::Store: case ir::Instruction::Stored: case ir::Instruction::Stores: case ir::Instruction::Storel: case ir::Instruction::Storeh: case ir::Instruction::Storeb: targetInfo->emitStore(*this, instr); break;
        case ir::Instruction::Alloc: case ir::Instruction::Alloc4: case ir::Instruction::Alloc16: targetInfo->emitAlloc(*this, instr); break;
        case ir::Instruction::Ceq: case ir::Instruction::Cne: case ir::Instruction::Cslt:
        case ir::Instruction::Csle: case ir::Instruction::Csgt: case ir::Instruction::Csge:
        case ir::Instruction::Cult: case ir::Instruction::Cule: case ir::Instruction::Cugt:
        case ir::Instruction::Cuge: case ir::Instruction::Ceqf: case ir::Instruction::Cnef:
        case ir::Instruction::Clt: case ir::Instruction::Cle: case ir::Instruction::Cgt:
        case ir::Instruction::Cge: targetInfo->emitCmp(*this, instr); break;
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
            targetInfo->emitCast(*this, instr, instr.getOperands()[0]->get()->getType(), instr.getType());
            break;
        default: break;
    }
}

std::string CodeGen::getValueAsOperand(const ir::Value* value) {
    if (!value) return targetInfo->getImmediatePrefix() + "0";
    if (auto* ci = dynamic_cast<const ir::ConstantInt*>(value)) return targetInfo->formatConstant(ci);
    if (auto* cfp = dynamic_cast<const ir::ConstantFP*>(value)) return targetInfo->formatConstant(cfp);

    // Handle physical registers and stack slots assigned by Register Allocator
    if (value->hasPhysicalRegister()) {
        auto regClass = value->getType()->isFloatingPoint() ? target::RegisterClass::Float : target::RegisterClass::Integer;
        auto& regs = targetInfo->getRegisters(regClass);
        if (static_cast<size_t>(value->getPhysicalRegister()) < regs.size()) {
            return targetInfo->getRegisterName(regs[value->getPhysicalRegister()], value->getType());
        }
    }
    if (currentFunction && currentFunction->hasStackSlot(value)) {
        return targetInfo->formatStackOperand(targetInfo->getStackOffset(*this, const_cast<ir::Value*>(value)));
    }

    if (stackOffsets.count(const_cast<ir::Value*>(value)))
        return targetInfo->formatStackOperand(stackOffsets.at(const_cast<ir::Value*>(value)));

    if (auto* param = dynamic_cast<const ir::Parameter*>(value)) {
        if (currentFunction) {
            const auto& params = currentFunction->getParameters();
            size_t idx = 0;
            for (auto& p : params) {
                if (p.get() == param) break;
                idx++;
            }
            if (idx < 6 && targetInfo) {
                const auto& argRegs = targetInfo->getIntegerArgumentRegisters();
                if (idx < argRegs.size()) {
                    return targetInfo->getRegisterName(argRegs[idx], param->getType());
                }
            } else if (targetInfo) {
                int32_t stackOff = 16 + (idx - 6) * 8;
                return targetInfo->formatStackOperand(stackOff);
            }
        }
    }
    if (auto* bb = dynamic_cast<const ir::BasicBlock*>(value)) return bb->getParent()->getName() + "_" + bb->getName();
    if (auto* gv = dynamic_cast<const ir::GlobalVariable*>(value)) return targetInfo->formatGlobalOperand(gv->getName());
    if (auto* f = dynamic_cast<const ir::Function*>(value)) return f->getName();

    return "$" + value->getName();
}

int32_t CodeGen::getStackOffset(ir::Value* val) const { return targetInfo->getStackOffset(*this, val); }

CodeGen::CompilationResult CodeGen::compileToObject(const std::string& outputPrefix, bool validateASM, bool generateObject, bool) {
    CompilationTimer timer; CompilationResult result; result.targetName = targetInfo->getName();
    std::stringstream ss; std::ostream* old_os = os; os = &ss; emit(false); os = old_os;
    std::string assembly = ss.str();
    std::string assemblyPath = outputPrefix + targetInfo->getAssemblyFileExtension();
    result.assemblyPath = writeAssemblyToFile(assembly, assemblyPath);
    if (validateASM && validator_) result.validation = validator_->validateAssembly(assembly, targetInfo->getName());
    if (generateObject && !result.hasValidationErrors()) {
        result.objGen = objectGenerator_->generateObject(result.assemblyPath, outputPrefix, targetInfo->getName());
        if (result.objGen.success) result.objectPath = result.objGen.objectPath;
    }
    result.success = !result.hasValidationErrors() && (!generateObject || result.objGen.success);
    result.totalTimeMs = timer.getElapsedMs(); return result;
}

CodeGen::CompilationResult CodeGen::compileToAssembly(const std::string& outputPath, bool validateASM) {
    CompilationTimer timer; CompilationResult result; result.targetName = targetInfo->getName();
    std::stringstream ss; std::ostream* old_os = os; os = &ss; emit(false); os = old_os;
    std::string assembly = ss.str();
    result.assemblyPath = writeAssemblyToFile(assembly, outputPath);
    if (validateASM && validator_) result.validation = validator_->validateAssembly(assembly, targetInfo->getName());
    result.success = !result.hasValidationErrors(); result.totalTimeMs = timer.getElapsedMs(); return result;
}

std::string CodeGen::writeAssemblyToFile(const std::string& assembly, const std::string& path) {
    std::ofstream file(path); if (!file.is_open()) return ""; file << assembly; file.close(); return path;
}

bool CodeGen::CompilationResult::hasObjectGenerationErrors() const { return !objGen.success; }
std::vector<std::string> CodeGen::CompilationResult::getAllErrors() const {
    std::vector<std::string> errors;
    for (const auto& error : validation.errors) errors.push_back("[Validation] " + error.message);
    if (!objGen.success) errors.push_back("[ObjectGen] " + objGen.errorOutput);
    return errors;
}

void CodeGen::setValidationLevel(validation::ValidationLevel level) { if (validator_) validator_->setValidationLevel(level); }
void CodeGen::enableVerboseOutput(bool enable) { verboseOutput_ = enable; if (validator_) validator_->enableDetailedReporting(enable); }

void CodeGen::initializeInstructionPatterns() {}
void CodeGen::performTargetOptimizations(ir::Function&) {}
std::string CodeGen::allocateRegister(ir::Value*) { return ""; }
void CodeGen::deallocateRegister(const std::string&) {}
void CodeGen::emitTargetSpecificHeader() {
    targetInfo->emitHeader(*this);
    if (os) {
        if (targetInfo->getName().find("windows") != std::string::npos || targetInfo->getName().find("win64") != std::string::npos)
            *os << ".intel_syntax noprefix\n";
        else if (targetInfo->getName() == "x86_64")
            *os << ".att_syntax prefix\n";
    }
}
void CodeGen::emitDataSection() {
    if (module.getGlobalVariables().empty() && !usesHeap) return;
    if (os) {
        *os << "\n.data\n";
        if (usesHeap) {
            *os << ".align 16\n__heap_base:\n  .zero 67108864\n";
            *os << ".align 8\n__heap_ptr:\n  .quad __heap_base\n";
        }
        for (auto& gv : module.getGlobalVariables()) {
            size_t align = gv->getType()->getSize();
            if (align >= 8) align = 8;
            else if (align >= 4) align = 4;
            else if (align >= 2) align = 2;
            else align = 1;
            *os << ".align " << align << "\n";
            *os << gv->getName() << ":\n";
            if (auto* init = gv->getInitializer()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    if (ci->getType()->getSize() == 1) *os << "  .byte " << ci->getValue() << "\n";
                    else if (ci->getType()->getSize() == 2) *os << "  .short " << ci->getValue() << "\n";
                    else if (ci->getType()->getSize() == 4) *os << "  .long " << ci->getValue() << "\n";
                    else *os << "  .quad " << ci->getValue() << "\n";
                } else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(init)) {
                    uint64_t bits = 0;
                    double val = cfp->getValue();
                    std::memcpy(&bits, &val, sizeof(double));
                    if (cfp->getType()->getSize() == 4) *os << "  .long " << (uint32_t)bits << "\n";
                    else *os << "  .quad " << bits << "\n";
                } else if (auto* cs = dynamic_cast<ir::ConstantString*>(init)) {
                    *os << "  .string \"" << cs->getValue() << "\"\n";
                } else if (auto* ca = dynamic_cast<ir::ConstantArray*>(init)) {
                    for (auto* elem : ca->getValues()) {
                        if (auto* eci = dynamic_cast<ir::ConstantInt*>(elem)) {
                             if (eci->getType()->getSize() == 1) *os << "  .byte " << eci->getValue() << "\n";
                             else if (eci->getType()->getSize() == 4) *os << "  .long " << eci->getValue() << "\n";
                             else *os << "  .quad " << eci->getValue() << "\n";
                        } else if (auto* ecfp = dynamic_cast<ir::ConstantFP*>(elem)) {
                             uint64_t bits = 0;
                             double val = ecfp->getValue();
                             std::memcpy(&bits, &val, sizeof(double));
                             if (ecfp->getType()->getSize() == 4) *os << "  .long " << (uint32_t)bits << "\n";
                             else *os << "  .quad " << bits << "\n";
                        } else if (auto* ecs = dynamic_cast<ir::ConstantString*>(elem)) {
                             *os << "  .string \"" << ecs->getValue() << "\"\n";
                        }
                    }
                }
            } else {
                *os << "  .zero " << gv->getType()->getSize() << "\n";
            }
        }
    } else if (rodataAssembler) {
        if (usesHeap) {
            // Place __fyra_heap in .bss (NOBITS) so zero bytes are allocated by loader, not written to ELF file
            SymbolInfo hb_sym;
            hb_sym.name = "__fyra_heap";
            hb_sym.sectionName = ".bss";
            hb_sym.value = 0;
            hb_sym.size = 67108864;
            hb_sym.binding = 1;
            addSymbol(hb_sym);

            uint64_t heap_ptr_offset = rodataAssembler->getCodeSize();
            while (heap_ptr_offset % 8 != 0) {
                rodataAssembler->emitByte(0);
                heap_ptr_offset = rodataAssembler->getCodeSize();
            }
            SymbolInfo hp_sym;
            hp_sym.name = "heap_ptr";
            hp_sym.sectionName = ".data";
            hp_sym.value = heap_ptr_offset;
            hp_sym.size = 8;
            hp_sym.binding = 1;
            addSymbol(hp_sym);
            
            rodataAssembler->emitQWord(0);
            RelocationInfo hp_reloc;
            hp_reloc.offset = heap_ptr_offset;
            hp_reloc.symbolName = "__fyra_heap";
            hp_reloc.type = "R_X86_64_64";
            hp_reloc.sectionName = ".data";
            hp_reloc.addend = 0;
            addRelocation(hp_reloc);
        }
        for (auto& gv : module.getGlobalVariables()) {
            size_t align = gv->getType()->getSize();
            if (align >= 8) align = 8;
            else if (align >= 4) align = 4;
            else if (align >= 2) align = 2;
            else align = 1;
            uint64_t offset = rodataAssembler->getCodeSize();
            while (offset % align != 0) {
                rodataAssembler->emitByte(0);
                offset = rodataAssembler->getCodeSize();
            }
            symbols.push_back({gv->getName(), offset, gv->getType()->getSize(), 0, 1, ".data"});
            if (auto* init = gv->getInitializer()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    if (ci->getType()->getSize() == 1) rodataAssembler->emitByte(ci->getValue());
                    else if (ci->getType()->getSize() == 2) rodataAssembler->emitWord(ci->getValue());
                    else if (ci->getType()->getSize() == 4) rodataAssembler->emitDWord(ci->getValue());
                    else rodataAssembler->emitQWord(ci->getValue());
                } else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(init)) {
                    uint64_t bits = 0;
                    double val = cfp->getValue();
                    std::memcpy(&bits, &val, sizeof(double));
                    if (cfp->getType()->getSize() == 4) rodataAssembler->emitDWord((uint32_t)bits);
                    else rodataAssembler->emitQWord(bits);
                } else if (auto* cs = dynamic_cast<ir::ConstantString*>(init)) {
                    for (char c : cs->getValue()) {
                        rodataAssembler->emitByte(c);
                    }
                    rodataAssembler->emitByte(0); // Null terminator
                } else if (auto* ca = dynamic_cast<ir::ConstantArray*>(init)) {
                    for (auto* elem : ca->getValues()) {
                        if (auto* eci = dynamic_cast<ir::ConstantInt*>(elem)) {
                             if (eci->getType()->getSize() == 1) rodataAssembler->emitByte(eci->getValue());
                             else if (eci->getType()->getSize() == 4) rodataAssembler->emitDWord(eci->getValue());
                             else rodataAssembler->emitQWord(eci->getValue());
                        } else if (auto* ecfp = dynamic_cast<ir::ConstantFP*>(elem)) {
                             uint64_t bits = 0;
                             double val = ecfp->getValue();
                             std::memcpy(&bits, &val, sizeof(double));
                             if (ecfp->getType()->getSize() == 4) rodataAssembler->emitDWord((uint32_t)bits);
                             else rodataAssembler->emitQWord(bits);
                        } else if (auto* ecs = dynamic_cast<ir::ConstantString*>(elem)) {
                             for (char c : ecs->getValue()) {
                                 rodataAssembler->emitByte(c);
                             }
                             rodataAssembler->emitByte(0);
                        }
                    }
                }
            } else {
                for (size_t i = 0; i < gv->getType()->getSize(); ++i) rodataAssembler->emitByte(0);
            }
        }
    }
}
void CodeGen::emitTextSection() {
    if (os) {
        *os << ".text\n.globl main\n";
    } else if (assembler) {
        // No explicit .text needed in the byte buffer,
        // but we might want to reset symbol list if needed.
    }
}
void CodeGen::emitFunctionAlignment() {}
void CodeGen::performInstructionFusion(ir::Function&) {}
void CodeGen::applyFusionOptimizations(ir::BasicBlock&) {}
void CodeGen::enableDebugInfo(bool enable) { debugInfoManager->enableDebugInfo(enable); }
void CodeGen::setCurrentLocation(const std::string& file, unsigned line) { debugInfoManager->setCurrentLocation(file, line); }
void CodeGen::emitDebugInfo() { if (debugInfoManager->isDebugEnabled()) debugInfoManager->generateDebugInfo(*this, os ? *os : std::cerr, module); }
void CodeGen::selectInstruction(ir::Instruction&) {}
bool CodeGen::matchPattern(ir::Instruction&, const InstructionPattern&) { return false; }
void CodeGen::selectArithmeticInstruction(ir::Instruction&) {}
void CodeGen::selectMemoryInstruction(ir::Instruction&) {}
void CodeGen::selectComparisonInstruction(ir::Instruction&) {}
void CodeGen::selectControlFlowInstruction(ir::Instruction&) {}
void CodeGen::emitBasicBlockContent(ir::BasicBlock& bb) { for (auto& instr : bb.getInstructions()) emitInstruction(*instr); }
bool CodeGen::isImmediateValue(ir::Value* value, int64_t& imm) {
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(value)) {
        imm = static_cast<int64_t>(ci->getValue());
        return true;
    }
    return false;
}

bool CodeGen::canFoldIntoMemoryOperand(ir::Value* value) {
    if (!value) return false;
    if (auto* gv = dynamic_cast<ir::GlobalValue*>(value)) return true;
    if (auto* gvar = dynamic_cast<ir::GlobalVariable*>(value)) return true;

    if (stackOffsets.count(value)) {
        const int32_t off = stackOffsets[value];
        return off >= -2048 && off <= 2047;
    }
    return false;
}

std::string CodeGen::getOptimizedOperand(ir::Value* value) {
    int64_t imm = 0;
    if (isImmediateValue(value, imm)) {
        return targetInfo->getImmediatePrefix() + std::to_string(imm);
    }
    return getValueAsOperand(value);
}
void CodeGen::emitWasmFunctionSignature(ir::Function&) {}
std::string CodeGen::getWasmValueAsOperand(const ir::Value*) { return ""; }

} // namespace codegen

namespace codegen {

CodeGenPipeline::CodeGenPipeline() {}

CodeGenPipeline::PipelineResult CodeGenPipeline::execute(ir::Module& module, const PipelineConfig& config) {
    PipelineResult result;
    for (const auto& t : config.targetPlatforms) {
        auto tr = executeForTarget(module, t, config);
        result.targetResults[t] = tr.targetResults[t];
    }
    result.success = true;
    return result;
}

CodeGenPipeline::PipelineResult CodeGenPipeline::executeForTarget(ir::Module& module, const std::string& t, const PipelineConfig& config) {
    PipelineResult result;
    auto targetInfo = createTargetInfoForName(t);
    if (!targetInfo) { result.success = false; return result; }
    auto cg = std::make_unique<CodeGen>(module, std::move(targetInfo));
    result.targetResults[t] = cg->compileToObject(config.outputPrefix + "_" + t, config.enableValidation, config.enableObjectGeneration);
    result.success = result.targetResults[t].success;
    return result;
}

} // namespace codegen
