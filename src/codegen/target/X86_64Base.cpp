#include "codegen/target/X86_64Base.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "ir/SIMDInstruction.h"
#include "ir/Use.h"
#include "ir/Function.h"
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace codegen {
namespace target {

X86_64Base::X86_64Base() {
    // initRegisters() is pure virtual, should be called by derived classes.
}

TypeInfo X86_64Base::getTypeInfo(const ir::Type* type) const {
    TypeInfo info;
    
    switch (type->getTypeID()) {
        case ir::Type::VoidTyID:
            info.size = 0;
            info.align = 8;
            info.regClass = RegisterClass::Integer;
            info.isFloatingPoint = false;
            info.isSigned = false;
            break;
            
        case ir::Type::IntegerTyID: {
            auto* intTy = static_cast<const ir::IntegerType*>(type);
            info.size = intTy->getBitwidth();
            info.align = std::min(static_cast<uint64_t>(8), (info.size + 7) / 8); // Round up to bytes, max 8
            info.regClass = RegisterClass::Integer;
            info.isFloatingPoint = false;
            info.isSigned = intTy->isSigned();
            break;
        }
            
        case ir::Type::FloatTyID:
            info.size = 32;
            info.align = 4;
            info.regClass = RegisterClass::Float;
            info.isFloatingPoint = true;
            info.isSigned = true;
            break;
            
        case ir::Type::DoubleTyID:
            info.size = 64;
            info.align = 8;
            info.regClass = RegisterClass::Float;
            info.isFloatingPoint = true;
            info.isSigned = true;
            break;
            
        case ir::Type::PointerTyID:
            info.size = 64; // 64-bit pointers
            info.align = 8;
            info.regClass = RegisterClass::Integer;
            info.isFloatingPoint = false;
            info.isSigned = false;
            break;
            
        case ir::Type::VectorTyID: {
            auto* vecTy = static_cast<const ir::VectorType*>(type);
            info.size = vecTy->getBitWidth();
            info.align = std::min(static_cast<uint64_t>(64), info.size / 8); // Max 64-byte alignment
            info.regClass = RegisterClass::Vector;
            info.isFloatingPoint = vecTy->isFloatingPointVector();
            info.isSigned = vecTy->getElementType()->isInteger();
            break;
        }
            
        default:
            throw std::runtime_error("Unsupported type in X86_64Base::getTypeInfo");
    }
    
    return info;
}

const std::vector<std::string>& X86_64Base::getRegisters(RegisterClass regClass) const {
    switch (regClass) {
        case RegisterClass::Integer: return integerRegs;
        case RegisterClass::Float: return floatRegs;
        case RegisterClass::Vector: return vectorRegs;
        default: 
            throw std::runtime_error("Unsupported register class");
    }
}

const std::string& X86_64Base::getReturnRegister(const ir::Type* type) const {
    if (!type->isFloatingPoint()) {
        return intReturnReg;
    } else {
        return floatReturnReg;
    }
}

void X86_64Base::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        // Save the frame pointer
        *os << "  pushq %" << framePtrReg << "\n";

        // Set up new frame pointer
        *os << "  movq %" << stackPtrReg << ", %" << framePtrReg << "\n";

        // Allocate stack space if needed
        if (stack_size > 0) {
            *os << "  subq $" << stack_size << ", %" << stackPtrReg << "\n";
        }

        // Save callee-saved registers
        for (const auto& reg : calleeSaved) {
            if (reg.second) {
                *os << "  pushq %" << reg.first << "\n";
            }
        }
    }
}

void X86_64Base::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        // Restore callee-saved registers in reverse order
        std::vector<std::string> regsToRestore;
        for (const auto& reg : calleeSaved) {
            if (reg.second) {
                regsToRestore.push_back(reg.first);
            }
        }
        std::reverse(regsToRestore.begin(), regsToRestore.end());
        for (const auto& reg : regsToRestore) {
            *os << "  popq %" << reg << "\n";
        }

        // Restore stack pointer and frame pointer
        *os << "  movq %" << framePtrReg << ", %" << stackPtrReg << "\n";
        *os << "  popq %" << framePtrReg << "\n";
        *os << "  ret\n";
    }
}

