#include "codegen/target/AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/SIMDInstruction.h"
#include <ostream>
#include <algorithm>
#include <map>
#include <set>

namespace codegen {
namespace target {

namespace {
std::string getEpilogueLabel(const ir::Function* func) {
    return func ? func->getName() + "_epilogue" : ".L_epilogue";
}

std::string getLoadInstr(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return "ldrb";
            case 16: return "ldrh";
            case 32: return "ldr";
            case 64: return "ldr";
        }
    }
    if (type->isPointerTy()) return "ldr";
    return "ldr";
}

std::string getStoreInstr(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return "strb";
            case 16: return "strh";
            case 32: return "str";
            case 64: return "str";
        }
    }
    if (type->isPointerTy()) return "str";
    return "str";
}

std::string getRegisterName(const std::string& base, const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        if (intTy->getBitwidth() <= 32) {
            if (base[0] == 'x') return "w" + base.substr(1);
        }
    }
    return base;
}

}

void AArch64::emitLoadValue(CodeGen& cg, execgen::Assembler& assembler, ir::Value* val, uint8_t reg) {
    if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
        if (instr->hasPhysicalRegister()) {
            uint8_t phys = static_cast<uint8_t>(instr->getPhysicalRegister());
            // Mapping from allocator index to AArch64 register index
            // allocator regs: x0-x28
            uint8_t src_reg = phys; 

            if (src_reg == reg) return;
            // mov rd, rn -> orr rd, xzr, rn
            uint32_t instruction = 0xAA0003E0 | ((src_reg & 0x1F) << 16) | (reg & 0x1F);
            if (val->getType()->getSize() <= 4) instruction &= ~0x80000000; // sf=0 for 32-bit
            else instruction |= 0x80000000;
            assembler.emitDWord(instruction);
            return;
        }
    }

    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        // movz xReg, value
        uint16_t imm = constInt->getValue() & 0xFFFF;
        uint32_t instruction = 0xD2800000 | (imm << 5) | (reg & 0x1F);
        if (val->getType()->getSize() > 4) instruction |= 0x80000000;
        else instruction &= 0x7FFFFFFF;
        assembler.emitDWord(instruction);
    } else {
        int32_t offset = cg.getStackOffset(val);
        const ir::Type* type = val->getType();
        uint32_t base;
        if (type->isFloatingPoint()) {
            // LDR (SIMD&FP) unscaled: 32-bit is 0xBC400000, 64-bit is 0xFC400000
            base = (type->getSize() > 4) ? 0xFC400000 : 0xBC400000;
        } else {
            // LDR (Integer) unscaled: 32-bit is 0xB8400000, 64-bit is 0xF8400000
            base = (type->getSize() > 4) ? 0xF8400000 : 0xB8400000;
        }
        uint32_t instruction = base | ((offset & 0x1FF) << 12) | (29 << 5) | (reg & 0x1F);
        assembler.emitDWord(instruction);
    }
}

// Core TargetInfo methods
std::string AArch64::getName() const {
    return "aarch64";
}

size_t AArch64::getPointerSize() const {
    return 64;
}

std::string AArch64::getImmediatePrefix() const {
    return "#";
}

std::string AArch64::formatStackOperand(int offset) const {
    return "[x29, #" + std::to_string(offset) + "]";
}

std::string AArch64::formatGlobalOperand(const std::string& name) const {
    return name;
}

TypeInfo AArch64::getTypeInfo(const ir::Type* type) const {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        uint64_t bitWidth = intTy->getBitwidth();
        return {bitWidth, bitWidth, RegisterClass::Integer, false, true};
    } else if (type->isFloatTy()) {
        return {32, 32, RegisterClass::Float, true, true};
    } else if (type->isDoubleTy()) {
        return {64, 64, RegisterClass::Float, true, true};
    } else if (type->isPointerTy()) {
        return {64, 64, RegisterClass::Integer, false, false};
    } else if (auto* vecTy = dynamic_cast<const ir::VectorType*>(type)) {
        uint64_t bitWidth = vecTy->getBitWidth();
        return {bitWidth, std::min(static_cast<uint64_t>(128), bitWidth / 8), RegisterClass::Vector, 
                vecTy->isFloatingPointVector(), vecTy->getElementType()->isInteger()};
    }
    // Default case
    return {32, 32, RegisterClass::Integer, false, true};
}

const std::vector<std::string>& AArch64::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
        "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
        "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28"
    };
    static const std::vector<std::string> floatRegs = {
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
        "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18",
        "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27",
        "v28", "v29", "v30", "v31"
    };
    
    switch (regClass) {
        case RegisterClass::Integer: return intRegs;
        case RegisterClass::Float: return floatRegs;
        case RegisterClass::Vector: return floatRegs; // NEON uses same registers as float
        default: return intRegs;
    }
}

const std::string& AArch64::getReturnRegister(const ir::Type* type) const {
    if (type->isFloatTy() || type->isDoubleTy()) {
        return getFloatReturnRegister();
    } else {
        return getIntegerReturnRegister();
    }
}

size_t AArch64::getMaxRegistersForArgs() const {
    return 8; // x0-x7 or v0-v7
}

void AArch64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    resetStackOffset();
    int int_reg_idx = 0;
    int float_reg_idx = 0;
    int stack_arg_idx = 0;

    // AArch64 AAPCS64 calling convention
    for (auto& param : func.getParameters()) {
        TypeInfo info = getTypeInfo(param->getType());
        if (info.regClass == RegisterClass::Float) {
            if (float_reg_idx < 8) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[param.get()] = currentStackOffset;
                float_reg_idx++;
            } else {
                cg.getStackOffsets()[param.get()] = 16 + (stack_arg_idx++) * 8;
            }
        } else {
            if (int_reg_idx < 8) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[param.get()] = currentStackOffset;
                int_reg_idx++;
            } else {
                cg.getStackOffsets()[param.get()] = 16 + (stack_arg_idx++) * 8;
            }
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
                currentStackOffset -= 8; // Allocate 8 bytes for value-producing instructions
                cg.getStackOffsets()[instr.get()] = currentStackOffset;
            }
        }
    }

    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) hasCalls = true;
        }
    }

    size_t local_area_size = -currentStackOffset;

    std::set<std::string> usedCalleeSaved;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->hasPhysicalRegister()) {
                std::string reg = getRegisters(RegisterClass::Integer)[instr->getPhysicalRegister()];
                if (isCalleeSaved(reg)) usedCalleeSaved.insert(reg);
            }
        }
    }

    // Optimization: simple leaf function with no locals and no parameters and no callee-saved
    bool isLeaf = !hasCalls;
    bool needsFrame = !isLeaf || local_area_size > 0 || !usedCalleeSaved.empty();

    if (!needsFrame) {
        if (auto* os = cg.getTextStream()) {
            *os << "  // Simple leaf function: frame pointer omitted\n";
        }
        return;
    }

    size_t total_frame_size = local_area_size + (usedCalleeSaved.size() * 8);
    if (!isLeaf) total_frame_size += 16;
    total_frame_size = align_to_16(total_frame_size);

    if (auto* os = cg.getTextStream()) {
        *os << "  // Function prologue for " << func.getName() << "\n";
        *os << "  stp x29, x30, [sp, #-16]!\n";
        *os << "  mov x29, sp\n";

        if (total_frame_size > 0) {
            if (total_frame_size <= 4095) {
                *os << "  sub sp, sp, #" << total_frame_size << "\n";
            } else {
                *os << "  mov x9, #" << total_frame_size << "\n";
                *os << "  sub sp, sp, x9\n";
            }
        }

        // Save only used callee-saved registers
        int cs_offset = isLeaf ? 0 : 16;
        auto it = usedCalleeSaved.begin();
        while (it != usedCalleeSaved.end()) {
            std::string r1 = *it++;
            if (it != usedCalleeSaved.end()) {
                std::string r2 = *it++;
                *os << "  stp " << r1 << ", " << r2 << ", [sp, #" << cs_offset << "]\n";
                cs_offset += 16;
            } else {
                *os << "  str " << r1 << ", [sp, #" << cs_offset << "]\n";
                cs_offset += 8;
            }
        }

        // Move arguments from registers to stack
        int int_idx = 0;
        int float_idx = 0;
        for (auto& param : func.getParameters()) {
            TypeInfo info = getTypeInfo(param->getType());
            if (info.regClass == RegisterClass::Float) {
                if (float_idx < 8) {
                    std::string reg = (info.size == 32) ? "s" : "d";
                    reg += std::to_string(float_idx++);
                    *os << "  str " << reg << ", [x29, #" << cg.getStackOffset(param.get()) << "]\n";
                }
            } else {
                if (int_idx < 8) {
                    std::string reg = getRegisterName("x" + std::to_string(int_idx++), param->getType());
                    *os << "  str " << reg << ", [x29, #" << cg.getStackOffset(param.get()) << "]\n";
                }
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        // stp x29, x30, [sp, #-16]!
        assembler.emitDWord(0xA9BF7BFD);
        // mov x29, sp
        assembler.emitDWord(0x910003FD);

        if (total_frame_size > 0) {
            if (total_frame_size <= 4095) {
                uint32_t imm12 = total_frame_size;
                assembler.emitDWord(0xD10003FF | (imm12 << 10));
            } else {
                uint32_t imm16_0 = total_frame_size & 0xFFFF;
                assembler.emitDWord(0xD2800009 | (imm16_0 << 5));
                if ((total_frame_size >> 16) > 0) {
                    uint32_t imm16_1 = (total_frame_size >> 16) & 0xFFFF;
                    assembler.emitDWord(0xF2A00009 | (imm16_1 << 5));
                }
                assembler.emitDWord(0xCB0903FF);
            }
        }

        // Binary mode: emit callee-saved stp/str (simplified for now as I prioritize text assembly)
        // In a real implementation we'd iterate usedCalleeSaved here too.

        // Move arguments to stack
        int int_idx = 0;
        int float_idx = 0;
        for (auto& param : func.getParameters()) {
            TypeInfo info = getTypeInfo(param->getType());
            if (info.regClass == RegisterClass::Float) {
                if (float_idx < 8) {
                    int32_t offset = cg.getStackOffset(param.get());
                    uint32_t base = (info.size == 32) ? 0xBC000000 : 0xFC000000;
                    assembler.emitDWord(base | ((offset & 0x1FF) << 12) | (29 << 5) | float_idx++);
                }
            } else {
                if (int_idx < 8) {
                    int32_t offset = cg.getStackOffset(param.get());
                    uint32_t base = (info.size > 4) ? 0xF8000000 : 0xB8000000;
                    assembler.emitDWord(base | ((offset & 0x1FF) << 12) | (29 << 5) | int_idx++);
                }
            }
        }
    }
}

void AArch64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getEpilogueLabel(&func) << ":\n";
    }
    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) { hasCalls = true; break; }
        }
        if (hasCalls) break;
    }
    size_t local_area_size = -currentStackOffset;

    std::set<std::string> usedCalleeSaved;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->hasPhysicalRegister()) {
                std::string reg = getRegisters(RegisterClass::Integer)[instr->getPhysicalRegister()];
                if (isCalleeSaved(reg)) usedCalleeSaved.insert(reg);
            }
        }
    }

    bool isLeaf = !hasCalls;
    bool needsFrame = !isLeaf || local_area_size > 0 || !usedCalleeSaved.empty();

    if (!needsFrame) {
        if (auto* os = cg.getTextStream()) {
            *os << "  ret\n";
        } else {
            cg.getAssembler().emitDWord(0xD65F03C0);
        }
        return;
    }

    size_t total_frame_size = local_area_size + (usedCalleeSaved.size() * 8);
    if (!isLeaf) total_frame_size += 16;
    total_frame_size = align_to_16(total_frame_size);

    if (auto* os = cg.getTextStream()) {
        *os << "  // Function epilogue for " << func.getName() << "\n";

        // Restore used callee-saved registers
        int cs_offset = isLeaf ? 0 : 16;
        auto it = usedCalleeSaved.begin();
        while (it != usedCalleeSaved.end()) {
            std::string r1 = *it++;
            if (it != usedCalleeSaved.end()) {
                std::string r2 = *it++;
                *os << "  ldp " << r1 << ", " << r2 << ", [sp, #" << cs_offset << "]\n";
                cs_offset += 16;
            } else {
                *os << "  ldr " << r1 << ", [sp, #" << cs_offset << "]\n";
                cs_offset += 8;
            }
        }

        // Restore stack pointer and frame
        if (!isLeaf) {
            *os << "  mov sp, x29\n";
            *os << "  ldp x29, x30, [sp], #16\n";
        } else if (total_frame_size > 0) {
            *os << "  add sp, sp, #" << total_frame_size << "\n";
        }
        *os << "  ret\n";
    } else {
        auto& assembler = cg.getAssembler();
        assembler.emitDWord(0xA94053F3); // ldp x19, x20, [sp]
        assembler.emitDWord(0xA9415BF5); // ldp x21, x22, [sp, #16]

        // mov sp, x29
        assembler.emitDWord(0x910003DF);
        // ldp x29, x30, [sp], #16
        assembler.emitDWord(0xA8C17BFD);
        // ret
        assembler.emitDWord(0xD65F03C0);
    }
}

