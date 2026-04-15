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

size_t AArch64::getMaxRegistersForArgs() const {
    return 8; // x0-x7 or v0-v7
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
    void AArch64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.read") {
        *os << "  mov x8, #63\n"; // read
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.write") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.close") {
        *os << "  mov x8, #57\n"; // close
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.seek") {
        *os << "  mov x8, #62\n"; // lseek
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.flush") {
        *os << "  mov x8, #82\n"; // fsync
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.stat") {
        *os << "  mov x8, #80\n"; // fstat
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  svc #0\n";
    void AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  mov x8, #93\n"; // exit
        if (!instr.getOperands().empty()) *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "process.abort") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
        *os << "  mov x1, #6\n"; // SIGABRT
        *os << "  mov x8, #129\n"; // kill
        *os << "  svc #0\n";
    } else if (cap == "process.spawn") {
        *os << "  mov x8, #220\n"; // clone (as fork)
        *os << "  mov x0, #17\n"; // SIGCHLD
        *os << "  mov x1, #0\n";
        *os << "  svc #0\n";
        *os << "  cmp x0, #0\n";
        *os << "  b.ne .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  mov x8, #221\n"; // execve
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x8, #93\n"; // exit
        *os << "  mov x0, #1\n";
        *os << "  svc #0\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
    } else if (cap == "process.sleep") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #1000\n";
        *os << "  udiv x2, x0, x1\n"; // sec
        *os << "  msub x3, x2, x1, x0\n"; // ms
        *os << "  mov x4, #1000000\n";
        *os << "  mul x3, x3, x4\n"; // nsec
        *os << "  str x2, [sp]\n";
        *os << "  str x3, [sp, #8]\n";
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #0\n";
        *os << "  mov x8, #101\n"; // nanosleep
        *os << "  svc #0\n";
        *os << "  add sp, sp, #16\n";
    void AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.load") {
        *os << "  ldr x0, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.store") {
        *os << "  str " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.fence") {
        *os << "  dmb ish\n";
    void AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") {
        *os << "  mov x8, #198\n"; // socket
        *os << "  mov x0, #2\n"; // AF_INET
        *os << "  mov x1, #1\n"; // SOCK_STREAM
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x8, #203\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0\n";
        *os << "  mov x0, x9\n";
    } else if (cap == "net.send" || cap == "ipc.send") {
        *os << "  mov x8, #206\n"; // sendto
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    } else if (cap == "net.recv" || cap == "ipc.recv") {
        *os << "  mov x8, #207\n"; // recvfrom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
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

void AArch64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const std::string& cap = ei->getCapability();

    if (cap.compare(0, 3, "io.") == 0) emitIOCall(cg, instr, cap);
    else if (cap.compare(0, 3, "fs.") == 0) emitFSCall(cg, instr, cap);
    else if (cap.compare(0, 7, "memory.") == 0) emitMemoryCall(cg, instr, cap);
    else if (cap.compare(0, 8, "process.") == 0) emitProcessCall(cg, instr, cap);
    else if (cap.compare(0, 7, "thread.") == 0) emitThreadCall(cg, instr, cap);
    else if (cap.compare(0, 5, "sync.") == 0) emitSyncCall(cg, instr, cap);
    else if (cap.compare(0, 5, "time.") == 0) emitTimeCall(cg, instr, cap);
    else if (cap.compare(0, 6, "event.") == 0) emitEventCall(cg, instr, cap);
    else if (cap.compare(0, 4, "net.") == 0) emitNetCall(cg, instr, cap);
    else if (cap.compare(0, 4, "ipc.") == 0) emitIPCCall(cg, instr, cap);
    else if (cap.compare(0, 4, "env.") == 0) emitEnvCall(cg, instr, cap);
    else if (cap.compare(0, 7, "system.") == 0) emitSystemCall(cg, instr, cap);
    else if (cap.compare(0, 7, "signal.") == 0) emitSignalCall(cg, instr, cap);
    else if (cap.compare(0, 7, "random.") == 0) emitRandomCall(cg, instr, cap);
    else if (cap.compare(0, 6, "error.") == 0) emitErrorCall(cg, instr, cap);
    else if (cap.compare(0, 6, "debug.") == 0) emitDebugCall(cg, instr, cap);
    else if (cap.compare(0, 7, "module.") == 0) emitModuleCall(cg, instr, cap);
    else if (cap.compare(0, 4, "tty.") == 0) emitTTYCall(cg, instr, cap);
    else if (cap.compare(0, 9, "security.") == 0) emitSecurityCall(cg, instr, cap);
    else if (cap.compare(0, 4, "gpu.") == 0) emitGPUCall(cg, instr, cap);

    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void AArch64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.read") {
        *os << "  mov x8, #63\n"; // read
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.write") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.close") {
        *os << "  mov x8, #57\n"; // close
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.seek") {
        *os << "  mov x8, #62\n"; // lseek
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.flush") {
        *os << "  mov x8, #82\n"; // fsync
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "io.stat") {
        *os << "  mov x8, #80\n"; // fstat
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  svc #0\n";
    void AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  mov x8, #93\n"; // exit
        if (!instr.getOperands().empty()) *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "process.abort") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
        *os << "  mov x1, #6\n"; // SIGABRT
        *os << "  mov x8, #129\n"; // kill
        *os << "  svc #0\n";
    } else if (cap == "process.spawn") {
        *os << "  mov x8, #220\n"; // clone (as fork)
        *os << "  mov x0, #17\n"; // SIGCHLD
        *os << "  mov x1, #0\n";
        *os << "  svc #0\n";
        *os << "  cmp x0, #0\n";
        *os << "  b.ne .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  mov x8, #221\n"; // execve
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x8, #93\n"; // exit
        *os << "  mov x0, #1\n";
        *os << "  svc #0\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
    } else if (cap == "process.sleep") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #1000\n";
        *os << "  udiv x2, x0, x1\n"; // sec
        *os << "  msub x3, x2, x1, x0\n"; // ms
        *os << "  mov x4, #1000000\n";
        *os << "  mul x3, x3, x4\n"; // nsec
        *os << "  str x2, [sp]\n";
        *os << "  str x3, [sp, #8]\n";
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #0\n";
        *os << "  mov x8, #101\n"; // nanosleep
        *os << "  svc #0\n";
        *os << "  add sp, sp, #16\n";
    void AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.load") {
        *os << "  ldr x0, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.store") {
        *os << "  str " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.fence") {
        *os << "  dmb ish\n";
    void AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") {
        *os << "  mov x8, #198\n"; // socket
        *os << "  mov x0, #2\n"; // AF_INET
        *os << "  mov x1, #1\n"; // SOCK_STREAM
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x8, #203\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0\n";
        *os << "  mov x0, x9\n";
    } else if (cap == "net.send" || cap == "ipc.send") {
        *os << "  mov x8, #206\n"; // sendto
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    } else if (cap == "net.recv" || cap == "ipc.recv") {
        *os << "  mov x8, #207\n"; // recvfrom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  mov x8, #93\n"; // exit
        if (!instr.getOperands().empty()) *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0\n";
    } else if (cap == "process.abort") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
        *os << "  mov x1, #6\n"; // SIGABRT
        *os << "  mov x8, #129\n"; // kill
        *os << "  svc #0\n";
    } else if (cap == "process.spawn") {
        *os << "  mov x8, #220\n"; // clone (as fork)
        *os << "  mov x0, #17\n"; // SIGCHLD
        *os << "  mov x1, #0\n";
        *os << "  svc #0\n";
        *os << "  cmp x0, #0\n";
        *os << "  b.ne .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  mov x8, #221\n"; // execve
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x8, #93\n"; // exit
        *os << "  mov x0, #1\n";
        *os << "  svc #0\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  mov x8, #172\n"; // getpid
        *os << "  svc #0\n";
    } else if (cap == "process.sleep") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #1000\n";
        *os << "  udiv x2, x0, x1\n"; // sec
        *os << "  msub x3, x2, x1, x0\n"; // ms
        *os << "  mov x4, #1000000\n";
        *os << "  mul x3, x3, x4\n"; // nsec
        *os << "  str x2, [sp]\n";
        *os << "  str x3, [sp, #8]\n";
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #0\n";
        *os << "  mov x8, #101\n"; // nanosleep
        *os << "  svc #0\n";
        *os << "  add sp, sp, #16\n";
    void AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.load") {
        *os << "  ldr x0, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.store") {
        *os << "  str " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.fence") {
        *os << "  dmb ish\n";
    void AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") {
        *os << "  mov x8, #198\n"; // socket
        *os << "  mov x0, #2\n"; // AF_INET
        *os << "  mov x1, #1\n"; // SOCK_STREAM
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x8, #203\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0\n";
        *os << "  mov x0, x9\n";
    } else if (cap == "net.send" || cap == "ipc.send") {
        *os << "  mov x8, #206\n"; // sendto
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    } else if (cap == "net.recv" || cap == "ipc.recv") {
        *os << "  mov x8, #207\n"; // recvfrom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.load") {
        *os << "  ldr x0, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.atomic.store") {
        *os << "  str " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "sync.fence") {
        *os << "  dmb ish\n";
    void AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") {
        *os << "  mov x8, #198\n"; // socket
        *os << "  mov x0, #2\n"; // AF_INET
        *os << "  mov x1, #1\n"; // SOCK_STREAM
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x8, #203\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0\n";
        *os << "  mov x0, x9\n";
    } else if (cap == "net.send" || cap == "ipc.send") {
        *os << "  mov x8, #206\n"; // sendto
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    } else if (cap == "net.recv" || cap == "ipc.recv") {
        *os << "  mov x8, #207\n"; // recvfrom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") {
        *os << "  mov x8, #198\n"; // socket
        *os << "  mov x0, #2\n"; // AF_INET
        *os << "  mov x1, #1\n"; // SOCK_STREAM
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x8, #203\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0\n";
        *os << "  mov x0, x9\n";
    } else if (cap == "net.send" || cap == "ipc.send") {
        *os << "  mov x8, #206\n"; // sendto
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    } else if (cap == "net.recv" || cap == "ipc.recv") {
        *os << "  mov x8, #207\n"; // recvfrom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  mov x3, #0\n";
        *os << "  mov x4, #0\n";
        *os << "  mov x5, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    } else if (cap == "random.bytes") {
        *os << "  mov x8, #278\n"; // getrandom
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0\n";
    void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" || cap == "debug.dump") {
        *os << "  mov x8, #64\n"; // write
        *os << "  mov x0, #2\n"; // stderr
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #128\n";
        *os << "  svc #0\n";
    void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    *os << "  mov x0, #0\n";
}

void AArch64::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void AArch64::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}


} // namespace target
} // namespace codegen