void X86_64Base::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // Function label
        *os << func.getName() << ":\n";
    }
    
    // Calculate stack size needed for local variables
    // This is a simplified version - a real implementation would analyze the function body
    size_t stackSize = 0;
    for (auto& arg : func.getParameters()) {
        stackSize += (getTypeInfo(arg->getType()).size + 7) / 8; // Round up to bytes
    }
    
    // Align stack size
    stackSize = (stackSize + 15) & ~15; // Align to 16 bytes
    
    // Emit prologue
    emitPrologue(cg, stackSize);
}

void X86_64Base::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    emitEpilogue(cg);
}

bool X86_64Base::isCallerSaved(const std::string& reg) const {
    return callerSaved.count(reg) > 0 && callerSaved.at(reg);
}

bool X86_64Base::isCalleeSaved(const std::string& reg) const {
    return calleeSaved.count(reg) > 0 && calleeSaved.at(reg);
}

void X86_64Base::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        if (!instr.getOperands().empty()) {
            std::string retval = cg.getValueAsOperand(instr.getOperands()[0]->get());
            const auto* type = instr.getOperands()[0]->get()->getType();

            if (type->isFloatingPoint()) {
                *os << "  movsd " << retval << ", %xmm0\n";
            } else {
                *os << "  movq " << retval << ", %rax\n";
            }
        }
    }
    
    emitEpilogue(cg);
}

void X86_64Base::emitCall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // This is a simplified version - a real implementation would handle argument passing
        // according to the ABI

        // Save caller-saved registers
        for (const auto& reg : callerSaved) {
            if (reg.second) {
                *os << "  pushq %" << reg.first << "\n";
            }
        }

        // Push arguments (in reverse order for cdecl)
        for (size_t i = instr.getOperands().size() - 1; i > 0; --i) {
            std::string arg = cg.getValueAsOperand(instr.getOperands()[i]->get());
            *os << "  pushq " << arg << "\n";
        }

        // Call the function
        *os << "  call " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";

        // Clean up arguments
        if (instr.getOperands().size() > 1) {
            *os << "  addq $" << (8 * (instr.getOperands().size() - 1)) << ", %rsp\n";
        }

        // Restore caller-saved registers
        std::vector<std::string> regsToRestore;
        for (const auto& reg : callerSaved) {
            if (reg.second) {
                regsToRestore.push_back(reg.first);
            }
        }
        std::reverse(regsToRestore.begin(), regsToRestore.end());
        for (const auto& reg : regsToRestore) {
            *os << "  popq %" << reg << "\n";
        }

        // Store return value if needed
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string dest = cg.getValueAsOperand(&instr);
            if (instr.getType()->isFloatingPoint()) {
                *os << "  movsd %xmm0, " << dest << "\n";
            } else {
                *os << "  movq %rax, " << dest << "\n";
            }
        }
    }
}

std::string X86_64Base::getLoadStoreSuffix(const ir::Type* type) const {
    switch (type->getTypeID()) {
        case ir::Type::IntegerTyID: {
            unsigned bits = static_cast<const ir::IntegerType*>(type)->getBitwidth();
            if (bits <= 8) return "b";
            if (bits <= 16) return "w";
            if (bits <= 32) return "l";
            return "q";
        }
        case ir::Type::FloatTyID: return "ss";
        case ir::Type::DoubleTyID: return "sd";
        case ir::Type::PointerTyID: return "q";
        default: return "q";
    }
}

std::string X86_64Base::getMoveInstruction(const ir::Type* type) const {
    if (type->isFloatingPoint()) {
        return type->isDoubleTy() ? "movsd" : "movss";
    } else {
        return "mov" + getLoadStoreSuffix(type);
    }
}

std::string X86_64Base::getRegisterName(const std::string& baseReg, const ir::Type* type) const {
    if (type->isFloatingPoint()) return baseReg;
    if (type->isPointerTy()) return baseReg;

    unsigned bits = 64;
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        bits = intTy->getBitwidth();
    }

    if (bits >= 64) return baseReg;

    if (bits <= 8) {
        if (baseReg == "%rax") return "%al";
        if (baseReg == "%rbx") return "%bl";
        if (baseReg == "%rcx") return "%cl";
        if (baseReg == "%rdx") return "%dl";
        if (baseReg == "%rsi") return "%sil";
        if (baseReg == "%rdi") return "%dil";
        if (baseReg == "%rbp") return "%bpl";
        if (baseReg == "%rsp") return "%spl";
        return baseReg + "b";
    }
    if (bits <= 16) {
        if (baseReg == "%rax") return "%ax";
        if (baseReg == "%rbx") return "%bx";
        if (baseReg == "%rcx") return "%cx";
        if (baseReg == "%rdx") return "%dx";
        if (baseReg == "%rsi") return "%si";
        if (baseReg == "%rdi") return "%di";
        if (baseReg == "%rbp") return "%bp";
        if (baseReg == "%rsp") return "%sp";
        return baseReg + "w";
    }
    if (bits <= 32) {
        if (baseReg == "%rax") return "%eax";
        if (baseReg == "%rbx") return "%ebx";
        if (baseReg == "%rcx") return "%ecx";
        if (baseReg == "%rdx") return "%edx";
        if (baseReg == "%rsi") return "%esi";
        if (baseReg == "%rdi") return "%edi";
        if (baseReg == "%rbp") return "%ebp";
        if (baseReg == "%rsp") return "%esp";
        return baseReg + "d";
    }
    return baseReg;
}