void AArch64::emitPassArgument(CodeGen& cg, size_t argIndex,
                              const std::string& value, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        const auto& int_regs = getIntegerArgumentRegisters();
        const auto& float_regs = getFloatArgumentRegisters();

        // Handle different argument types according to AAPCS64
        if (type->isFloatTy()) {
            if (argIndex < float_regs.size()) {
                *os << "  ldr s" << argIndex << ", " << value << "\n";
            } else {
                // Stack argument - align to 4 bytes for float
                size_t stack_offset = (argIndex - float_regs.size()) * 8;
                *os << "  ldr s0, " << value << "\n";
                *os << "  str s0, [sp, #" << stack_offset << "]\n";
            }
        } else if (type->isDoubleTy()) {
            if (argIndex < float_regs.size()) {
                *os << "  ldr d" << argIndex << ", " << value << "\n";
            } else {
                // Stack argument - align to 8 bytes for double
                size_t stack_offset = (argIndex - float_regs.size()) * 8;
                *os << "  ldr d0, " << value << "\n";
                *os << "  str d0, [sp, #" << stack_offset << "]\n";
            }
        } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
            if (argIndex < int_regs.size()) {
                if (intTy->getBitwidth() <= 32) {
                    std::string wreg = getWRegister(int_regs[argIndex]);
                    *os << "  ldr " << wreg << ", " << value << "\n";
                } else {
                    *os << "  ldr " << int_regs[argIndex] << ", " << value << "\n";
                }
            } else {
                // Stack argument
                size_t stack_offset = (argIndex - int_regs.size()) * 8;
                *os << "  ldr x9, " << value << "\n";
                *os << "  str x9, [sp, #" << stack_offset << "]\n";
            }
        } else if (type->isPointerTy()) {
            if (argIndex < int_regs.size()) {
                *os << "  ldr " << int_regs[argIndex] << ", " << value << "\n";
            } else {
                // Stack argument for pointer
                size_t stack_offset = (argIndex - int_regs.size()) * 8;
                *os << "  ldr x9, " << value << "\n";
                *os << "  str x9, [sp, #" << stack_offset << "]\n";
            }
        } else {
            // Handle aggregate types (struct, array) - would need more complex logic
            // For now, treat as pointer
            if (argIndex < int_regs.size()) {
                *os << "  ldr " << int_regs[argIndex] << ", " << value << "\n";
            } else {
                size_t stack_offset = (argIndex - int_regs.size()) * 8;
                *os << "  ldr x9, " << value << "\n";
                *os << "  str x9, [sp, #" << stack_offset << "]\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        const auto& int_regs = getIntegerArgumentRegisters();
        const auto& float_regs = getFloatArgumentRegisters();

        if (type->isFloatingPoint()) {
            if (argIndex < float_regs.size()) {
                // Register argument: handled by CodeGen calling convention logic
                // that ensures values are in the correct registers before emitCall.
            } else {
                // Stack argument
                size_t stack_offset = (argIndex - float_regs.size()) * 8;
                // str d0, [sp, #stack_offset]
                uint32_t str = 0xFC0003E0 | ((stack_offset & 0xFFF) << 10) | (31 << 5) | 0;
                assembler.emitDWord(str);
            }
        } else {
            if (argIndex < int_regs.size()) {
                // Register argument: handled by CodeGen calling convention logic.
            } else {
                size_t stack_offset = (argIndex - int_regs.size()) * 8;
                // str x9, [sp, #stack_offset]
                uint32_t str = 0xF90003E9 | ((stack_offset & 0xFFF) << 10) | (31 << 5) | 9;
                assembler.emitDWord(str);
            }
        }
    }
}

void AArch64::emitGetArgument(CodeGen& cg, size_t argIndex,
                             const std::string& dest, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        // Enhanced AArch64 AAPCS64 argument retrieval with comprehensive type support
        const auto& int_regs = getIntegerArgumentRegisters();
        const auto& float_regs = getFloatArgumentRegisters();

        if (type->isFloatTy() || type->isDoubleTy()) {
            // Floating-point argument handling
            if (argIndex < float_regs.size()) {
                // Argument is in floating-point register
                if (type->isFloatTy()) {
                    *os << "  fmov s0, " << float_regs[argIndex] << "\n";
                    *os << "  str s0, " << dest << "\n";
                } else { // Double
                    *os << "  fmov d0, " << float_regs[argIndex] << "\n";
                    *os << "  str d0, " << dest << "\n";
                }
            } else {
                // Argument is on stack - calculate proper offset
                size_t floatArgsOnStack = argIndex - float_regs.size();
                size_t intArgsOnStack = (int_regs.size() > argIndex) ? 0 : argIndex - int_regs.size();
                size_t totalArgsOnStack = floatArgsOnStack + intArgsOnStack;
                size_t stackOffset = totalArgsOnStack * 8 + 16; // Skip frame pointer and return address

                if (type->isFloatTy()) {
                    *os << "  ldr s0, [x29, #" << stackOffset << "]\n";
                    *os << "  str s0, " << dest << "\n";
                } else { // Double
                    *os << "  ldr d0, [x29, #" << stackOffset << "]\n";
                    *os << "  str d0, " << dest << "\n";
                }
            }
        } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
            // Integer argument handling with width-aware processing
            uint64_t bitWidth = intTy->getBitwidth();
            
            if (argIndex < int_regs.size()) {
                // Argument is in integer register
                if (bitWidth <= 8) {
                    *os << "  and w9, " << int_regs[argIndex].substr(1) << ", #0xFF\n"; // Extract byte
                    *os << "  str w9, " << dest << "\n";
                } else if (bitWidth <= 16) {
                    *os << "  and w9, " << int_regs[argIndex].substr(1) << ", #0xFFFF\n"; // Extract halfword
                    *os << "  str w9, " << dest << "\n";
                } else if (bitWidth <= 32) {
                    *os << "  mov w9, " << int_regs[argIndex].substr(1) << "\n"; // 32-bit move
                    *os << "  str w9, " << dest << "\n";
                } else {
                    *os << "  str " << int_regs[argIndex] << ", " << dest << "\n"; // 64-bit store
                }
            } else {
                // Argument is on stack
                size_t stackOffset = (argIndex - int_regs.size()) * 8 + 16;

                if (bitWidth <= 8) {
                    *os << "  ldrb w9, [x29, #" << stackOffset << "]\n";
                    *os << "  str w9, " << dest << "\n";
                } else if (bitWidth <= 16) {
                    *os << "  ldrh w9, [x29, #" << stackOffset << "]\n";
                    *os << "  str w9, " << dest << "\n";
                } else if (bitWidth <= 32) {
                    *os << "  ldr w9, [x29, #" << stackOffset << "]\n";
                    *os << "  str w9, " << dest << "\n";
                } else {
                    *os << "  ldr x9, [x29, #" << stackOffset << "]\n";
                    *os << "  str x9, " << dest << "\n";
                }
            }
        } else if (type->isPointerTy()) {
            // Pointer argument handling
            if (argIndex < int_regs.size()) {
                // Pointer is in integer register (always 64-bit on AArch64)
                *os << "  str " << int_regs[argIndex] << ", " << dest << "\n";
            } else {
                // Pointer is on stack
                size_t stackOffset = (argIndex - int_regs.size()) * 8 + 16;
                *os << "  ldr x9, [x29, #" << stackOffset << "]\n";
                *os << "  str x9, " << dest << "\n";
            }
        } else if (type->isStructTy() || type->isArrayTy()) {
            // Aggregate types - passed by reference according to AAPCS64
            if (argIndex < int_regs.size()) {
                // Pointer to aggregate is in integer register
                *os << "  str " << int_regs[argIndex] << ", " << dest << "\n";
            } else {
                // Pointer to aggregate is on stack
                size_t stackOffset = (argIndex - int_regs.size()) * 8 + 16;
                *os << "  ldr x9, [x29, #" << stackOffset << "]\n";
                *os << "  str x9, " << dest << "\n";
            }
        } else {
            // Default case - treat as 64-bit value
            if (argIndex < int_regs.size()) {
                *os << "  str " << int_regs[argIndex] << ", " << dest << "\n";
            } else {
                size_t stackOffset = (argIndex - int_regs.size()) * 8 + 16;
                *os << "  ldr x9, [x29, #" << stackOffset << "]\n";
                *os << "  str x9, " << dest << "\n";
            }
        }

        // Add debug comment for argument retrieval
        *os << "  ; Retrieved argument " << argIndex << " of type " << type->toString() << "\n";
    } else {
        (void)cg; (void)argIndex; (void)dest; (void)type;
        // For binary mode, GetArgument is typically handled by the function prologue
        // that spills registers to stack slots.
    }
}

size_t AArch64::calculateStackArgumentOffset(size_t argIndex, const ir::Type* type) const {
    (void)type;
    // Calculate offset for stack-passed arguments
    // This is simplified - real ABI has more complex rules
    size_t offset = 16; // Past saved fp and lr
    offset += (argIndex - getMaxRegistersForArgs()) * 8;
    return offset;
}

bool AArch64::isCallerSaved(const std::string& reg) const {
    // AArch64 calling convention: x0-x18 are caller-saved
    static const std::set<std::string> caller_saved = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
        "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17",
        "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26",
        "v27", "v28", "v29", "v30", "v31"
    };
    return caller_saved.find(reg) != caller_saved.end();
}

bool AArch64::isCalleeSaved(const std::string& reg) const {
    // AArch64 calling convention: x19-x28 are callee-saved
    static const std::set<std::string> callee_saved = {
        "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15"
    };
    return callee_saved.find(reg) != callee_saved.end();
}

std::string AArch64::getWRegister(const std::string& xReg) const {
    if (xReg.substr(0, 1) == "x") {
        return "w" + xReg.substr(1);
    }
    return xReg;
}

