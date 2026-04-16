#include "codegen/target/RiscV64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/SIMDInstruction.h"
#include "codegen/execgen/Assembler.h"
#include <ostream>
#include <algorithm>

namespace codegen {
namespace target {


void RiscV64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    int currentStackOffset = -16;
    for (auto& param : func.getParameters()) {
        currentStackOffset -= 8;
        cg.getStackOffsets()[param.get()] = currentStackOffset;
    }
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[instr.get()] = currentStackOffset;
            }
        }
    }
    int aligned_stack_size = (-currentStackOffset + 15) & ~15;
    emitPrologue(cg, aligned_stack_size);
    int int_reg_idx = 0;
    for (auto& param : func.getParameters()) {
        if (int_reg_idx < 8) {
            if (auto* os = cg.getTextStream()) *os << "  " << getStoreInstr(param->getType()) << " " << getIntegerArgumentRegisters()[int_reg_idx] << ", " << cg.getStackOffset(param.get()) << "(s0)\n";
            int_reg_idx++;
        }
    }
}

void RiscV64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) *os << getEpilogueLabel(&func) << ":\n";
    emitEpilogue(cg);
}

void RiscV64::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        if (!instr.getOperands().empty()) {
            ir::Value* retVal = instr.getOperands()[0]->get();
            if (auto* constant = dynamic_cast<ir::ConstantInt*>(retVal)) {
                *os << "  li a0, " << constant->getValue() << "\n";
            } else {
                std::string retval = cg.getValueAsOperand(retVal);
                *os << "  " << getLoadInstr(retVal->getType()) << " a0, " << retval << "\n";
            }
        }
        *os << "  j " << getEpilogueLabel(instr.getParent()->getParent()) << "\n";
        return;
    } else {
        if (!instr.getOperands().empty()) {
            auto& assembler = cg.getAssembler();
            ir::Value* retVal = instr.getOperands()[0]->get();
            emitLoadValue(cg, assembler, retVal, 10);
        }
    }
    emitFunctionEpilogue(cg, *instr.getParent()->getParent());
}

void RiscV64::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  " << getLoadInstr(type) << " t0, " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << rhsOperand << "\n";
        *os << "  " << getArithInstr("add", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0000000);
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitSub(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        *os << "  " << getLoadInstr(type) << " t0, " << cg.getValueAsOperand(lhs) << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << cg.getValueAsOperand(rhs) << "\n";
        *os << "  " << getArithInstr("sub", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << cg.getValueAsOperand(dest) << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0100000);
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(cg.getStackOffset(dest)));
    }
}

void RiscV64::emitMul(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        *os << "  " << getLoadInstr(type) << " t0, " << cg.getValueAsOperand(lhs) << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << cg.getValueAsOperand(rhs) << "\n";
        *os << "  " << getArithInstr("mul", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << cg.getValueAsOperand(dest) << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0000001);
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(cg.getStackOffset(dest)));
    }
}

void RiscV64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Udiv);

    if (auto* os = cg.getTextStream()) {
        *os << "  " << getLoadInstr(type) << " t0, " << cg.getValueAsOperand(lhs) << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << cg.getValueAsOperand(rhs) << "\n";
        *os << "  " << getArithInstr(isUnsigned ? "divu" : "div", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << cg.getValueAsOperand(dest) << "\n";
    }
}

void RiscV64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Urem);

    if (auto* os = cg.getTextStream()) {
        *os << "  " << getLoadInstr(type) << " t0, " << cg.getValueAsOperand(lhs) << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << cg.getValueAsOperand(rhs) << "\n";
        *os << "  " << getArithInstr(isUnsigned ? "remu" : "rem", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << cg.getValueAsOperand(dest) << "\n";
    }
}