void X86_64Base::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // On x86_64, va_end is typically a no-op
        *os << "  # va_end - no operation needed on x86_64\n";
    }
}

// === SIMD/Vector Support Implementation ===

VectorCapabilities X86_64Base::getVectorCapabilities() const {
    VectorCapabilities caps;
    
    // Base SSE support
    if (hasSSE()) {
        caps.supportedWidths.push_back(128);
        caps.supportsFloatVectors = true;
    }
    if (hasSSE2()) {
        caps.supportsIntegerVectors = true;
        caps.supportsDoubleVectors = true;
    }
    
    // AVX support
    if (hasAVX()) {
        caps.supportedWidths.push_back(256);
        caps.simdExtension = "AVX";
    }
    
    // AVX-512 support
    if (hasAVX512()) {
        caps.supportedWidths.push_back(512);
        caps.supportsMaskedOps = true;
        caps.supportsGatherScatter = true;
    }
    
    // FMA support
    if (hasFMA()) {
        caps.supportsFMA = true;
    }
    
    // Horizontal operations (SSE3+)
    if (hasSSE3()) {
        caps.supportsHorizontalOps = true;
    }
    
    caps.maxVectorWidth = caps.supportedWidths.empty() ? 0 : 
        *std::max_element(caps.supportedWidths.begin(), caps.supportedWidths.end());
    
    return caps;
}

bool X86_64Base::supportsVectorWidth(unsigned width) const {
    auto caps = getVectorCapabilities();
    return std::find(caps.supportedWidths.begin(), caps.supportedWidths.end(), width) 
           != caps.supportedWidths.end();
}

bool X86_64Base::supportsVectorType(const ir::VectorType* type) const {
    if (!supportsVectorWidth(type->getBitWidth())) {
        return false;
    }
    
    auto caps = getVectorCapabilities();
    
    if (type->isIntegerVector() && !caps.supportsIntegerVectors) {
        return false;
    }
    
    if (type->isFloatingPointVector()) {
        if (type->getElementType()->isFloatTy() && !caps.supportsFloatVectors) {
            return false;
        }
        if (type->getElementType()->isDoubleTy() && !caps.supportsDoubleVectors) {
            return false;
        }
    }
    
    return true;
}

unsigned X86_64Base::getOptimalVectorWidth(const ir::Type* elementType) const {
    auto caps = getVectorCapabilities();
    
    if (caps.supportedWidths.empty()) {
        return 0; // No SIMD support
    }
    
    // For floating-point, prefer wider vectors if available
    if (elementType->isFloatingPoint() && hasAVX()) {
        return 256;
    }
    
    // Default to SSE width
    return 128;
}

void X86_64Base::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  # Error: Invalid vector type for load\n";
            return;
        }

        std::string ptr = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        SIMDContext ctx; // dummy
        std::string loadInstr;

        if (vecType->isFloatingPointVector()) {
            if (ctx.vectorWidth <= 128) {
                loadInstr = vecType->getElementType()->isFloatTy() ? "movups" : "movupd";
            } else {
                loadInstr = vecType->getElementType()->isFloatTy() ? "vmovups" : "vmovupd";
            }
        } else {
            loadInstr = ctx.vectorWidth <= 128 ? "movdqu" : "vmovdqu";
        }

        std::string reg = getVectorRegister("%xmm0", ctx.vectorWidth);
        *os << "  " << loadInstr << " " << ptr << ", " << reg << "\n";
        *os << "  " << loadInstr.substr(0, loadInstr.find('u')) << "a " << reg << ", " << dest << "\n";
    }
}