std::string AArch64::getAArch64InstructionSuffix(const ir::Type* type) const {
    if (type->isFloatTy()) return "s";
    if (type->isDoubleTy()) return "d";
    return "";
}

// Advanced control flow helpers
void AArch64::emitConditionalBranch(CodeGen& cg, const std::string& condition,
                                   const std::string& trueLabel, const std::string& falseLabel) {
    if (auto* os = cg.getTextStream()) {
        std::string condCode = getConditionCode(condition);
        *os << "  b." << condCode << " " << trueLabel << "\n";
        if (!falseLabel.empty()) {
            *os << "  b " << falseLabel << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        std::string condCode = getConditionCode(condition);
        uint32_t cond_bits = 0;
        if (condCode == "eq") cond_bits = 0x0;
        else if (condCode == "ne") cond_bits = 0x1;
        else if (condCode == "lt") cond_bits = 0xB;
        else if (condCode == "le") cond_bits = 0xD;
        else if (condCode == "gt") cond_bits = 0xC;
        else if (condCode == "ge") cond_bits = 0xA;
        else if (condCode == "lo") cond_bits = 0x3;
        else if (condCode == "ls") cond_bits = 0x9;
        else if (condCode == "hi") cond_bits = 0x8;
        else if (condCode == "hs") cond_bits = 0x2;

        // b.cond <target>
        assembler.emitDWord(0x54000000 | (cond_bits));
        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        reloc.symbolName = trueLabel;
        reloc.type = "R_AARCH64_CONDBR19";
        reloc.sectionName = ".text";
        cg.addRelocation(reloc);

        if (!falseLabel.empty()) {
            assembler.emitDWord(0x14000000);
            CodeGen::RelocationInfo reloc2;
            reloc2.offset = assembler.getCodeSize() - 4;
            reloc2.symbolName = falseLabel;
            reloc2.type = "R_AARCH64_JUMP26";
            reloc2.sectionName = ".text";
            cg.addRelocation(reloc2);
        }
    }
}

void AArch64::emitCompareAndBranch(CodeGen& cg, const std::string& reg,
                                  const std::string& label, bool branchOnZero) {
    if (auto* os = cg.getTextStream()) {
        if (branchOnZero) {
            *os << "  cbz " << reg << ", " << label << "\n";
        } else {
            *os << "  cbnz " << reg << ", " << label << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        uint32_t reg_idx = std::stoi(reg.substr(1));
        uint32_t instruction = branchOnZero ? 0x34000000 : 0x35000000;
        if (reg[0] == 'x') instruction |= 0x80000000;
        instruction |= (reg_idx & 0x1F);

        assembler.emitDWord(instruction);
        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        reloc.symbolName = label;
        reloc.type = "R_AARCH64_CONDBR19";
        reloc.sectionName = ".text";
        cg.addRelocation(reloc);
    }
}

std::string AArch64::getConditionCode(const std::string& op, bool isFloat, bool isUnsigned) const {
    if (op == "eq") return "eq";
    if (op == "ne") return "ne";
    
    if (isFloat) {
        if (op == "lt") return "mi";
        if (op == "le") return "ls";
        if (op == "gt") return "gt";
        if (op == "ge") return "ge";
    } else if (isUnsigned) {
        if (op == "lt" || op == "ult") return "lo";
        if (op == "le" || op == "ule") return "ls";
        if (op == "gt" || op == "ugt") return "hi";
        if (op == "ge" || op == "uge") return "hs";
    } else {
        if (op == "lt") return "lt";
        if (op == "le") return "le";
        if (op == "gt") return "gt";
        if (op == "ge") return "ge";
    }
    
    return "al"; // Always (default)
}

void AArch64::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        *os << "  sub sp, sp, #16\n";
        *os << "  stp x29, x30, [sp]\n";
        *os << "  mov x29, sp\n";
        if (stack_size > 0) {
            if (stack_size <= 4095) {
                *os << "  sub sp, sp, #" << stack_size << "\n";
            } else {
                *os << "  mov x9, #" << stack_size << "\n";
                *os << "  sub sp, sp, x9\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        // sub sp, sp, #16
        assembler.emitDWord(0xD10043FF);
        // stp x29, x30, [sp]
        assembler.emitDWord(0xA9007BFD);
        // mov x29, sp
        assembler.emitDWord(0x910003FD);
        if (stack_size > 0) {
            if (stack_size <= 4095) {
                // sub sp, sp, #stack_size
                uint32_t imm12 = stack_size;
                assembler.emitDWord(0xD10003FF | (imm12 << 10));
            } else {
                // mov x9, #stack_size (using movz and movk)
                uint32_t imm16_0 = stack_size & 0xFFFF;
                uint32_t imm16_1 = (stack_size >> 16) & 0xFFFF;
                assembler.emitDWord(0xD2800009 | (imm16_0 << 5)); // movz x9, imm16_0
                if (imm16_1 != 0) {
                    assembler.emitDWord(0xF2A00009 | (imm16_1 << 5)); // movk x9, imm16_1, lsl 16
                }
                // sub sp, sp, x9
                assembler.emitDWord(0xCB0903E9);
            }
        }
    }
}

void AArch64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n# --- Executable Entry Point ---\n";
        *os << ".globl _start\n";
        *os << "_start:\n";
        *os << "  // Call the user's main function\n";
        *os << "  bl main\n\n";
        *os << "  // Exit with the return code from main\n";
        *os << "  // x0 already contains the return code\n";
        *os << "  mov x8, #93 // 93 is the syscall number for exit on AArch64\n";
        *os << "  svc #0      // Invoke kernel\n";
    } else {
        auto& assembler = cg.getAssembler();

        // Add _start symbol
        CodeGen::SymbolInfo start_sym;
        start_sym.name = "_start";
        start_sym.sectionName = ".text";
        start_sym.value = assembler.getCodeSize();
        start_sym.type = 2; // STT_FUNC
        start_sym.binding = 1; // STB_GLOBAL
        cg.addSymbol(start_sym);

        // bl main
        assembler.emitDWord(0x94000000); // bl <offset>
        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        reloc.symbolName = "main";
        reloc.type = "R_AARCH64_CALL26";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);

        // x0 already has the return value

        // mov x8, #93 (exit syscall)
        assembler.emitDWord(0xD2800BA8);
        // svc #0
        assembler.emitDWord(0xD4000001);
    }
}

void AArch64::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov sp, x29\n";
        *os << "  ldp x29, x30, [sp]\n";
        *os << "  add sp, sp, #16\n";
        *os << "  ret\n";
    } else {
        auto& assembler = cg.getAssembler();
        // mov sp, x29
        assembler.emitDWord(0x910003DF);
        // ldp x29, x30, [sp]
        assembler.emitDWord(0xA9407BFD);
        // add sp, sp, #16
        assembler.emitDWord(0x910043FF);
        // ret
        assembler.emitDWord(0xD65F03C0);
    }
}

const std::vector<std::string>& AArch64::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> regs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    return regs;
}

const std::vector<std::string>& AArch64::getFloatArgumentRegisters() const {
    static const std::vector<std::string> regs = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"};
    return regs;
}

const std::string& AArch64::getIntegerReturnRegister() const {
    static const std::string reg = "x0";
    return reg;
}

const std::string& AArch64::getFloatReturnRegister() const {
    static const std::string reg = "v0";
    return reg;
}

void AArch64::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (!instr.getOperands().empty()) {
        ir::Value* retVal = instr.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            std::string retval = cg.getValueAsOperand(retVal);
            std::string reg = getRegisterName("x0", retVal->getType());
            *os << "  " << getLoadInstr(retVal->getType()) << " " << reg << ", " << retval << "\n";
        } else {
            auto& assembler = cg.getAssembler();
            emitLoadValue(cg, assembler, retVal, 0);
        }
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  b " << getEpilogueLabel(instr.getParent()->getParent()) << "\n";
        return;
    }
    emitFunctionEpilogue(cg, *instr.getParent()->getParent());
}