void RiscV64::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::Type* type = instr.getOperands()[0]->get()->getType();
        if (type->isFloatingPoint()) {
            *os << "  flw ft0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  fsw ft0, " << cg.getValueAsOperand(&instr) << "\n";
        } else {
            *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

void RiscV64::emitCall(CodeGen& cg, ir::Instruction& instr) {
    const auto& int_regs = getIntegerArgumentRegisters();
    const auto& float_regs = getFloatArgumentRegisters();
    unsigned int_reg_idx = 0;
    unsigned float_reg_idx = 0;
    std::vector<ir::Value*> stack_args;

    if (auto* os = cg.getTextStream()) {
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            TypeInfo info = getTypeInfo(arg->getType());
            if (info.regClass == RegisterClass::Float && float_reg_idx < 8) {
                *os << "  " << (info.size == 32 ? "flw " : "fld ") << float_regs[float_reg_idx++] << ", " << cg.getValueAsOperand(arg) << "\n";
            } else if (int_reg_idx < 8) {
                *os << "  " << getLoadInstr(arg->getType()) << " " << int_regs[int_reg_idx++] << ", " << cg.getValueAsOperand(arg) << "\n";
            } else {
                stack_args.push_back(arg);
            }
        }
        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            *os << "  ld t0, " << cg.getValueAsOperand(arg) << "\n  addi sp, sp, -8\n  sd t0, 0(sp)\n";
        }
        *os << "  call " << instr.getOperands()[0]->get()->getName() << "\n";
        if (!stack_args.empty()) *os << "  addi sp, sp, " << stack_args.size() * 8 << "\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            TypeInfo info = getTypeInfo(instr.getType());
            if (info.regClass == RegisterClass::Float) *os << "  " << (info.size == 32 ? "fsw fa0, " : "fsd fa0, ") << cg.getValueAsOperand(&instr) << "\n";
            else *os << "  " << getStoreInstr(instr.getType()) << " a0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

TypeInfo RiscV64::getTypeInfo(const ir::Type* type) const {
    TypeInfo info;
    info.size = type->getSize() * 8;
    info.align = type->getAlignment() * 8;
    info.isFloatingPoint = type->isFloatingPoint();
    info.isSigned = true;
    if (type->isFloatingPoint()) info.regClass = RegisterClass::Float;
    else if (type->isVector()) info.regClass = RegisterClass::Vector;
    else info.regClass = RegisterClass::Integer;
    return info;
}

const std::vector<std::string>& RiscV64::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x5", "x6", "x7", "x28", "x29", "x30", "x31"};
    static const std::vector<std::string> floatRegs = {"f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f5", "f6", "f7", "f28", "f29", "f30", "f31"};
    return regClass == RegisterClass::Float ? floatRegs : intRegs;
}

const std::string& RiscV64::getReturnRegister(const ir::Type* type) const {
    return type->isFloatingPoint() ? getFloatReturnRegister() : getIntegerReturnRegister();
}