void X86_64Base::emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  # Error: Invalid vector type for store\n";
            return;
        }

        std::string vec = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string ptr = cg.getValueAsOperand(instr.getOperands()[1]->get());

        SIMDContext ctx; // dummy
        std::string storeInstr;

        if (vecType->isFloatingPointVector()) {
            if (ctx.vectorWidth <= 128) {
                storeInstr = vecType->getElementType()->isFloatTy() ? "movups" : "movupd";
            } else {
                storeInstr = vecType->getElementType()->isFloatTy() ? "vmovups" : "vmovupd";
            }
        } else {
            storeInstr = ctx.vectorWidth <= 128 ? "movdqu" : "vmovdqu";
        }

        std::string reg = getVectorRegister("%xmm0", ctx.vectorWidth);
        *os << "  " << storeInstr.substr(0, storeInstr.find('u')) << "a " << vec << ", " << reg << "\n";
        *os << "  " << storeInstr << " " << reg << ", " << ptr << "\n";
    }
}

void X86_64Base::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  # Error: Invalid vector type for arithmetic\n";
            return;
        }

        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        SIMDContext ctx; // dummy
        std::string instrName;

        // Determine instruction based on opcode and type
        switch (instr.getOpcode()) {
            case ir::Instruction::VAdd:
            case ir::Instruction::VFAdd:
                if (vecType->isFloatingPointVector()) {
                    instrName = ctx.vectorWidth <= 128 ? "addps" : "vaddps";
                    if (vecType->getElementType()->isDoubleTy()) {
                        instrName = ctx.vectorWidth <= 128 ? "addpd" : "vaddpd";
                    }
                } else {
                    instrName = getVectorInstruction("padd", ctx);
                    if (ctx.vectorWidth > 128) instrName = "v" + instrName;
                }
                break;

            case ir::Instruction::VSub:
            case ir::Instruction::VFSub:
                if (vecType->isFloatingPointVector()) {
                    instrName = ctx.vectorWidth <= 128 ? "subps" : "vsubps";
                    if (vecType->getElementType()->isDoubleTy()) {
                        instrName = ctx.vectorWidth <= 128 ? "subpd" : "vsubpd";
                    }
                } else {
                    instrName = getVectorInstruction("psub", ctx);
                    if (ctx.vectorWidth > 128) instrName = "v" + instrName;
                }
                break;

            case ir::Instruction::VMul:
            case ir::Instruction::VFMul:
                if (vecType->isFloatingPointVector()) {
                    instrName = ctx.vectorWidth <= 128 ? "mulps" : "vmulps";
                    if (vecType->getElementType()->isDoubleTy()) {
                        instrName = ctx.vectorWidth <= 128 ? "mulpd" : "vmulpd";
                    }
                } else {
                    instrName = getVectorInstruction("pmull", ctx);
                    if (ctx.vectorWidth > 128) instrName = "v" + instrName;
                }
                break;

            case ir::Instruction::VDiv:
            case ir::Instruction::VFDiv:
                if (vecType->isFloatingPointVector()) {
                    instrName = ctx.vectorWidth <= 128 ? "divps" : "vdivps";
                    if (vecType->getElementType()->isDoubleTy()) {
                        instrName = ctx.vectorWidth <= 128 ? "divpd" : "vdivpd";
                    }
                } else {
                    *os << "  # Integer vector division not directly supported\n";
                    return;
                }
                break;

            default:
                *os << "  # Unsupported vector arithmetic operation\n";
                return;
        }

        std::string reg1 = getVectorRegister("%xmm0", ctx.vectorWidth);
        std::string reg2 = getVectorRegister("%xmm1", ctx.vectorWidth);

        // Load operands
        std::string loadInstr = ctx.vectorWidth <= 128 ? "movaps" : "vmovaps";
        *os << "  " << loadInstr << " " << lhs << ", " << reg1 << "\n";
        *os << "  " << loadInstr << " " << rhs << ", " << reg2 << "\n";

        // Perform operation
        if (ctx.vectorWidth <= 128) {
            *os << "  " << instrName << " " << reg2 << ", " << reg1 << "\n";
        } else {
            *os << "  " << instrName << " " << reg1 << ", " << reg2 << ", " << reg1 << "\n";
        }

        // Store result
        *os << "  " << loadInstr << " " << reg1 << ", " << dest << "\n";
    }
}