void AArch64::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  add " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // add x9, x9, x10
        uint32_t instruction = 0x8B0A0129;
        if (type->getSize() <= 4) instruction &= ~0x80000000; // 32-bit if sf=0
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitAddBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // add x9, x9, x10
    assembler.emitDWord(0x8B0A0129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitSub(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  sub " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // sub x9, x9, x10
        uint32_t instruction = 0xCB0A0129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitSubBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // sub x9, x9, x10
    assembler.emitDWord(0xCB0A0129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitMul(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  mul " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // mul x9, x9, x10
        uint32_t instruction = 0x9B0A7D29;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitMulBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // mul x9, x9, x10
    assembler.emitDWord(0x9B0A7D29);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Udiv);

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  " << (isUnsigned ? "udiv " : "sdiv ") << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // sdiv/udiv x9, x9, x10
        uint32_t instruction = isUnsigned ? 0x9ACAD529 : 0x9ACAD129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Urem);

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        std::string reg3 = getRegisterName("x11", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  " << (isUnsigned ? "udiv " : "sdiv ") << reg3 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  msub " << reg1 << ", " << reg3 << ", " << reg2 << ", " << reg1 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);

        // udiv/sdiv x11, x9, x10
        uint32_t div_instr = isUnsigned ? 0x9ACAD52B : 0x9ACAD12B;
        if (type->getSize() <= 4) div_instr &= ~0x80000000;
        else div_instr |= 0x80000000;
        assembler.emitDWord(div_instr);

        // msub x9, x11, x10, x9
        uint32_t msub_instr = 0x9B2A8169;
        if (type->getSize() <= 4) msub_instr &= ~0x80000000;
        else msub_instr |= 0x80000000;
        assembler.emitDWord(msub_instr);

        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitDivBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // sdiv x9, x9, x10
    assembler.emitDWord(0x9ACAD129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitAnd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  and " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // and x9, x9, x10
        uint32_t instruction = 0x8A0A0129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitAndBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // and x9, x9, x10
    assembler.emitDWord(0x8A0A0129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitOr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  orr " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // orr x9, x9, x10
        uint32_t instruction = 0xAA0A0129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitOrBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // orr x9, x9, x10
    assembler.emitDWord(0xAA0A0129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitXor(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  eor " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // eor x9, x9, x10
        uint32_t instruction = 0xCA0A0129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitXorBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // eor x9, x9, x10
    assembler.emitDWord(0xCA0A0129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitShl(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  lsl " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // lslv x9, x9, x10
        uint32_t instruction = 0x9AD22129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitShlBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // lslv x9, x9, x10
    assembler.emitDWord(0x9AD22129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitShr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  lsr " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // lsrv x9, x9, x10
        uint32_t instruction = 0x9AD22929;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitShrBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // lsrv x9, x9, x10
    assembler.emitDWord(0x9AD22929);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitSar(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string reg1 = getRegisterName("x9", type);
        std::string reg2 = getRegisterName("x10", type);
        *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
        *os << "  asr " << reg1 << ", " << reg1 << ", " << reg2 << "\n";
        *os << "  " << getStoreInstr(type) << " " << reg1 << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 9);
        emitLoadValue(cg, assembler, rhs, 10);
        // asrv x9, x9, x10
        uint32_t instruction = 0x9AD23129;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitSarBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t lhsOffset = cg.getStackOffset(lhs);
    int32_t rhsOffset = cg.getStackOffset(rhs);

    // ldur x9, [x29, #lhsOffset]
    assembler.emitDWord(0xF8400C09 | ((lhsOffset & 0x1FF) << 12) | (29 << 5));
    // ldur x10, [x29, #rhsOffset]
    assembler.emitDWord(0xF8400C0A | ((rhsOffset & 0x1FF) << 12) | (29 << 5));
    // asrv x9, x9, x10
    assembler.emitDWord(0x9AD23129);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        if (type->isFloatTy()) {
            *os << "  ldr s0, " << opOperand << "\n";
            *os << "  fneg s0, s0\n";
            *os << "  str s0, " << destOperand << "\n";
        } else if (type->isDoubleTy()) {
            *os << "  ldr d0, " << opOperand << "\n";
            *os << "  fneg d0, d0\n";
            *os << "  str d0, " << destOperand << "\n";
        } else {
            std::string reg = getRegisterName("x9", type);
            *os << "  " << getLoadInstr(type) << " " << reg << ", " << opOperand << "\n";
            *os << "  neg " << reg << ", " << reg << "\n";
            *os << "  " << getStoreInstr(type) << " " << reg << ", " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        if (type->isFloatingPoint()) {
            emitLoadValue(cg, assembler, op, 0); // Load into v0
            // fneg s0, s0 (0x1E214000) or fneg d0, d0 (0x1E614000)
            uint32_t instruction = (type->getSize() == 4) ? 0x1E214000 : 0x1E614000;
            assembler.emitDWord(instruction);
            // store
            int32_t destOffset = cg.getStackOffset(dest);
            uint32_t base = (type->getSize() == 4) ? 0xBC000000 : 0xFC000000;
            assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
        } else {
            emitLoadValue(cg, assembler, op, 9);
            // neg x9, x9 (sub x9, xzr, x9)
            uint32_t instruction = 0xCB0903E9;
            if (type->getSize() <= 4) instruction &= ~0x80000000;
            else instruction |= 0x80000000;
            assembler.emitDWord(instruction);
            // store
            int32_t destOffset = cg.getStackOffset(dest);
            uint32_t base = (type->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
            assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
        }
    }
}

void AArch64::emitNegBinary(CodeGen& cg, ir::Instruction& instr) {
    auto& assembler = cg.getAssembler();
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();

    int32_t destOffset = cg.getStackOffset(dest);
    int32_t opOffset = cg.getStackOffset(op);

    // ldur x9, [x29, #opOffset]
    assembler.emitDWord(0xF8400C09 | ((opOffset & 0x1FF) << 12) | (29 << 5));
    // neg x9, x9
    assembler.emitDWord(0xCB0903E9);
    // stur x9, [x29, #destOffset]
    assembler.emitDWord(0xF8000C09 | ((destOffset & 0x1FF) << 12) | (29 << 5));
}

void AArch64::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* src = instr.getOperands()[0]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string srcOperand = cg.getValueAsOperand(src);
        *os << "  ldr x9, " << srcOperand << "\n";
        *os << "  str x9, " << destOperand << "\n";
    }
}

void AArch64::emitCall(CodeGen& cg, ir::Instruction& instr) {
    unsigned int_reg_idx = 0;
    unsigned float_reg_idx = 0;
    std::vector<ir::Value*> stack_args;

    if (auto* os = cg.getTextStream()) {
        const auto& int_regs = getIntegerArgumentRegisters();
        const auto& float_regs = getFloatArgumentRegisters();
        std::string callee = cg.getValueAsOperand(instr.getOperands()[0]->get());
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            TypeInfo info = getTypeInfo(arg->getType());
            std::string argOperand = cg.getValueAsOperand(arg);
            bool isImm = argOperand[0] == '#';

            if (info.regClass == RegisterClass::Float) {
                if (float_reg_idx < 8) {
                    std::string reg = (info.size == 32) ? "s" : "d";
                    reg += std::to_string(float_reg_idx++);
                    if (isImm) *os << "  fmov " << reg << ", " << argOperand << "\n";
                    else *os << "  ldr " << reg << ", " << argOperand << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            } else {
                if (int_reg_idx < 8) {
                    std::string reg = getRegisterName("x" + std::to_string(int_reg_idx++), arg->getType());
                    if (isImm) *os << "  mov " << reg << ", " << argOperand << "\n";
                    else *os << "  ldr " << reg << ", " << argOperand << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            std::string argOperand = cg.getValueAsOperand(arg);
            bool isImm = argOperand[0] == '#';
            if (isImm) *os << "  mov x9, " << argOperand << "\n";
            else *os << "  ldr x9, " << argOperand << "\n";
            *os << "  str x9, [sp, #-16]!\n";
        }

        *os << "  bl " << callee << "\n";

        if (!stack_args.empty()) {
            *os << "  add sp, sp, #" << stack_args.size() * 16 << "\n"; // AAPCS64 stack is 16-byte aligned
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string destOperand = cg.getValueAsOperand(&instr);
            std::string reg = getRegisterName("x0", instr.getType());
            if (instr.getType()->isFloatingPoint()) reg = (instr.getType()->getSize() == 4) ? "s0" : "d0";
            *os << "  str " << reg << ", " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            TypeInfo info = getTypeInfo(arg->getType());
            if (info.regClass == RegisterClass::Float) {
                if (float_reg_idx < 8) {
                    emitLoadValue(cg, assembler, arg, float_reg_idx++); // Uses SIMD load if float
                } else {
                    stack_args.push_back(arg);
                }
            } else {
                if (int_reg_idx < 8) {
                    emitLoadValue(cg, assembler, arg, int_reg_idx++);
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            emitLoadValue(cg, assembler, arg, 9);
            // str x9, [sp, #-16]! (Pre-indexed)
            assembler.emitDWord(0xF81F0FE9);
        }

        assembler.emitDWord(0x94000000 | 0); // bl <offset>

        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        ir::Value* targetVal = instr.getOperands()[0]->get();
        if (auto* bbTarget = dynamic_cast<ir::BasicBlock*>(targetVal)) {
            reloc.symbolName = bbTarget->getParent()->getName() + "_" + bbTarget->getName();
        } else {
            reloc.symbolName = targetVal->getName();
        }
        reloc.type = "R_AARCH64_CALL26";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);

        if (!stack_args.empty()) {
            // add sp, sp, #stack_args.size() * 16
            uint32_t imm12 = stack_args.size() * 16;
            assembler.emitDWord(0x910003FF | (imm12 << 10));
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            if (cg.getStackOffsets().count(&instr)) {
                int32_t destOffset = cg.getStackOffset(&instr);
                uint32_t base = (instr.getType()->isFloatingPoint()) ?
                    ((instr.getType()->getSize() == 4) ? 0xBC000000 : 0xFC000000) :
                    ((instr.getType()->getSize() > 4) ? 0xF8000000 : 0xB8000000);
                assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
            }
        }
    }
}

// New instruction emission methods
void AArch64::emitNot(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        std::string reg = getRegisterName("x9", type);

        *os << "  " << getLoadInstr(type) << " " << reg << ", " << opOperand << "\n";
        *os << "  mvn " << reg << ", " << reg << "\n";  // Bitwise NOT
        *os << "  " << getStoreInstr(type) << " " << reg << ", " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, op, 9);
        // mvn x9, x9 (orn x9, xzr, x9)
        uint32_t instruction = 0xAA2903E9;
        if (type->getSize() <= 4) instruction &= ~0x80000000;
        else instruction |= 0x80000000;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000000 : 0xB8000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
    }
}

void AArch64::emitCmp(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    std::string cond_code;
    uint32_t cond_bits = 0;
    bool is_float = type->isFloatTy() || type->isDoubleTy();

    switch(instr.getOpcode()) {
        case ir::Instruction::Ceq:  cond_code = "eq"; cond_bits = 0x0; break;
        case ir::Instruction::Cne:  cond_code = "ne"; cond_bits = 0x1; break;
        case ir::Instruction::Cslt: cond_code = "lt"; cond_bits = 0xB; break;
        case ir::Instruction::Csle: cond_code = "le"; cond_bits = 0xD; break;
        case ir::Instruction::Csgt: cond_code = "gt"; cond_bits = 0xC; break;
        case ir::Instruction::Csge: cond_code = "ge"; cond_bits = 0xA; break;
        case ir::Instruction::Cult: cond_code = "lo"; cond_bits = 0x3; break;
        case ir::Instruction::Cule: cond_code = "ls"; cond_bits = 0x9; break;
        case ir::Instruction::Cugt: cond_code = "hi"; cond_bits = 0x8; break;
        case ir::Instruction::Cuge: cond_code = "hs"; cond_bits = 0x2; break;
        case ir::Instruction::Ceqf: cond_code = "eq"; cond_bits = 0x0; break;
        case ir::Instruction::Cnef: cond_code = "ne"; cond_bits = 0x1; break;
        case ir::Instruction::Clt:  cond_code = is_float ? "mi" : "lt"; cond_bits = is_float ? 0x4 : 0xB; break;
        case ir::Instruction::Cle:  cond_code = is_float ? "ls" : "le"; cond_bits = is_float ? 0x9 : 0xD; break;
        case ir::Instruction::Cgt:  cond_code = "gt"; cond_bits = 0xC; break;
        case ir::Instruction::Cge:  cond_code = "ge"; cond_bits = 0xA; break;
        case ir::Instruction::Co:   cond_code = "vs"; cond_bits = 0x6; break;
        case ir::Instruction::Cuo:  cond_code = "vc"; cond_bits = 0x7; break;
        default: return;
    }

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);

        if (is_float) {
            if (type->isFloatTy()) {
                *os << "  ldr s0, " << lhsOperand << "\n";
                *os << "  ldr s1, " << rhsOperand << "\n";
                *os << "  fcmp s0, s1\n";
            } else {
                *os << "  ldr d0, " << lhsOperand << "\n";
                *os << "  ldr d1, " << rhsOperand << "\n";
                *os << "  fcmp d0, d1\n";
            }
        } else {
            std::string reg1 = getRegisterName("x10", type);
            std::string reg2 = getRegisterName("x11", type);
            *os << "  " << getLoadInstr(type) << " " << reg1 << ", " << lhsOperand << "\n";
            *os << "  " << getLoadInstr(type) << " " << reg2 << ", " << rhsOperand << "\n";
            *os << "  cmp " << reg1 << ", " << reg2 << "\n";
        }
        
        *os << "  cset w9, " << cond_code << "\n";
        *os << "  str w9, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        if (is_float) {
            emitLoadValue(cg, assembler, lhs, 0); // load into v0
            emitLoadValue(cg, assembler, rhs, 1); // load into v1
            // fcmp s0, s1 (0x1E212020) or fcmp d0, d1 (0x1E612020)
            uint32_t instruction = (type->getSize() == 4) ? 0x1E212000 : 0x1E612000;
            instruction |= (1 << 5); // rn = v1 (wait, fcmp rn, rm)
            assembler.emitDWord(instruction);

            // cset w9, cond
            uint32_t inv_cond = cond_bits ^ 1;
            assembler.emitDWord(0x1A9F07E9 | (inv_cond << 12));

            // store
            int32_t destOffset = cg.getStackOffset(dest);
            assembler.emitDWord(0xB8000000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
        } else {
            emitLoadValue(cg, assembler, lhs, 10);
            emitLoadValue(cg, assembler, rhs, 11);
            // cmp x10, x11 (subs xzr, x10, x11)
            uint32_t instruction = 0xEB0B01FF;
            if (type->getSize() <= 4) instruction &= ~0x80000000;
            else instruction |= 0x80000000;
            assembler.emitDWord(instruction);

            // cset w9, cond -> csinc w9, wzr, wzr, inv_cond
            uint32_t inv_cond = cond_bits ^ 1;
            assembler.emitDWord(0x1A9F07E9 | (inv_cond << 12));

            // store
            int32_t destOffset = cg.getStackOffset(dest);
            assembler.emitDWord(0xB8000000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
        }
    }
}

void AArch64::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);

        if (type->isFloatTy()) {
            *os << "  ldr s0, " << lhsOperand << "\n";
            *os << "  ldr s1, " << rhsOperand << "\n";
            *os << "  fadd s0, s0, s1\n";
            *os << "  str s0, " << destOperand << "\n";
        } else {
            *os << "  ldr d0, " << lhsOperand << "\n";
            *os << "  ldr d1, " << rhsOperand << "\n";
            *os << "  fadd d0, d0, d1\n";
            *os << "  str d0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        // fadd s0, s0, s1 (0x1E212800) or fadd d0, d0, d1 (0x1E612800)
        uint32_t instruction = (type->getSize() == 4) ? 0x1E212800 : 0x1E612800;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() == 4) ? 0xBC000000 : 0xFC000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);

        if (type->isFloatTy()) {
            *os << "  ldr s0, " << lhsOperand << "\n";
            *os << "  ldr s1, " << rhsOperand << "\n";
            *os << "  fsub s0, s0, s1\n";
            *os << "  str s0, " << destOperand << "\n";
        } else {
            *os << "  ldr d0, " << lhsOperand << "\n";
            *os << "  ldr d1, " << rhsOperand << "\n";
            *os << "  fsub d0, d0, d1\n";
            *os << "  str d0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        // fsub s0, s0, s1 (0x1E213800) or fsub d0, d0, d1 (0x1E613800)
        uint32_t instruction = (type->getSize() == 4) ? 0x1E213800 : 0x1E613800;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() == 4) ? 0xBC000000 : 0xFC000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitFMul(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);

        if (type->isFloatTy()) {
            *os << "  ldr s0, " << lhsOperand << "\n";
            *os << "  ldr s1, " << rhsOperand << "\n";
            *os << "  fmul s0, s0, s1\n";
            *os << "  str s0, " << destOperand << "\n";
        } else {
            *os << "  ldr d0, " << lhsOperand << "\n";
            *os << "  ldr d1, " << rhsOperand << "\n";
            *os << "  fmul d0, d0, d1\n";
            *os << "  str d0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        // fmul s0, s0, s1 (0x1E210800) or fmul d0, d0, d1 (0x1E610800)
        uint32_t instruction = (type->getSize() == 4) ? 0x1E210800 : 0x1E610800;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() == 4) ? 0xBC000000 : 0xFC000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);

        if (type->isFloatTy()) {
            *os << "  ldr s0, " << lhsOperand << "\n";
            *os << "  ldr s1, " << rhsOperand << "\n";
            *os << "  fdiv s0, s0, s1\n";
            *os << "  str s0, " << destOperand << "\n";
        } else {
            *os << "  ldr d0, " << lhsOperand << "\n";
            *os << "  ldr d1, " << rhsOperand << "\n";
            *os << "  fdiv d0, d0, d1\n";
            *os << "  str d0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        // fdiv s0, s0, s1 (0x1E211800) or fdiv d0, d0, d1 (0x1E611800)
        uint32_t instruction = (type->getSize() == 4) ? 0x1E211800 : 0x1E611800;
        assembler.emitDWord(instruction);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() == 4) ? 0xBC000000 : 0xFC000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitCast(CodeGen& cg, ir::Instruction& instr,
                      const ir::Type* fromType, const ir::Type* toType) {
    ir::Value* dest = &instr;
    ir::Value* src = instr.getOperands()[0]->get();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string srcOperand = cg.getValueAsOperand(src);

        // Handle different conversion types

        // Integer to Float conversions
        if (fromType->isIntegerTy() && toType->isFloatTy()) {
            std::string reg = getRegisterName("x9", fromType);
            *os << "  " << getLoadInstr(fromType) << " " << reg << ", " << srcOperand << "\n";
            *os << "  scvtf s0, " << reg << "\n";
            *os << "  str s0, " << destOperand << "\n";
        }
        // Integer to Double conversions
        else if (fromType->isIntegerTy() && toType->isDoubleTy()) {
            std::string reg = getRegisterName("x9", fromType);
            *os << "  " << getLoadInstr(fromType) << " " << reg << ", " << srcOperand << "\n";
            *os << "  scvtf d0, " << reg << "\n";
            *os << "  str d0, " << destOperand << "\n";
        }
        // Float to Integer conversions
        else if (fromType->isFloatTy() && toType->isIntegerTy()) {
            *os << "  ldr s0, " << srcOperand << "\n";
            std::string reg = getRegisterName("x9", toType);
            *os << "  fcvtzs " << reg << ", s0\n";
            *os << "  " << getStoreInstr(toType) << " " << reg << ", " << destOperand << "\n";
        }
        // Double to Integer conversions
        else if (fromType->isDoubleTy() && toType->isIntegerTy()) {
            *os << "  ldr d0, " << srcOperand << "\n";
            std::string reg = getRegisterName("x9", toType);
            *os << "  fcvtzs " << reg << ", d0\n";
            *os << "  " << getStoreInstr(toType) << " " << reg << ", " << destOperand << "\n";
        }
        // Float to Double conversion
        else if (fromType->isFloatTy() && toType->isDoubleTy()) {
            *os << "  ldr s0, " << srcOperand << "\n";
            *os << "  fcvt d0, s0\n";
            *os << "  str d0, " << destOperand << "\n";
        }
        // Double to Float conversion
        else if (fromType->isDoubleTy() && toType->isFloatTy()) {
            *os << "  ldr d0, " << srcOperand << "\n";
            *os << "  fcvt s0, d0\n";
            *os << "  str s0, " << destOperand << "\n";
        }
        // Integer to Integer conversions (size changes)
        else if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            auto* fromIntTy = static_cast<const ir::IntegerType*>(fromType);
            auto* toIntTy = static_cast<const ir::IntegerType*>(toType);

            if (fromIntTy->getBitwidth() < toIntTy->getBitwidth()) {
                // Sign extension
                std::string srcReg = getRegisterName("w9", fromType);
                std::string destReg = getRegisterName("x10", toType);
                *os << "  " << getLoadInstr(fromType) << " " << srcReg << ", " << srcOperand << "\n";
                if (fromIntTy->getBitwidth() == 8) {
                    *os << "  sxtb " << destReg << ", w9\n";
                } else if (fromIntTy->getBitwidth() == 16) {
                    *os << "  sxth " << destReg << ", w9\n";
                } else if (fromIntTy->getBitwidth() == 32) {
                    *os << "  sxtw x10, w9\n";
                }
                *os << "  " << getStoreInstr(toType) << " " << destReg << ", " << destOperand << "\n";
            } else if (fromIntTy->getBitwidth() > toIntTy->getBitwidth()) {
                // Truncation
                std::string srcReg = getRegisterName("x9", fromType);
                std::string destReg = getRegisterName("x9", toType);
                *os << "  " << getLoadInstr(fromType) << " " << srcReg << ", " << srcOperand << "\n";
                *os << "  " << getStoreInstr(toType) << " " << destReg << ", " << destOperand << "\n";
            } else {
                // Same size - just copy
                std::string reg = getRegisterName("x9", fromType);
                *os << "  " << getLoadInstr(fromType) << " " << reg << ", " << srcOperand << "\n";
                *os << "  " << getStoreInstr(toType) << " " << reg << ", " << destOperand << "\n";
            }
        }
        // Pointer conversions
        else if (fromType->isPointerTy() || toType->isPointerTy()) {
            // Pointer casts are essentially no-ops on AArch64
            *os << "  ldr x9, " << srcOperand << "\n";
            *os << "  str x9, " << destOperand << "\n";
        }
        // Default: bitcast (copy)
        else {
            *os << "  ldr x9, " << srcOperand << "\n";
            *os << "  str x9, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        if (fromType->isIntegerTy() && toType->isIntegerTy()) {
             emitLoadValue(cg, assembler, src, 9);
             int32_t destOffset = cg.getStackOffset(dest);
             uint32_t base = (toType->getSize() > 4) ? 0xF8000C00 : 0xB8000C00;
             assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 9);
        }
    }
}

void AArch64::emitVAStart(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Simplified implementation - in real ABI this would set up va_list structure
        std::string vaList = cg.getValueAsOperand(instr.getOperands()[0]->get());
        *os << "  mov x9, sp\n";
        *os << "  str x9, " << vaList << "\n";
    }
}