void RiscV64::emitCmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        std::string destOperand = cg.getValueAsOperand(&instr);
        bool is_float = lhs->getType()->isFloatingPoint();

        if (is_float) {
            *os << "  flw ft0, " << lhsOperand << "\n";
            *os << "  flw ft1, " << rhsOperand << "\n";
            std::string op_str;
            switch (instr.getOpcode()) {
                case ir::Instruction::Ceqf: op_str = "feq.s"; break;
                case ir::Instruction::Clt:  op_str = "flt.s"; break;
                case ir::Instruction::Cle:  op_str = "fle.s"; break;
                default: *os << "  # Unhandled FP comparison\n"; return;
            }
            *os << "  " << op_str << " t0, ft0, ft1\n";
            *os << "  sd t0, " << destOperand << "\n";
        } else {
            *os << "  ld t0, " << lhsOperand << "\n";
            *os << "  ld t1, " << rhsOperand << "\n";
            switch (instr.getOpcode()) {
                case ir::Instruction::Ceq: *os << "  sub t2, t0, t1\n  seqz t2, t2\n"; break;
                case ir::Instruction::Cne: *os << "  sub t2, t0, t1\n  snez t2, t2\n"; break;
                case ir::Instruction::Cslt: *os << "  slt t2, t0, t1\n"; break;
                case ir::Instruction::Cult: *os << "  sltu t2, t0, t1\n"; break;
                case ir::Instruction::Csgt: *os << "  sgt t2, t0, t1\n"; break;
                case ir::Instruction::Cugt: *os << "  sgtu t2, t0, t1\n"; break;
                case ir::Instruction::Csle: *os << "  sgt t2, t0, t1\n  xori t2, t2, 1\n"; break;
                case ir::Instruction::Cule: *os << "  sgtu t2, t0, t1\n  xori t2, t2, 1\n"; break;
                case ir::Instruction::Csge: *os << "  slt t2, t0, t1\n  xori t2, t2, 1\n"; break;
                case ir::Instruction::Cuge: *os << "  sltu t2, t0, t1\n  xori t2, t2, 1\n"; break;
                default: *os << "  # Unhandled integer comparison\n"; return;
            }
            *os << "  sd t2, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);

        if (lhs->getType()->isIntegerTy()) {
            switch (instr.getOpcode()) {
                case ir::Instruction::Ceq: emitRType(assembler, 0b0110011, 7, 0, 5, 6, 0b0100000); emitIType(assembler, 0b0010011, 7, 3, 7, 1); break;
                case ir::Instruction::Cne: emitRType(assembler, 0b0110011, 7, 0, 5, 6, 0b0100000); emitRType(assembler, 0b0110011, 7, 3, 0, 7, 0b0000000); break;
                case ir::Instruction::Cslt: emitRType(assembler, 0b0110011, 7, 2, 5, 6, 0b0000000); break;
                case ir::Instruction::Cult: emitRType(assembler, 0b0110011, 7, 3, 5, 6, 0b0000000); break;
                case ir::Instruction::Csgt: emitRType(assembler, 0b0110011, 7, 2, 6, 5, 0b0000000); break;
                case ir::Instruction::Cugt: emitRType(assembler, 0b0110011, 7, 3, 6, 5, 0b0000000); break;
                case ir::Instruction::Csle: emitRType(assembler, 0b0110011, 7, 2, 6, 5, 0b0000000); emitIType(assembler, 0b0010011, 7, 4, 7, 1); break;
                case ir::Instruction::Cule: emitRType(assembler, 0b0110011, 7, 3, 6, 5, 0b0000000); emitIType(assembler, 0b0010011, 7, 4, 7, 1); break;
                case ir::Instruction::Csge: emitRType(assembler, 0b0110011, 7, 2, 5, 6, 0b0000000); emitIType(assembler, 0b0010011, 7, 4, 7, 1); break;
                case ir::Instruction::Cuge: emitRType(assembler, 0b0110011, 7, 3, 5, 6, 0b0000000); emitIType(assembler, 0b0010011, 7, 4, 7, 1); break;
                default: break;
            }
            emitSType(assembler, 0b0100011, getStoreFunct3(instr.getType()), 8, 7, destOffset);
        }
    }
}

void RiscV64::emitBr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bnez t0, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        if (instr.getOperands().size() > 2) *os << "  j " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
    }
}

void RiscV64::emitJmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) *os << "  j " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
}