void X86_64Base::emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        *os << "  # Vector Negation (simplified)\n";
        *os << "  pxor %xmm0, %xmm0\n";
        *os << "  psubd " << src << ", %xmm0\n";
        *os << "  movaps %xmm0, " << dest << "\n";
    }
}

void X86_64Base::emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        *os << "  pcmpeqd %xmm0, %xmm0\n";
        *os << "  pxor " << src << ", %xmm0\n";
        *os << "  movaps %xmm0, " << dest << "\n";
    }
}

void X86_64Base::emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        auto* vecType = instr.getVectorType();
        if (!vecType) {
            *os << "  # Error: Invalid vector type for logical operation\n";
            return;
        }

        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        SIMDContext ctx; // dummy
        std::string instrName;

        switch (instr.getOpcode()) {
            case ir::Instruction::VAnd:
                instrName = ctx.vectorWidth <= 128 ? "pand" : "vpand";
                break;
            case ir::Instruction::VOr:
                instrName = ctx.vectorWidth <= 128 ? "por" : "vpor";
                break;
            case ir::Instruction::VXor:
                instrName = ctx.vectorWidth <= 128 ? "pxor" : "vpxor";
                break;
            default:
                *os << "  # Unsupported vector logical operation\n";
                return;
        }

        std::string reg1 = getVectorRegister("%xmm0", ctx.vectorWidth);
        std::string reg2 = getVectorRegister("%xmm1", ctx.vectorWidth);

        std::string loadInstr = ctx.vectorWidth <= 128 ? "movdqa" : "vmovdqa";
        *os << "  " << loadInstr << " " << lhs << ", " << reg1 << "\n";
        *os << "  " << loadInstr << " " << rhs << ", " << reg2 << "\n";

        if (ctx.vectorWidth <= 128) {
            *os << "  " << instrName << " " << reg2 << ", " << reg1 << "\n";
        } else {
            *os << "  " << instrName << " " << reg1 << ", " << reg2 << ", " << reg1 << "\n";
        }

        *os << "  " << loadInstr << " " << reg1 << ", " << dest << "\n";
    }
}

void X86_64Base::emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Vector comparison operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Vector shuffle operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string scalar = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        if (vecType->isFloatingPointVector()) {
            std::string brInstr = vecType->getElementType()->isFloatTy() ? "vbroadcastss" : "vbroadcastsd";
            *os << "  " << brInstr << " " << dest << ", " << scalar << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        if (instr.getVectorType()->isFloatingPointVector()) {
            uint8_t prefix = (instr.getVectorType()->getElementType()->getSize() == 4) ? 0xF3 : 0xF2;
            // Load scalar to xmm0
            uint8_t op_ldr = 0x10; // movss/sd
            if (prefix) assembler.emitByte(prefix);
            assembler.emitBytes({0x0F, op_ldr});
            if (srcOffset >= -128 && srcOffset <= 127) {
                assembler.emitByte(0x45); assembler.emitByte(static_cast<int8_t>(srcOffset));
            } else {
                assembler.emitByte(0x85); assembler.emitDWord(static_cast<uint32_t>(srcOffset));
            }

            // shufps xmm0, xmm0, 0
            if (instr.getVectorType()->getElementType()->getSize() == 4)
                assembler.emitBytes({0x0F, 0xC6, 0xC0, 0x00});
            else
                assembler.emitBytes({0x66, 0x0F, 0xC6, 0xC0, 0x00});

            // Store xmm0 to dest
            assembler.emitBytes({0x0F, 0x29});
            if (destOffset >= -128 && destOffset <= 127) {
                assembler.emitByte(0x45); assembler.emitByte(static_cast<int8_t>(destOffset));
            } else {
                assembler.emitByte(0x85); assembler.emitDWord(static_cast<uint32_t>(destOffset));
            }
        }
    }
}

void X86_64Base::emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Vector extract operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Vector insert operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorGather(CodeGen& cg, ir::VectorInstruction& instr) {
    if (!hasAVX2()) {
        // Fall back to base implementation
        return;
    }
    
    if (auto* os = cg.getTextStream()) {
        *os << "  # AVX2 gather operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorScatter(CodeGen& cg, ir::VectorInstruction& instr) {
    if (!hasAVX512()) {
        // Fall back to base implementation
        return;
    }
    
    if (auto* os = cg.getTextStream()) {
        *os << "  # AVX-512 scatter operations not yet implemented\n";
    }
}

void X86_64Base::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) {
    if (!hasSSE3()) {
        return;
    }
    
    if (auto* os = cg.getTextStream()) {
        *os << "  # Horizontal operations not yet implemented\n";
    }
}