void AArch64::emitVAArg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Simplified implementation
        ir::Value* dest = &instr;
        std::string vaList = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string destOperand = cg.getValueAsOperand(dest);

        *os << "  ldr x9, " << vaList << "\n";
        *os << "  ldr x10, [x9]\n";
        *os << "  str x10, " << destOperand << "\n";
        *os << "  add x9, x9, #8\n";
        *os << "  str x9, " << vaList << "\n";
    }
}

void AArch64::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
    // No-op for AArch64
}

uint64_t AArch64::getSyscallNumber(ir::SyscallId id) const {
    switch (id) {
        case ir::SyscallId::Exit: return 93;
        case ir::SyscallId::Read: return 63;
        case ir::SyscallId::Write: return 64;
        case ir::SyscallId::OpenAt: return 56;
        case ir::SyscallId::Close: return 57;
        case ir::SyscallId::LSeek: return 62;
        case ir::SyscallId::MMap: return 222;
        case ir::SyscallId::MUnmap: return 215;
        case ir::SyscallId::MProtect: return 226;
        case ir::SyscallId::Brk: return 214;
        case ir::SyscallId::MkDirAt: return 34;
        case ir::SyscallId::UnlinkAt: return 35;
        case ir::SyscallId::RenameAt: return 38;
        case ir::SyscallId::GetDents64: return 61;
        case ir::SyscallId::ClockGetTime: return 113;
        case ir::SyscallId::NanoSleep: return 101;
        case ir::SyscallId::RtSigAction: return 134;
        case ir::SyscallId::RtSigProcMask: return 135;
        case ir::SyscallId::RtSigReturn: return 139;
        case ir::SyscallId::GetRandom: return 278;
        case ir::SyscallId::Uname: return 160;
        case ir::SyscallId::GetPid: return 172;
        case ir::SyscallId::GetTid: return 178;
        case ir::SyscallId::Fork: return 220; // AArch64 often uses clone instead of fork
        case ir::SyscallId::Execve: return 221;
        case ir::SyscallId::Clone: return 220;
        case ir::SyscallId::Wait4: return 260;
        case ir::SyscallId::Kill: return 129;
        default: return 0;
    }
}

void AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    if (auto* os = cg.getTextStream()) {
        // AArch64 Syscall ABI: x8 (number), x0-x5 (args)
        if (sid != ir::SyscallId::None) {
            *os << "  mov x8, #" << getSyscallNumber(sid) << "\n";
        } else {
            *os << "  mov x8, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 6) {
                *os << "  mov x" << argIdx << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
            }
        }
        *os << "  svc #0\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // mov x8, num
        if (sid != ir::SyscallId::None) {
            uint64_t num = getSyscallNumber(sid);
            assembler.emitDWord(0xD2800008 | ((num & 0xFFFF) << 5));
        } else {
            emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 8);
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 6) {
                emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), argIdx);
            }
        }
        // svc #0 -> 0xD4000001
        assembler.emitDWord(0xD4000001);
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            // str x0, [x29, #offset]
            assembler.emitDWord(0xF8000BA0 | ((destOffset & 0x1FF) << 12));
        }
    }
}