void RiscV64::emitAnd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  and t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitOr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  or t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitXor(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  xor t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitShl(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  sll t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitShr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  srl t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitSar(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  sra t0, t0, t1\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::Type* type = instr.getOperands()[0]->get()->getType();
        if (type->isFloatingPoint()) {
            *os << "  flw f0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  fneg.s f0, f0\n";
            *os << "  fsw f0, " << cg.getValueAsOperand(&instr) << "\n";
        } else {
            *os << "  " << getLoadInstr(type) << " t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  " << getArithInstr("neg", type) << " t0, t0\n";
            *os << "  " << getStoreInstr(type) << " t0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

void RiscV64::emitNot(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  not t0, t0\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  " << getLoadInstr(instr.getType()) << " t1, 0(t0)\n";
        *os << "  " << getStoreInstr(instr.getType()) << " t1, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitStore(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  " << getLoadInstr(instr.getOperands()[0]->get()->getType()) << " t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  " << getStoreInstr(instr.getOperands()[0]->get()->getType()) << " t0, 0(t1)\n";
    void RiscV64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.write") {
        *os << "  li a7, 64\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.read") {
        *os << "  li a7, 63\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.open") {
        *os << "  li a7, 56\n  li a0, -100\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a3, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.close") {
        *os << "  li a7, 57\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ecall\n";
    } else if (cap == "io.seek") {
        *os << "  li a7, 62\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.flush") {
        *os << "  li a7, 82\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ecall\n";
    } else if (cap == "io.stat") {
        *os << "  li a7, 80\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ecall\n";
    void RiscV64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  li a7, 93\n"; if (!instr.getOperands().empty()) *os << "  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ecall\n";
    } else if (cap == "process.spawn") {
        *os << "  li a7, 220\n  li a0, 17\n  li a1, 0\n  ecall\n  bnez a0, .Lspawn_parent_" << cg.labelCounter << "\n  li a7, 221\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n  li a7, 93\n  li a0, 1\n  ecall\n.Lspawn_parent_" << cg.labelCounter << ":\n"; cg.labelCounter++;
    } else if (cap == "process.abort") {
        *os << "  li a7, 172\n  ecall\n  mv a1, a0\n  li a0, 6\n  li a7, 129\n  ecall\n";
    } else if (cap == "process.info") {
        *os << "  li a7, 172\n  ecall\n";
    } else if (cap == "process.sleep") {
        *os << "  addi sp, sp, -16\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a1, 1000\n  div a2, a0, a1\n  rem a3, a0, a1\n  li a4, 1000000\n  mul a3, a3, a4\n  sd a2, 0(sp)\n  sd a3, 8(sp)\n  mv a0, sp\n  li a1, 0\n  li a7, 101\n  ecall\n  addi sp, sp, 16\n";
    void RiscV64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
        *os << "  li t0, 1\n  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n.Lmutex_retry_" << cg.labelCounter << ":\n  amoswap.w.aq t2, t0, (t1)\n  bnez t2, .Lmutex_retry_" << cg.labelCounter << "\n"; cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  amoswap.w.rl x0, x0, (t1)\n";
    } else if (cap == "sync.fence") { *os << "  fence\n"; void RiscV64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") { *os << "  li a7, 198\n  li a0, 2\n  li a1, 1\n  li a2, 0\n  ecall\n  mv t5, a0\n  li a7, 203\n  mv a0, t5\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a2, 16\n  ecall\n  mv a0, t5\n"; }
    else if (cap == "net.send") { *os << "  li a7, 206\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  li a3, 0\n  li a4, 0\n  li a5, 0\n  ecall\n"; void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::ConstantInt* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get());
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;
    if (auto* os = cg.getTextStream()) { *os << "  la t0, heap_ptr\n  ld t1, 0(t0)\n  sd t1, " << pointerOffset << "(s0)\n  addi t1, t1, " << alignedSize << "\n  sd t1, 0(t0)\n"; }
}

void RiscV64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) { *os << ".globl _start\n_start:\n  call main\n  li a7, 93\n  ecall\n"; }
}

VectorCapabilities RiscV64::getVectorCapabilities() const { VectorCapabilities caps; caps.supportedWidths = {128, 256, 512}; return caps; }
bool RiscV64::supportsVectorWidth(unsigned width) const { return width <= 512; }
bool RiscV64::supportsVectorType(const ir::VectorType* type) const { return type->getBitWidth() <= 512; }
unsigned RiscV64::getOptimalVectorWidth(const ir::Type* elementType) const { (void)elementType; return 128;
}

TypeInfo RiscV64::getTypeInfo(const ir::Type* type) const {
    TypeInfo info;
    info.size = type->getSize() * 8;
    info.align = type->getAlignment() * 8;
    info.isFloatingPoint = type->isFloatingPoint();
    info.isSigned = true;
    if (type->isFloatingPoint()) info.regClass = RegisterClass::Float;
    else if (type->isVector()) info.regClass = RegisterClass::Vector;
    else info.regClass = RegisterClass::Integer;
    return info;
}

const std::vector<std::string>& RiscV64::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x5", "x6", "x7", "x28", "x29", "x30", "x31"};
    static const std::vector<std::string> floatRegs = {"f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f5", "f6", "f7", "f28", "f29", "f30", "f31"};
    return regClass == RegisterClass::Float ? floatRegs : intRegs;
}

const std::string& RiscV64::getReturnRegister(const ir::Type* type) const {
    return type->isFloatingPoint() ? getFloatReturnRegister() : getIntegerReturnRegister();
}

void RiscV64::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        *os << "  addi sp, sp, -" << stack_size << "\n";
        *os << "  sd ra, " << stack_size - 8 << "(sp)\n";
        *os << "  sd s0, " << stack_size - 16 << "(sp)\n";
        *os << "  addi s0, sp, " << stack_size << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitIType(assembler, 0b0010011, 2, 0, 2, -stack_size);
        emitSType(assembler, 0b0100011, 3, 2, 1, stack_size - 8);
        emitSType(assembler, 0b0100011, 3, 2, 8, stack_size - 16);
        emitIType(assembler, 0b0010011, 8, 0, 2, stack_size);
    }
}