void X86_64Base::emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) {
    if (!hasFMA()) {
        return;
    }
    
    if (auto* os = cg.getTextStream()) {
        const auto& operands = instr.getOperands();
        if (operands.size() != 3) {
            *os << "  # Error: FMA requires exactly 3 operands\n";
            return;
        }

        std::string a = cg.getValueAsOperand(operands[0]->get());
        std::string b = cg.getValueAsOperand(operands[1]->get());
        std::string c = cg.getValueAsOperand(operands[2]->get());
        std::string dest = cg.getValueAsOperand(&instr);

        std::string fmaInstr;
        if (instr.getType()->isFloatTy()) {
            fmaInstr = "vfmadd213ss"; // a * b + c -> dest
        } else if (instr.getType()->isDoubleTy()) {
            fmaInstr = "vfmadd213sd";
        } else {
            *os << "  # FMA only supported for floating-point types\n";
            return;
        }

        // Load operands into registers
        *os << "  vmovss " << a << ", %xmm0\n";
        *os << "  vmovss " << b << ", %xmm1\n";
        *os << "  vmovss " << c << ", %xmm2\n";

        // Perform FMA: xmm0 = xmm0 * xmm1 + xmm2
        *os << "  " << fmaInstr << " %xmm2, %xmm1, %xmm0\n";

        // Store result
        *os << "  vmovss %xmm0, " << dest << "\n";
    }
}

// SIMD helper implementations
std::string X86_64Base::getVectorRegister(const std::string& baseReg, unsigned width) const {
    // Extract register number from base register
    std::string regNum = "0";
    size_t numPos = baseReg.find_first_of("0123456789");
    if (numPos != std::string::npos) {
        regNum = baseReg.substr(numPos);
    }
    
    std::string prefix = getVectorRegisterPrefix(width);
    return "%" + prefix + "mm" + regNum;
}

std::string X86_64Base::getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const {
    std::string result = baseInstr;
    
    // Add vector width prefix for AVX+
    if (ctx.vectorWidth > 128) {
        result = "v" + result;
    }
    
    // Add element suffix if needed
    if (!ctx.elementSuffix.empty()) {
        result += ctx.elementSuffix;
    }
    
    return result;
}

std::string X86_64Base::getVectorSuffix(const ir::VectorType* type) const {
    if (type->getElementType()->isFloatTy()) {
        return "ps"; // Packed single-precision
    } else if (type->getElementType()->isDoubleTy()) {
        return "pd"; // Packed double-precision
    } else if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type->getElementType())) {
        switch (intTy->getBitwidth()) {
            case 8:  return "epi8";
            case 16: return "epi16";
            case 32: return "epi32";
            case 64: return "epi64";
            default: return "epi32";
        }
    }
    return "dq"; // Default
}

std::string X86_64Base::getVectorRegisterPrefix(unsigned width) const {
    if (width <= 128) {
        return "x"; // XMM registers
    } else if (width <= 256) {
        return "y"; // YMM registers
    } else {
        return "z"; // ZMM registers
    }
}

std::string X86_64Base::allocateVectorRegister(unsigned width) const {
    // Simplified register allocation - in practice this would be more sophisticated
    return getVectorRegister("%xmm0", width);
}

void X86_64Base::deallocateVectorRegister(const std::string& reg) const {
    // Simplified - in practice this would update register allocation state
}

std::string X86_64Base::formatStackOperand(int offset) const {
    return std::to_string(offset) + "(%" + framePtrReg + ")";
}

std::string X86_64Base::formatGlobalOperand(const std::string& name) const {
    return name + "(%rip)";
}

bool X86_64Base::supportsFusedPattern(FusedPattern pattern) const {
    switch (pattern) {
        case FusedPattern::MultiplyAdd:
            return hasFMA();
        case FusedPattern::MultiplySubtract:
            return hasFMA();
        case FusedPattern::LoadAndOperate:
            return true;
        case FusedPattern::CompareAndBranch:
            return true;
        case FusedPattern::AddressCalculation:
            return true;
        default:
            return false;
    }
}

} // namespace target
} // namespace codegen