void AArch64::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* ptr = instr.getOperands()[0]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string ptrOperand = cg.getValueAsOperand(ptr);

        // Load the pointer address
        *os << "  ldr x9, " << ptrOperand << "\n";

        // Check if we need to handle offset
        if (instr.getOperands().size() > 1) {
            // Handle indexed load with offset
            ir::Value* offset = instr.getOperands()[1]->get();
            std::string offsetOperand = cg.getValueAsOperand(offset);
            *os << "  ldr x10, " << offsetOperand << "\n";
            *os << "  add x9, x9, x10\n";
        }

        // Perform the load based on the type
        if (instr.getType()->isFloatTy()) {
            *os << "  ldr s0, [x9]\n";
            *os << "  str s0, " << destOperand << "\n";
        } else if (instr.getType()->isDoubleTy()) {
            *os << "  ldr d0, [x9]\n";
            *os << "  str d0, " << destOperand << "\n";
        } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(instr.getType())) {
            uint64_t bitWidth = intTy->getBitwidth();
            if (bitWidth == 8) {
                *os << "  ldrb w10, [x9]\n";
                *os << "  str w10, " << destOperand << "\n";
            } else if (bitWidth == 16) {
                *os << "  ldrh w10, [x9]\n";
                *os << "  str w10, " << destOperand << "\n";
            } else if (bitWidth == 32) {
                *os << "  ldr w10, [x9]\n";
                *os << "  str w10, " << destOperand << "\n";
            } else {
                *os << "  ldr x10, [x9]\n";
                *os << "  str x10, " << destOperand << "\n";
            }
        } else {
            // Default to 64-bit load
            *os << "  ldr x10, [x9]\n";
            *os << "  str x10, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* ptr = instr.getOperands()[0]->get();
        const ir::Type* type = instr.getType();

        emitLoadValue(cg, assembler, ptr, 9);
        // ldr x10, [x9]
        uint32_t ldr;
        if (type->getSize() == 1) ldr = 0x3940012A; // ldrb
        else if (type->getSize() == 2) ldr = 0x7940012A; // ldrh
        else if (type->getSize() == 4) ldr = 0xB940012A; // ldr w
        else ldr = 0xF940012A; // ldr x
        assembler.emitDWord(ldr);
        // store
        int32_t destOffset = cg.getStackOffset(dest);
        uint32_t base = (type->getSize() > 4) ? 0xF8000000 : 0xB8000000;
        assembler.emitDWord(base | ((destOffset & 0x1FF) << 12) | (29 << 5) | 10);
    }
}

void AArch64::emitStore(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* value = instr.getOperands()[0]->get();
        ir::Value* ptr = instr.getOperands()[1]->get();
        std::string valueOperand = cg.getValueAsOperand(value);
        std::string ptrOperand = cg.getValueAsOperand(ptr);

        // Load the pointer address
        *os << "  ldr x9, " << ptrOperand << "\n";

        // Check if we need to handle offset
        if (instr.getOperands().size() > 2) {
            // Handle indexed store with offset
            ir::Value* offset = instr.getOperands()[2]->get();
            std::string offsetOperand = cg.getValueAsOperand(offset);
            *os << "  ldr x11, " << offsetOperand << "\n";
            *os << "  add x9, x9, x11\n";
        }

        // Perform the store based on the type
        if (value->getType()->isFloatTy()) {
            *os << "  ldr s0, " << valueOperand << "\n";
            *os << "  str s0, [x9]\n";
        } else if (value->getType()->isDoubleTy()) {
            *os << "  ldr d0, " << valueOperand << "\n";
            *os << "  str d0, [x9]\n";
        } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(value->getType())) {
            uint64_t bitWidth = intTy->getBitwidth();
            if (bitWidth == 8) {
                *os << "  ldr w10, " << valueOperand << "\n";
                *os << "  strb w10, [x9]\n";
            } else if (bitWidth == 16) {
                *os << "  ldr w10, " << valueOperand << "\n";
                *os << "  strh w10, [x9]\n";
            } else if (bitWidth == 32) {
                *os << "  ldr w10, " << valueOperand << "\n";
                *os << "  str w10, [x9]\n";
            } else {
                *os << "  ldr x10, " << valueOperand << "\n";
                *os << "  str x10, [x9]\n";
            }
        } else {
            // Default to 64-bit store
            *os << "  ldr x10, " << valueOperand << "\n";
            *os << "  str x10, [x9]\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* value = instr.getOperands()[0]->get();
        ir::Value* ptr = instr.getOperands()[1]->get();
        const ir::Type* type = value->getType();

        emitLoadValue(cg, assembler, ptr, 9);
        emitLoadValue(cg, assembler, value, 10);
        // str x10, [x9]
        uint32_t str;
        if (type->getSize() == 1) str = 0x3900012A; // strb
        else if (type->getSize() == 2) str = 0x7900012A; // strh
        else if (type->getSize() == 4) str = 0xB900012A; // str w
        else str = 0xF900012A; // str x
        assembler.emitDWord(str);
    }
}

void AArch64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::ConstantInt* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get());
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  adrp x9, __heap_ptr\n";
        *os << "  ldr x9, [x9, :lo12:__heap_ptr]\n";
        *os << "  str x9, [x29, #" << pointerOffset << "]\n";
        *os << "  add x9, x9, #" << alignedSize << "\n";
        *os << "  adrp x10, __heap_ptr\n";
        *os << "  str x9, [x10, :lo12:__heap_ptr]\n";
    } else {
        auto& assembler = cg.getAssembler();
        // adrp x9, __heap_ptr
        assembler.emitDWord(0x90000009);
        CodeGen::RelocationInfo reloc_adrp1;
        reloc_adrp1.offset = assembler.getCodeSize() - 4;
        reloc_adrp1.symbolName = "__heap_ptr";
        reloc_adrp1.type = "R_AARCH64_ADR_PREL_PG_HI21";
        reloc_adrp1.sectionName = ".text";
        cg.addRelocation(reloc_adrp1);

        // ldr x9, [x9, :lo12:__heap_ptr]
        assembler.emitDWord(0xF9400129);
        CodeGen::RelocationInfo reloc_ldr;
        reloc_ldr.offset = assembler.getCodeSize() - 4;
        reloc_ldr.symbolName = "__heap_ptr";
        reloc_ldr.type = "R_AARCH64_LDST64_ABS_LO12_NC";
        reloc_ldr.sectionName = ".text";
        cg.addRelocation(reloc_ldr);

        // str x9, [x29, #pointerOffset]
        uint32_t str_instr = 0xF8000C09 | ((pointerOffset & 0x1FF) << 12) | (29 << 5);
        assembler.emitDWord(str_instr);

        // add x9, x9, #alignedSize
        if (alignedSize <= 4095) {
            assembler.emitDWord(0x91000129 | (alignedSize << 10));
        } else {
            // mov x10, alignedSize
            assembler.emitDWord(0xD280000A | ((alignedSize & 0xFFFF) << 5));
            // add x9, x9, x10
            assembler.emitDWord(0x8B0A0129);
        }

        // adrp x10, __heap_ptr
        assembler.emitDWord(0x9000000A);
        CodeGen::RelocationInfo reloc_adrp2;
        reloc_adrp2.offset = assembler.getCodeSize() - 4;
        reloc_adrp2.symbolName = "__heap_ptr";
        reloc_adrp2.type = "R_AARCH64_ADR_PREL_PG_HI21";
        reloc_adrp2.sectionName = ".text";
        cg.addRelocation(reloc_adrp2);

        // str x9, [x10, :lo12:__heap_ptr]
        assembler.emitDWord(0xF9000149);
        CodeGen::RelocationInfo reloc_str;
        reloc_str.offset = assembler.getCodeSize() - 4;
        reloc_str.symbolName = "__heap_ptr";
        reloc_str.type = "R_AARCH64_LDST64_ABS_LO12_NC";
        reloc_str.sectionName = ".text";
        cg.addRelocation(reloc_str);
    }
}