void RiscV64::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld ra, -8(s0)\n";
        *os << "  ld s0, -16(s0)\n";
        *os << "  addi sp, s0, 0\n";
        *os << "  jr ra\n";
    } else {
        auto& assembler = cg.getAssembler();
        emitIType(assembler, 0b0000011, 1, 3, 8, -8);
        emitIType(assembler, 0b0000011, 8, 3, 8, -16);
        emitIType(assembler, 0b0010011, 2, 0, 8, 0);
        emitIType(assembler, 0b1100111, 0, 0, 1, 0);
    }
}

const std::vector<std::string>& RiscV64::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> regs = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
    return regs;
}

const std::vector<std::string>& RiscV64::getFloatArgumentRegisters() const {
    static const std::vector<std::string> regs = {"fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"};
    return regs;
}

const std::string& RiscV64::getIntegerReturnRegister() const {
    static const std::string reg = "a0";
    return reg;
}

const std::string& RiscV64::getFloatReturnRegister() const {
    static const std::string reg = "fa0";
    return reg;
}

void RiscV64::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {
    (void)cg; (void)argIndex; (void)value; (void)type;
}

void RiscV64::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {
    (void)cg; (void)argIndex; (void)dest; (void)type;
}

void RiscV64::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  flw f0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  flw f1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  fadd.s f0, f0, f1\n";
        *os << "  fsw f0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  flw f0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  flw f1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  fsub.s f0, f0, f1\n";
        *os << "  fsw f0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitFMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  flw f0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  flw f1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  fmul.s f0, f0, f1\n";
        *os << "  fsw f0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  flw f0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  flw f1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  fdiv.s f0, f0, f1\n";
        *os << "  fsw f0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitCast(CodeGen& cg, ir::Instruction& instr, const ir::Type* fromType, const ir::Type* toType) {
    (void)fromType; (void)toType;
    if (auto* os = cg.getTextStream()) {
        *os << "  ld t0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  sd t0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void RiscV64::emitVAStart(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
}

void RiscV64::emitVAArg(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
}

void RiscV64::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr; }

uint64_t RiscV64::getSyscallNumber(ir::SyscallId id) const {
    switch (id) {
        case ir::SyscallId::Exit: return 93; case ir::SyscallId::Read: return 63; case ir::SyscallId::Write: return 64; case ir::SyscallId::OpenAt: return 56; case ir::SyscallId::Close: return 57; default: return 0;
    }
}

size_t RiscV64::getPointerSize() const {
    return 64; }

namespace {
std::string getEpilogueLabel(const ir::Function* func) {
    return func ? func->getName() + "_epilogue" : ".L_epilogue";
}

uint8_t getLoadFunct3(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return 0; // lb
            case 16: return 1; // lh
            case 32: return 2; // lw
            case 64: return 3; // ld
        }
    }
    if (type->isPointerTy()) return 3; // ld
    return 3;
}

uint8_t getStoreFunct3(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return 0; // sb
            case 16: return 1; // sh
            case 32: return 2; // sw
            case 64: return 3; // sd
        }
    }
    if (type->isPointerTy()) return 3; // sd
    return 3;
}