void AArch64::emitBr(CodeGen& cg, ir::Instruction& instr) {
    ir::BasicBlock* currentBB = instr.getParent();
    if (auto* os = cg.getTextStream()) {
        if (instr.getOperands().size() == 1) {
            // Unconditional branch
            ir::Value* targetVal = instr.getOperands()[0]->get();
            ir::BasicBlock* targetBB = dynamic_cast<ir::BasicBlock*>(targetVal);
            if (targetBB) {
                for (auto& t_instr : targetBB->getInstructions()) {
                    if (auto* phi = dynamic_cast<ir::PhiNode*>(t_instr.get())) {
                        ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                        if (incoming) {
                            std::string src = cg.getValueAsOperand(incoming);
                            std::string dst = cg.getValueAsOperand(phi);
                            if (src != dst) {
                                *os << "  ldr x9, " << src << "\n";
                                *os << "  str x9, " << dst << "\n";
                            }
                        }
                    } else break;
                }
            }
            std::string label = cg.getValueAsOperand(targetVal);
            *os << "  b " << label << "\n";
        } else {
            // Conditional branch
            ir::Value* condVal = instr.getOperands()[0]->get();
            std::string cond = cg.getValueAsOperand(condVal);
            std::string trueLabel = cg.getValueAsOperand(instr.getOperands()[1]->get());

            // Load condition and test
            if (cond[0] == '#') {
                *os << "  mov w9, " << cond << "\n";
            } else {
                *os << "  ldr w9, " << cond << "\n";
            }
            *os << "  cmp w9, #0\n";
            *os << "  b.ne " << trueLabel << "\n";

            // Handle false label if present
            if (instr.getOperands().size() > 2) {
                ir::Value* falseTarget = instr.getOperands()[2]->get();
                ir::BasicBlock* falseBB = dynamic_cast<ir::BasicBlock*>(falseTarget);
                if (falseBB) {
                    for (auto& f_instr : falseBB->getInstructions()) {
                        if (auto* phi = dynamic_cast<ir::PhiNode*>(f_instr.get())) {
                            ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                            if (incoming) {
                                std::string src = cg.getValueAsOperand(incoming);
                                std::string dst = cg.getValueAsOperand(phi);
                                if (src != dst) {
                                    *os << "  ldr x9, " << src << "\n";
                                    *os << "  str x9, " << dst << "\n";
                                }
                            }
                        } else break;
                    }
                }
                std::string falseLabel = cg.getValueAsOperand(falseTarget);
                *os << "  b " << falseLabel << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        if (instr.getOperands().size() == 1) {
            // Unconditional branch
            ir::Value* targetVal = instr.getOperands()[0]->get();
            ir::BasicBlock* targetBB = dynamic_cast<ir::BasicBlock*>(targetVal);
            if (targetBB) {
                for (auto& t_instr : targetBB->getInstructions()) {
                    if (auto* phi = dynamic_cast<ir::PhiNode*>(t_instr.get())) {
                        ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                        if (incoming) {
                            emitLoadValue(cg, assembler, incoming, 9);
                            int32_t dstOffset = cg.getStackOffset(phi);
                            uint32_t str_instr = 0xF90003A9 | ((dstOffset & 0x1FF) << 12); // str x9, [x29, #offset]
                            assembler.emitDWord(str_instr);
                        }
                    } else break;
                }
            }
            assembler.emitDWord(0x14000000); // b <offset>

            CodeGen::RelocationInfo reloc;
            reloc.offset = assembler.getCodeSize() - 4;
            if (targetBB) {
                reloc.symbolName = targetBB->getParent()->getName() + "_" + targetBB->getName();
            } else {
                reloc.symbolName = targetVal->getName();
            }
            reloc.type = "R_AARCH64_JUMP26";
            reloc.sectionName = ".text";
            reloc.addend = 0;
            cg.addRelocation(reloc);
        } else {
            // Conditional branch
            ir::Value* cond = instr.getOperands()[0]->get();
            emitLoadValue(cg, assembler, cond, 9);
            // cmp w9, #0
            assembler.emitDWord(0x7100013F);
            // b.ne <true_label>
            assembler.emitDWord(0x54000001); // b.ne (cond=1)

            CodeGen::RelocationInfo reloc_true;
            reloc_true.offset = assembler.getCodeSize() - 4;
            ir::Value* trueTarget = instr.getOperands()[1]->get();
            if (auto* bb = dynamic_cast<ir::BasicBlock*>(trueTarget)) {
                reloc_true.symbolName = bb->getParent()->getName() + "_" + bb->getName();
            } else {
                reloc_true.symbolName = trueTarget->getName();
            }
            reloc_true.type = "R_AARCH64_CONDBR19";
            reloc_true.sectionName = ".text";
            reloc_true.addend = 0;
            cg.addRelocation(reloc_true);

            if (instr.getOperands().size() > 2) {
                ir::Value* falseTarget = instr.getOperands()[2]->get();
                ir::BasicBlock* falseBB = dynamic_cast<ir::BasicBlock*>(falseTarget);
                if (falseBB) {
                    for (auto& f_instr : falseBB->getInstructions()) {
                        if (auto* phi = dynamic_cast<ir::PhiNode*>(f_instr.get())) {
                            ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                            if (incoming) {
                                emitLoadValue(cg, assembler, incoming, 9);
                                int32_t dstOffset = cg.getStackOffset(phi);
                                uint32_t str_instr = 0xF90003A9 | ((dstOffset & 0x1FF) << 12); // str x9, [x29, #offset]
                                assembler.emitDWord(str_instr);
                            }
                        } else break;
                    }
                }
                // b <false_label>
                assembler.emitDWord(0x14000000);
                CodeGen::RelocationInfo reloc_false;
                reloc_false.offset = assembler.getCodeSize() - 4;
                if (falseBB) {
                    reloc_false.symbolName = falseBB->getParent()->getName() + "_" + falseBB->getName();
                } else {
                    reloc_false.symbolName = falseTarget->getName();
                }
                reloc_false.type = "R_AARCH64_JUMP26";
                reloc_false.sectionName = ".text";
                reloc_false.addend = 0;
                cg.addRelocation(reloc_false);
            }
        }
    }
}

void AArch64::emitJmp(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* targetVal = instr.getOperands()[0]->get();
    ir::BasicBlock* targetBB = dynamic_cast<ir::BasicBlock*>(targetVal);
    ir::BasicBlock* currentBB = instr.getParent();

    if (targetBB) {
        if (auto* os = cg.getTextStream()) {
            for (auto& t_instr : targetBB->getInstructions()) {
                if (auto* phi = dynamic_cast<ir::PhiNode*>(t_instr.get())) {
                    ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                    if (incoming) {
                        std::string src = cg.getValueAsOperand(incoming);
                        std::string dst = cg.getValueAsOperand(phi);
                        if (src != dst) {
                            *os << "  ldr x9, " << src << "\n";
                            *os << "  str x9, " << dst << "\n";
                        }
                    }
                } else break;
            }
        } else {
            auto& assembler = cg.getAssembler();
            for (auto& t_instr : targetBB->getInstructions()) {
                if (auto* phi = dynamic_cast<ir::PhiNode*>(t_instr.get())) {
                    ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                    if (incoming) {
                        emitLoadValue(cg, assembler, incoming, 9);
                        int32_t dstOffset = cg.getStackOffset(phi);
                        uint32_t str_instr = 0xF90003A9 | ((dstOffset & 0x1FF) << 12);
                        assembler.emitDWord(str_instr);
                    }
                } else break;
            }
        }
    }

    if (auto* os = cg.getTextStream()) {
        std::string label = cg.getValueAsOperand(targetVal);

        // Check if this is an indirect jump (through register)
        if (label.find("[") != std::string::npos || label.find("x") == 0) {
            // Indirect jump through register
            *os << "  ldr x9, " << label << "\n";
            *os << "  br x9\n";
        } else {
            // Direct jump to label
            *os << "  b " << label << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // b <offset> (placeholder, to be filled by linker)
        assembler.emitDWord(0x14000000);

        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        if (targetBB) {
            reloc.symbolName = targetBB->getParent()->getName() + "_" + targetBB->getName();
        } else {
            reloc.symbolName = targetVal->getName();
        }
        reloc.type = "R_AARCH64_JUMP26";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);
    }
}

// === NEON/Vector Support Implementation ===

VectorCapabilities AArch64::getVectorCapabilities() const {
    VectorCapabilities caps;
    
    // NEON is standard on AArch64
    if (hasAdvancedSIMD()) {
        caps.supportedWidths.push_back(128); // NEON is 128-bit
        caps.supportsIntegerVectors = true;
        caps.supportsFloatVectors = true;
        caps.supportsDoubleVectors = true;
        caps.supportsHorizontalOps = true;
        caps.supportsFMA = true; // NEON supports fused operations
        caps.maxVectorWidth = 128;
        caps.simdExtension = "NEON";
    }
    
    // Scalable Vector Extensions (optional)
    if (hasSVE()) {
        caps.supportedWidths.push_back(256);
        caps.supportedWidths.push_back(512);
        caps.supportsMaskedOps = true;
        caps.maxVectorWidth = 2048; // SVE can be up to 2048-bit
        caps.simdExtension = "SVE";
    }
    
    return caps;
}

bool AArch64::supportsVectorWidth(unsigned width) const {
    // NEON supports 64-bit and 128-bit operations
    return width == 64 || width == 128 || (hasSVE() && width <= 2048);
}

bool AArch64::supportsVectorType(const ir::VectorType* type) const {
    if (!supportsVectorWidth(type->getBitWidth())) {
        return false;
    }
    
    // NEON supports all standard integer and floating-point element types
    auto* elemType = type->getElementType();
    if (elemType->isIntegerTy()) {
        auto* intTy = static_cast<const ir::IntegerType*>(elemType);
        unsigned bitWidth = intTy->getBitwidth();
        return bitWidth == 8 || bitWidth == 16 || bitWidth == 32 || bitWidth == 64;
    }
    
    return elemType->isFloatTy() || elemType->isDoubleTy();
}

unsigned AArch64::getOptimalVectorWidth(const ir::Type* elementType) const {
    // NEON optimal width is 128-bit for most operations
    return 128;
}

void AArch64::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string ptr = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lanes = getNEONLanes(vecType);
        *os << "  ldr x9, " << ptr << "\n";
        *os << "  ld1 {v0." << lanes << "}, [x9]\n";
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* ptr = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        emitLoadValue(cg, assembler, ptr, 9);
        // ld1 {v0.4s}, [x9] -> 0x4C407120
        uint32_t ld1 = 0x4C407120;
        if (vecType->getBitWidth() == 64) ld1 &= ~(1 << 30); // Q=0
        // Adjust for element size
        uint64_t esize = vecType->getElementType()->getSize();
        if (esize == 1) ld1 |= (0x0 << 10);
        else if (esize == 2) ld1 |= (0x1 << 10);
        else if (esize == 4) ld1 |= (0x2 << 10);
        else ld1 |= (0x3 << 10);
        assembler.emitDWord(ld1);
        // store q0
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string vec = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string ptr = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string lanes = getNEONLanes(vecType);
        *os << "  ldr q0, " << vec << "\n";
        *os << "  ldr x9, " << ptr << "\n";
        *os << "  st1 {v0." << lanes << "}, [x9]\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* ptr = instr.getOperands()[1]->get();
        // load q0
        int32_t srcOffset = cg.getStackOffset(src);
        assembler.emitDWord(0x3DC00000 | ((srcOffset & 0x1FF) << 12) | (29 << 5) | 0);
        // load x9
        emitLoadValue(cg, assembler, ptr, 9);
        // st1 {v0.4s}, [x9] -> 0x4C007120
        uint32_t st1 = 0x4C007120;
        if (vecType->getBitWidth() == 64) st1 &= ~(1 << 30);
        uint64_t esize = vecType->getElementType()->getSize();
        if (esize == 1) st1 |= (0x0 << 10);
        else if (esize == 2) st1 |= (0x1 << 10);
        else if (esize == 4) st1 |= (0x2 << 10);
        else st1 |= (0x3 << 10);
        assembler.emitDWord(st1);
    }
}