std::string getLoadInstr(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return "lb";
            case 16: return "lh";
            case 32: return "lw";
            case 64: return "ld";
        }
    }
    if (type->isPointerTy()) return "ld";
    return "ld";
}

std::string getStoreInstr(const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        switch (intTy->getBitwidth()) {
            case 8:  return "sb";
            case 16: return "sh";
            case 32: return "sw";
            case 64: return "sd";
        }
    }
    if (type->isPointerTy()) return "sd";
    return "sd";
}

std::string getArithInstr(const std::string& base, const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        if (intTy->getBitwidth() == 32) return base + "w";
    }
    return base;
}

uint8_t getArithOpcode(uint8_t base, const ir::Type* type) {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        if (intTy->getBitwidth() == 32) return 0b0111011; // OP-32
    }
    return base;
}

void emitIType(execgen::Assembler& assembler, uint8_t opcode, uint8_t rd, uint8_t funct3, uint8_t rs1, int16_t imm);

void emitLoadValue(CodeGen& cg, execgen::Assembler& assembler, ir::Value* val, uint8_t reg) {
    if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
        if (instr->hasPhysicalRegister()) {
            uint8_t src_reg = static_cast<uint8_t>(instr->getPhysicalRegister());
            if (src_reg == reg) return;
            emitIType(assembler, 0b0010011, reg, 0, src_reg, 0);
            return;
        }
    }

    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        int64_t value = constInt->getValue();
        if (value >= -2048 && value <= 2047) {
            emitIType(assembler, 0b0010011, reg, 0, 0, static_cast<int16_t>(value));
        } else {
            uint32_t high = (value + 0x800) >> 12;
            int16_t low = value & 0xFFF;
            if (low & 0x800) low -= 0x1000;
            assembler.emitDWord(0b0110111 | (reg << 7) | ((high & 0xFFFFF) << 12));
            emitIType(assembler, 0b0011011, reg, 0, reg, low);
        }
    } else {
        int32_t offset = cg.getStackOffset(val);
        emitIType(assembler, 0b0000011, reg, getLoadFunct3(val->getType()), 8, static_cast<int16_t>(offset));
    }
}

void emitIType(execgen::Assembler& assembler, uint8_t opcode, uint8_t rd, uint8_t funct3, uint8_t rs1, int16_t imm) {
    uint32_t instruction = 0;
    instruction |= (imm & 0xFFF) << 20;
    instruction |= rs1 << 15;
    instruction |= funct3 << 12;
    instruction |= rd << 7;
    instruction |= opcode;
    assembler.emitDWord(instruction);
}

void emitSType(execgen::Assembler& assembler, uint8_t opcode, uint8_t funct3, uint8_t rs1, uint8_t rs2, int16_t imm) {
    uint32_t instruction = 0;
    instruction |= ((imm >> 5) & 0x7F) << 25;
    instruction |= rs2 << 20;
    instruction |= rs1 << 15;
    instruction |= funct3 << 12;
    instruction |= (imm & 0x1F) << 7;
    instruction |= opcode;
    assembler.emitDWord(instruction);
}

void emitRType(execgen::Assembler& assembler, uint8_t opcode, uint8_t rd, uint8_t funct3, uint8_t rs1, uint8_t rs2, uint8_t funct7) {
    uint32_t instruction = 0;
    instruction |= funct7 << 25;
    instruction |= rs2 << 20;
    instruction |= rs1 << 15;
    instruction |= funct3 << 12;
    instruction |= rd << 7;
    instruction |= opcode;
    assembler.emitDWord(instruction);
}

void emitJType(execgen::Assembler& assembler, uint8_t opcode, uint8_t rd, int32_t imm) {
    uint32_t instruction = 0;
    instruction |= (imm & 0x100000) << 11;
    instruction |= (imm & 0x7FE) << 20;
    instruction |= (imm & 0x800) << 9;
    instruction |= (imm & 0xFF000);
    instruction |= rd << 7;
    instruction |= opcode;
    assembler.emitDWord(instruction);
}

void emitBType(execgen::Assembler& assembler, uint8_t opcode, uint8_t funct3, uint8_t rs1, uint8_t rs2, int16_t imm) {
    uint32_t instruction = 0;
    instruction |= ((imm >> 12) & 0x1) << 31;
    instruction |= ((imm >> 5) & 0x3F) << 25;
    instruction |= rs2 << 20;
    instruction |= rs1 << 15;
    instruction |= funct3 << 12;
    instruction |= ((imm >> 1) & 0xF) << 8;
    instruction |= ((imm >> 11) & 0x1) << 7;
    instruction |= opcode;
    assembler.emitDWord(instruction);
}

}

std::string RiscV64::formatStackOperand(int offset) const {
    return std::to_string(offset) + "(s0)";
}

std::string RiscV64::formatGlobalOperand(const std::string& name) const {
    return name;
}

void RiscV64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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

void RiscV64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.write") {
        *os << "  li a7, 64\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.read") {
        *os << "  li a7, 63\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.open") {
        *os << "  li a7, 56\n  li a0, -100\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a3, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.close") {
        *os << "  li a7, 57\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ecall\n";
    } else if (cap == "io.seek") {
        *os << "  li a7, 62\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  ecall\n";
    } else if (cap == "io.flush") {
        *os << "  li a7, 82\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ecall\n";
    } else if (cap == "io.stat") {
        *os << "  li a7, 80\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ecall\n";
    void RiscV64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  li a7, 93\n"; if (!instr.getOperands().empty()) *os << "  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ecall\n";
    } else if (cap == "process.spawn") {
        *os << "  li a7, 220\n  li a0, 17\n  li a1, 0\n  ecall\n  bnez a0, .Lspawn_parent_" << cg.labelCounter << "\n  li a7, 221\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n  li a7, 93\n  li a0, 1\n  ecall\n.Lspawn_parent_" << cg.labelCounter << ":\n"; cg.labelCounter++;
    } else if (cap == "process.abort") {
        *os << "  li a7, 172\n  ecall\n  mv a1, a0\n  li a0, 6\n  li a7, 129\n  ecall\n";
    } else if (cap == "process.info") {
        *os << "  li a7, 172\n  ecall\n";
    } else if (cap == "process.sleep") {
        *os << "  addi sp, sp, -16\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a1, 1000\n  div a2, a0, a1\n  rem a3, a0, a1\n  li a4, 1000000\n  mul a3, a3, a4\n  sd a2, 0(sp)\n  sd a3, 8(sp)\n  mv a0, sp\n  li a1, 0\n  li a7, 101\n  ecall\n  addi sp, sp, 16\n";
    void RiscV64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
        *os << "  li t0, 1\n  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n.Lmutex_retry_" << cg.labelCounter << ":\n  amoswap.w.aq t2, t0, (t1)\n  bnez t2, .Lmutex_retry_" << cg.labelCounter << "\n"; cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  amoswap.w.rl x0, x0, (t1)\n";
    } else if (cap == "sync.fence") { *os << "  fence\n"; void RiscV64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") { *os << "  li a7, 198\n  li a0, 2\n  li a1, 1\n  li a2, 0\n  ecall\n  mv t5, a0\n  li a7, 203\n  mv a0, t5\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a2, 16\n  ecall\n  mv a0, t5\n"; }
    else if (cap == "net.send") { *os << "  li a7, 206\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  li a3, 0\n  li a4, 0\n  li a5, 0\n  ecall\n"; void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void RiscV64::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void RiscV64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  li a7, 93\n"; if (!instr.getOperands().empty()) *os << "  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  ecall\n";
    } else if (cap == "process.spawn") {
        *os << "  li a7, 220\n  li a0, 17\n  li a1, 0\n  ecall\n  bnez a0, .Lspawn_parent_" << cg.labelCounter << "\n  li a7, 221\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n  li a7, 93\n  li a0, 1\n  ecall\n.Lspawn_parent_" << cg.labelCounter << ":\n"; cg.labelCounter++;
    } else if (cap == "process.abort") {
        *os << "  li a7, 172\n  ecall\n  mv a1, a0\n  li a0, 6\n  li a7, 129\n  ecall\n";
    } else if (cap == "process.info") {
        *os << "  li a7, 172\n  ecall\n";
    } else if (cap == "process.sleep") {
        *os << "  addi sp, sp, -16\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a1, 1000\n  div a2, a0, a1\n  rem a3, a0, a1\n  li a4, 1000000\n  mul a3, a3, a4\n  sd a2, 0(sp)\n  sd a3, 8(sp)\n  mv a0, sp\n  li a1, 0\n  li a7, 101\n  ecall\n  addi sp, sp, 16\n";
    void RiscV64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
        *os << "  li t0, 1\n  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n.Lmutex_retry_" << cg.labelCounter << ":\n  amoswap.w.aq t2, t0, (t1)\n  bnez t2, .Lmutex_retry_" << cg.labelCounter << "\n"; cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  amoswap.w.rl x0, x0, (t1)\n";
    } else if (cap == "sync.fence") { *os << "  fence\n"; void RiscV64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") { *os << "  li a7, 198\n  li a0, 2\n  li a1, 1\n  li a2, 0\n  ecall\n  mv t5, a0\n  li a7, 203\n  mv a0, t5\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a2, 16\n  ecall\n  mv a0, t5\n"; }
    else if (cap == "net.send") { *os << "  li a7, 206\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  li a3, 0\n  li a4, 0\n  li a5, 0\n  ecall\n"; void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void RiscV64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
        *os << "  li t0, 1\n  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n.Lmutex_retry_" << cg.labelCounter << ":\n  amoswap.w.aq t2, t0, (t1)\n  bnez t2, .Lmutex_retry_" << cg.labelCounter << "\n"; cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  ld t1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  amoswap.w.rl x0, x0, (t1)\n";
    } else if (cap == "sync.fence") { *os << "  fence\n"; void RiscV64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") { *os << "  li a7, 198\n  li a0, 2\n  li a1, 1\n  li a2, 0\n  ecall\n  mv t5, a0\n  li a7, 203\n  mv a0, t5\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a2, 16\n  ecall\n  mv a0, t5\n"; }
    else if (cap == "net.send") { *os << "  li a7, 206\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  li a3, 0\n  li a4, 0\n  li a5, 0\n  ecall\n"; void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "time.now" || cap == "time.monotonic") {
        *os << "  rdtime a0\n";
    } else {
        *os << "  li a0, -38\n";
    }
}

void RiscV64::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, -38\n";
}

void RiscV64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "net.connect") { *os << "  li a7, 198\n  li a0, 2\n  li a1, 1\n  li a2, 0\n  ecall\n  mv t5, a0\n  li a7, 203\n  mv a0, t5\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  li a2, 16\n  ecall\n  mv a0, t5\n"; }
    else if (cap == "net.send") { *os << "  li a7, 206\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  ld a2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  li a3, 0\n  li a4, 0\n  li a5, 0\n  ecall\n"; void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, -38\n";
}