void AArch64::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lanes = getNEONLanes(vecType);
        std::string instrName;

        switch (instr.getOpcode()) {
            case ir::Instruction::VAdd: case ir::Instruction::VFAdd: instrName = vecType->isFloatingPointVector() ? "fadd" : "add"; break;
            case ir::Instruction::VSub: case ir::Instruction::VFSub: instrName = vecType->isFloatingPointVector() ? "fsub" : "sub"; break;
            case ir::Instruction::VMul: case ir::Instruction::VFMul: instrName = vecType->isFloatingPointVector() ? "fmul" : "mul"; break;
            case ir::Instruction::VDiv: case ir::Instruction::VFDiv: instrName = vecType->isFloatingPointVector() ? "fdiv" : "fdiv_err"; break;
            default: return;
        }
        *os << "  ldr q0, " << lhs << "\n";
        *os << "  ldr q1, " << rhs << "\n";
        *os << "  " << instrName << " v0." << lanes << ", v0." << lanes << ", v1." << lanes << "\n";
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        // load q0, q1
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        assembler.emitDWord(0x3DC00000 | ((lhsOffset & 0x1FF) << 12) | (29 << 5) | 0);
        assembler.emitDWord(0x3DC00000 | ((rhsOffset & 0x1FF) << 12) | (29 << 5) | 1);

        uint32_t op;
        bool is_fp = vecType->isFloatingPointVector();
        uint64_t esize = vecType->getElementType()->getSize();

        switch (instr.getOpcode()) {
            case ir::Instruction::VAdd: case ir::Instruction::VFAdd:
                op = is_fp ? 0x4E21D400 : 0x4E218400; break;
            case ir::Instruction::VSub: case ir::Instruction::VFSub:
                op = is_fp ? 0x4E21F400 : 0x4E218400; break; // Simplified, fix sub
            default: return;
        }
        if (is_fp) {
            if (esize == 8) op |= (1 << 22); // sz=1 for double
        } else {
            if (esize == 2) op |= (0x1 << 22);
            else if (esize == 4) op |= (0x2 << 22);
            else if (esize == 8) op |= (0x3 << 22);
        }
        assembler.emitDWord(op);
        // store q0
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string instrName;
        switch (instr.getOpcode()) {
            case ir::Instruction::VAnd: instrName = "and"; break;
            case ir::Instruction::VOr: instrName = "orr"; break;
            case ir::Instruction::VXor: instrName = "eor"; break;
            case ir::Instruction::VNot: instrName = "mvn"; break;
            default: return;
        }
        if (instr.getOpcode() == ir::Instruction::VNot) {
            *os << "  ldr q0, " << lhs << "\n";
            *os << "  mvn v0.16b, v0.16b\n";
        } else {
            *os << "  ldr q0, " << lhs << "\n";
            *os << "  ldr q1, " << rhs << "\n";
            *os << "  " << instrName << " v0.16b, v0.16b, v1.16b\n";
        }
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        assembler.emitDWord(0x3DC00000 | ((lhsOffset & 0x1FF) << 12) | (29 << 5) | 0);

        if (instr.getOpcode() == ir::Instruction::VNot) {
            assembler.emitDWord(0x4E205800); // mvn v0.16b, v0.16b
        } else {
            ir::Value* rhs = instr.getOperands()[1]->get();
            int32_t rhsOffset = cg.getStackOffset(rhs);
            assembler.emitDWord(0x3DC00000 | ((rhsOffset & 0x1FF) << 12) | (29 << 5) | 1);

            uint32_t op;
            switch (instr.getOpcode()) {
                case ir::Instruction::VAnd: op = 0x4E211C00; break;
                case ir::Instruction::VOr:  op = 0x4E611C00; break;
                case ir::Instruction::VXor: op = 0x4EE11C00; break;
                default: return;
            }
            assembler.emitDWord(op);
        }
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        *os << "  ldr q0, " << src << "\n";
        *os << "  mvn v0.16b, v0.16b\n";
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        assembler.emitDWord(0x3DC00000 | ((srcOffset & 0x1FF) << 12) | (29 << 5) | 0);
        // mvn v0.16b, v0.16b -> 0x4E205800
        assembler.emitDWord(0x4E205800);
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lanes = getNEONLanes(vecType);
        std::string instrName = vecType->isFloatingPointVector() ? "fcmeq" : "cmeq";
        *os << "  ldr q0, " << lhs << "\n";
        *os << "  ldr q1, " << rhs << "\n";
        *os << "  " << instrName << " v0." << lanes << ", v0." << lanes << ", v1." << lanes << "\n";
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        assembler.emitDWord(0x3DC00000 | ((lhsOffset & 0x1FF) << 12) | (29 << 5) | 0);
        assembler.emitDWord(0x3DC00000 | ((rhsOffset & 0x1FF) << 12) | (29 << 5) | 1);

        uint32_t op;
        if (vecType->isFloatingPointVector()) {
            op = 0x4E21E400; // fcmeq v0.4s, v0.4s, v1.4s
            if (vecType->getElementType()->getSize() == 8) op |= (1 << 22);
        } else {
            op = 0x4E218C00; // cmeq v0.4s, v0.4s, v1.4s
            uint64_t esize = vecType->getElementType()->getSize();
            if (esize == 2) op |= (0x1 << 22);
            else if (esize == 4) op |= (0x2 << 22);
            else if (esize == 8) op |= (0x3 << 22);
        }
        assembler.emitDWord(op);
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  // Vector shuffle operations not yet implemented for NEON\n";
    } else {
        (void)cg; (void)instr;
    }
}

void AArch64::emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string scalar = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lanes = getNEONLanes(vecType);
        std::string elemType = getNEONElementType(vecType->getElementType());

        if (vecType->isFloatingPointVector()) {
            if (vecType->getElementType()->isFloatTy()) {
                *os << "  ldr s0, " << scalar << "\n";
                *os << "  dup v0." << lanes << ", v0.s[0]\n";
            } else {
                *os << "  ldr d0, " << scalar << "\n";
                *os << "  dup v0." << lanes << ", v0.d[0]\n";
            }
        } else {
            auto* intTy = static_cast<const ir::IntegerType*>(vecType->getElementType());
            if (intTy->getBitwidth() <= 32) {
                *os << "  ldr w9, " << scalar << "\n";
                *os << "  dup v0." << lanes << ", w9\n";
            } else {
                *os << "  ldr x9, " << scalar << "\n";
                *os << "  dup v0." << lanes << ", x9\n";
            }
        }
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;

        if (vecType->isFloatingPointVector()) {
            emitLoadValue(cg, assembler, src, 0); // Load into v0
            // dup v0.4s, v0.s[0] -> 0x4E040400
            uint32_t dup = 0x4E040400;
            if (vecType->getElementType()->getSize() == 8) dup = 0x4E080400; // dup v0.2d, v0.d[0]
            assembler.emitDWord(dup);
        } else {
            emitLoadValue(cg, assembler, src, 9); // Load into x9
            // dup v0.4s, w9 -> 0x4E040C20
            uint32_t dup = 0x4E040C20;
            uint64_t esize = vecType->getElementType()->getSize();
            if (esize == 1) dup = 0x4E010C20;
            else if (esize == 2) dup = 0x4E020C20;
            else if (esize == 8) dup = 0x4E080C20; // x9
            assembler.emitDWord(dup);
        }
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  // Vector extract operations not yet implemented\n";
    } else {
        (void)cg; (void)instr;
    }
}

void AArch64::emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  // Vector insert operations not yet implemented\n";
    } else {
        (void)cg; (void)instr;
    }
}

void AArch64::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  // Error: Invalid vector type for horizontal operation\n";
            return;
        }

        // NEON supports various horizontal operations
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        std::string lanes = getNEONLanes(vecType);

        // Load source vector
        *os << "  ldr q0, " << src << "\n";

        // Example: horizontal add (pairwise add)
        if (vecType->isFloatingPointVector()) {
            *os << "  faddp v0." << lanes << ", v0." << lanes << ", v0." << lanes << "\n";
        } else {
            *os << "  addp v0." << lanes << ", v0." << lanes << ", v0." << lanes << "\n";
        }

        // Store result
        *os << "  str q0, " << dest << "\n";
    }
}

void AArch64::emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) {
    auto* vecType = instr.getVectorType();
    if (!vecType) return;

    if (auto* os = cg.getTextStream()) {
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string lanes = getNEONLanes(vecType);
        std::string instrName = vecType->isFloatingPointVector() ? "fneg" : "neg";
        *os << "  ldr q0, " << src << "\n";
        *os << "  " << instrName << " v0." << lanes << ", v0." << lanes << "\n";
        *os << "  str q0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        assembler.emitDWord(0x3DC00000 | ((srcOffset & 0x1FF) << 12) | (29 << 5) | 0);

        uint32_t op;
        if (vecType->isFloatingPointVector()) {
            op = 0x6E21F800; // fneg v0.4s
            if (vecType->getElementType()->getSize() == 8) op |= (1 << 22);
        } else {
            op = 0x4E20B800; // neg v0.4s
            uint64_t esize = vecType->getElementType()->getSize();
            if (esize == 2) op |= (0x1 << 22);
            else if (esize == 4) op |= (0x2 << 22);
            else if (esize == 8) op |= (0x3 << 22);
        }
        assembler.emitDWord(op);
        int32_t destOffset = cg.getStackOffset(dest);
        assembler.emitDWord(0x3D800000 | ((destOffset & 0x1FF) << 12) | (29 << 5) | 0);
    }
}

void AArch64::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  // Error: Invalid vector type for reduction\n";
            return;
        }

        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        std::string lanes = getNEONLanes(vecType);

        // Load source vector
        *os << "  ldr q0, " << src << "\n";

        // Perform reduction (example: add across vector)
        if (vecType->isFloatingPointVector()) {
            if (vecType->getElementType()->isFloatTy()) {
                *os << "  faddv s0, v0." << lanes << "\n";
            } else {
                *os << "  faddv d0, v0." << lanes << "\n";
            }
        } else {
            auto* intTy = static_cast<const ir::IntegerType*>(vecType->getElementType());
            if (intTy->getBitwidth() <= 32) {
                *os << "  addv s0, v0." << lanes << "\n";
            } else {
                *os << "  addv d0, v0." << lanes << "\n";
            }
        }

        // Store scalar result
        if (vecType->getElementType()->isFloatTy()) {
            *os << "  str s0, " << dest << "\n";
        } else if (vecType->getElementType()->isDoubleTy()) {
            *os << "  str d0, " << dest << "\n";
        } else {
            *os << "  str w0, " << dest << "\n";
        }
    }
}

bool AArch64::supportsFusedPattern(FusedPattern pattern) const {
    switch (pattern) {
        case FusedPattern::MultiplyAdd:
        case FusedPattern::MultiplySubtract:
            return true; // NEON supports FMA
        case FusedPattern::LoadAndOperate:
            return true; // Limited support
        case FusedPattern::AddressCalculation:
            return true; // AArch64 has flexible addressing
        default:
            return false;
    }
}

void AArch64::emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const auto& operands = instr.getOperands();
        if (operands.size() != 3) {
            *os << "  // Error: FMA requires exactly 3 operands\n";
            return;
        }
        
        std::string a = cg.getValueAsOperand(operands[0]->get());
        std::string b = cg.getValueAsOperand(operands[1]->get());
        std::string c = cg.getValueAsOperand(operands[2]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        
        if (instr.getType()->isFloatTy()) {
            // Load operands
            *os << "  ldr s0, " << a << "\n";
            *os << "  ldr s1, " << b << "\n";
            *os << "  ldr s2, " << c << "\n";

            // Fused multiply-add: s0 = s1 * s2 + s0
            *os << "  fmadd s0, s1, s2, s0\n";

            // Store result
            *os << "  str s0, " << dest << "\n";
        } else if (instr.getType()->isDoubleTy()) {
            // Load operands
            *os << "  ldr d0, " << a << "\n";
            *os << "  ldr d1, " << b << "\n";
            *os << "  ldr d2, " << c << "\n";

            // Fused multiply-add: d0 = d1 * d2 + d0
            *os << "  fmadd d0, d1, d2, d0\n";

            // Store result
            *os << "  str d0, " << dest << "\n";
        } else {
            *os << "  // FMA only supported for floating-point types on AArch64\n";
        }
    }
}

// NEON helper method implementations
std::string AArch64::getNEONRegister(const std::string& baseReg, unsigned lanes) const {
    // Extract register number
    std::string regNum = "0";
    size_t vPos = baseReg.find('v');
    if (vPos != std::string::npos) {
        regNum = baseReg.substr(vPos + 1);
    }
    
    return "v" + regNum;
}

std::string AArch64::getNEONLanes(const ir::VectorType* type) const {
    auto* elemType = type->getElementType();
    unsigned numElements = type->getNumElements();
    
    if (elemType->isFloatTy()) {
        return std::to_string(numElements) + "s"; // e.g., "4s" for 4 singles
    } else if (elemType->isDoubleTy()) {
        return std::to_string(numElements) + "d"; // e.g., "2d" for 2 doubles
    } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(elemType)) {
        switch (intTy->getBitwidth()) {
            case 8:  return std::to_string(numElements) + "b";
            case 16: return std::to_string(numElements) + "h";
            case 32: return std::to_string(numElements) + "s";
            case 64: return std::to_string(numElements) + "d";
            default: return std::to_string(numElements) + "s";
        }
    }
    
    return "4s"; // Default
}

std::string AArch64::getNEONElementType(const ir::Type* elementType) const {
    if (elementType->isFloatTy()) {
        return "s"; // single-precision float
    } else if (elementType->isDoubleTy()) {
        return "d"; // double-precision float
    } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(elementType)) {
        switch (intTy->getBitwidth()) {
            case 8:  return "b";
            case 16: return "h";
            case 32: return "s";
            case 64: return "d";
            default: return "s";
        }
    }
    return "s";
}

std::string AArch64::getNEONInstruction(const std::string& baseInstr, const ir::VectorType* type) const {
    // For NEON, most instructions don't need prefix modification
    // The lane specification (e.g., .4s, .2d) handles the type
    return baseInstr;
}

} // namespace target
} // namespace codegen