void RiscV64::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, 0\n";
}

void RiscV64::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "system.page_size") *os << "  li a0, 4096\n";
    else if (cap == "system.cpu_count") *os << "  li a0, 1\n";
    else *os << "  li a0, -38\n";
}

void RiscV64::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, -38\n";
}

void RiscV64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "random.u64") { *os << "  addi sp, sp, -16\n  li a7, 278\n  mv a0, sp\n  li a1, 8\n  li a2, 0\n  ecall\n  ld a0, 0(sp)\n  addi sp, sp, 16\n"; }
    else if (cap == "random.bytes") { *os << "  li a7, 278\n  ld a0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  ld a1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  li a2, 0\n  ecall\n"; void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n"; void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.log" && !instr.getOperands().empty()) {
        *os << "  li a7, 64\n";
        *os << "  li a0, 2\n";
        *os << "  ld a1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  li a2, 64\n";
        *os << "  ecall\n";
    } else {
        *os << "  li a0, 0\n";
    }
}

void RiscV64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  li a0, 0\n";
}

void RiscV64::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "tty.isatty") *os << "  li a0, 1\n";
    else *os << "  li a0, -38\n";
}

void RiscV64::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, -38\n";
}

void RiscV64::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  li a0, -38\n";
}


} // namespace target
} // namespace codegen
