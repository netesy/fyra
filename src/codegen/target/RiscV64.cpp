#include "codegen/target/RiscV64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/SIMDInstruction.h"
#include "codegen/execgen/Assembler.h"
#include <ostream>
#include <algorithm>

namespace codegen {
namespace target {

namespace {

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
    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        int64_t value = constInt->getValue();
        if (value >= -2048 && value <= 2047) {
            // li reg, value (addi reg, x0, value)
            emitIType(assembler, 0b0010011, reg, 0, 0, static_cast<int16_t>(value));
        } else {
            // Standard 64-bit constant loading for RISC-V:
            // lui + addiw + shifts + ori
            // For now, implement simple 32-bit load: lui + addiw
            uint32_t high = (value + 0x800) >> 12;
            int16_t low = value & 0xFFF;
            if (low & 0x800) low -= 0x1000;

            // lui reg, high
            assembler.emitDWord(0b0110111 | (reg << 7) | ((high & 0xFFFFF) << 12));
            // addiw reg, reg, low
            emitIType(assembler, 0b0011011, reg, 0, reg, low);
        }
    } else {
        int32_t offset = cg.getStackOffset(val);
        // load reg, offset(s0)
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

void RiscV64::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        *os << "  addi sp, sp, -" << stack_size << "\n";
        *os << "  sd ra, " << stack_size - 8 << "(sp)\n";
        *os << "  sd s0, " << stack_size - 16 << "(sp)\n";
        *os << "  addi s0, sp, " << stack_size << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        // addi sp, sp, -stack_size
        emitIType(assembler, 0b0010011, 2, 0, 2, -stack_size);
        // sd ra, stack_size - 8(sp)
        emitSType(assembler, 0b0100011, 3, 2, 1, stack_size - 8);
        // sd s0, stack_size - 16(sp)
        emitSType(assembler, 0b0100011, 3, 2, 8, stack_size - 16);
        // addi s0, sp, stack_size
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
        // ld ra, -8(s0)
        emitIType(assembler, 0b0000011, 1, 3, 8, -8);
        // ld s0, -16(s0)
        emitIType(assembler, 0b0000011, 8, 3, 8, -16);
        // addi sp, s0, 0
        emitIType(assembler, 0b0010011, 2, 0, 8, 0);
        // jr ra (jalr x0, ra, 0)
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
    } else {
        if (!instr.getOperands().empty()) {
            auto& assembler = cg.getAssembler();
            ir::Value* retVal = instr.getOperands()[0]->get();
            emitLoadValue(cg, assembler, retVal, 10); // load into a0
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
        // load
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        // add
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0000000);
        // store
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
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
                case ir::Instruction::Ceq:
                    *os << "  sub t2, t0, t1\n";
                    *os << "  seqz t2, t2\n";
                    break;
                case ir::Instruction::Cne:
                    *os << "  sub t2, t0, t1\n";
                    *os << "  snez t2, t2\n";
                    break;
                case ir::Instruction::Cslt: *os << "  slt t2, t0, t1\n"; break;
                case ir::Instruction::Cult: *os << "  sltu t2, t0, t1\n"; break;
                case ir::Instruction::Csgt: *os << "  sgt t2, t0, t1\n"; break;
                case ir::Instruction::Cugt: *os << "  sgtu t2, t0, t1\n"; break;
                case ir::Instruction::Csle: *os << "  sgt t2, t0, t1\n"; *os << "  xori t2, t2, 1\n"; break;
                case ir::Instruction::Cule: *os << "  sgtu t2, t0, t1\n"; *os << "  xori t2, t2, 1\n"; break;
                case ir::Instruction::Csge: *os << "  slt t2, t0, t1\n"; *os << "  xori t2, t2, 1\n"; break;
                case ir::Instruction::Cuge: *os << "  sltu t2, t0, t1\n"; *os << "  xori t2, t2, 1\n"; break;
                default: *os << "  # Unhandled integer comparison\n"; return;
            }
            *os << "  sd t2, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitLoadValue(cg, assembler, lhs, 5); // load into t0
        emitLoadValue(cg, assembler, rhs, 6); // load into t1

        if (lhs->getType()->isIntegerTy()) {
            switch (instr.getOpcode()) {
                case ir::Instruction::Ceq:
                    emitRType(assembler, 0b0110011, 7, 0, 5, 6, 0b0100000); // sub t2, t0, t1
                    emitIType(assembler, 0b0010011, 7, 3, 7, 1);          // seqz t2, t2 (sltiu)
                    break;
                case ir::Instruction::Cne:
                    emitRType(assembler, 0b0110011, 7, 0, 5, 6, 0b0100000); // sub t2, t0, t1
                    emitRType(assembler, 0b0110011, 7, 3, 0, 7, 0b0000000); // snez t2, t2 (sltu)
                    break;
                case ir::Instruction::Cslt:
                    emitRType(assembler, 0b0110011, 7, 2, 5, 6, 0b0000000); // slt t2, t0, t1
                    break;
                case ir::Instruction::Cult:
                    emitRType(assembler, 0b0110011, 7, 3, 5, 6, 0b0000000); // sltu t2, t0, t1
                    break;
                case ir::Instruction::Csgt:
                    emitRType(assembler, 0b0110011, 7, 2, 6, 5, 0b0000000); // slt t2, t1, t0
                    break;
                case ir::Instruction::Cugt:
                    emitRType(assembler, 0b0110011, 7, 3, 6, 5, 0b0000000); // sltu t2, t1, t0
                    break;
                case ir::Instruction::Csle:
                    emitRType(assembler, 0b0110011, 7, 2, 6, 5, 0b0000000); // slt t2, t1, t0
                    emitIType(assembler, 0b0010011, 7, 4, 7, 1);          // xori t2, t2, 1
                    break;
                case ir::Instruction::Cule:
                    emitRType(assembler, 0b0110011, 7, 3, 6, 5, 0b0000000); // sltu t2, t1, t0
                    emitIType(assembler, 0b0010011, 7, 4, 7, 1);          // xori t2, t2, 1
                    break;
                case ir::Instruction::Csge:
                    emitRType(assembler, 0b0110011, 7, 2, 5, 6, 0b0000000); // slt t2, t0, t1
                    emitIType(assembler, 0b0010011, 7, 4, 7, 1);          // xori t2, t2, 1
                    break;
                case ir::Instruction::Cuge:
                    emitRType(assembler, 0b0110011, 7, 3, 5, 6, 0b0000000); // sltu t2, t0, t1
                    emitIType(assembler, 0b0010011, 7, 4, 7, 1);          // xori t2, t2, 1
                    break;
                default: break;
            }
            emitSType(assembler, 0b0100011, getStoreFunct3(instr.getType()), 8, 7, destOffset);
        } else {
            // load into f0, f1
            emitIType(assembler, 0b0000111, 0, 2, 8, lhsOffset);
            emitIType(assembler, 0b0000111, 1, 2, 8, rhsOffset);
            uint8_t funct7 = 0;
            uint8_t funct3 = 0;
            switch (instr.getOpcode()) {
                case ir::Instruction::Ceqf: funct7 = 0b1010000; funct3 = 2; break; // feq.s
                case ir::Instruction::Clt:  funct7 = 0b1010000; funct3 = 1; break; // flt.s
                case ir::Instruction::Cle:  funct7 = 0b1010000; funct3 = 0; break; // fle.s
                default: break;
            }
            emitRType(assembler, 0b1010011, 7, funct3, 0, 1, funct7);
            emitSType(assembler, 0b0100011, getStoreFunct3(instr.getType()), 8, 7, destOffset);
        }
    }
}

void RiscV64::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  flw f0, " << lhsOperand << "\n";
        *os << "  flw f1, " << rhsOperand << "\n";
        *os << "  fadd.s f0, f0, f1\n";
        *os << "  fsw f0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        emitIType(assembler, 0b0000111, 0, 2, 8, lhsOffset);  // flw f0, lhsOffset(s0)
        emitIType(assembler, 0b0000111, 1, 2, 8, rhsOffset);  // flw f1, rhsOffset(s0)
        emitRType(assembler, 0b1010011, 0, 0, 0, 1, 0b0000000); // fadd.s f0, f0, f1
        emitSType(assembler, 0b0100111, 2, 8, 0, destOffset);  // fsw f0, destOffset(s0)
    }
}

void RiscV64::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  flw f0, " << lhsOperand << "\n";
        *os << "  flw f1, " << rhsOperand << "\n";
        *os << "  fsub.s f0, f0, f1\n";
        *os << "  fsw f0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        emitIType(assembler, 0b0000111, 0, 2, 8, lhsOffset);  // flw f0, lhsOffset(s0)
        emitIType(assembler, 0b0000111, 1, 2, 8, rhsOffset);  // flw f1, rhsOffset(s0)
        emitRType(assembler, 0b1010011, 0, 0, 0, 1, 0b0000100); // fsub.s f0, f0, f1
        emitSType(assembler, 0b0100111, 2, 8, 0, destOffset);  // fsw f0, destOffset(s0)
    }
}

void RiscV64::emitFMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  flw f0, " << lhsOperand << "\n";
        *os << "  flw f1, " << rhsOperand << "\n";
        *os << "  fmul.s f0, f0, f1\n";
        *os << "  fsw f0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        emitIType(assembler, 0b0000111, 0, 2, 8, lhsOffset);  // flw f0, lhsOffset(s0)
        emitIType(assembler, 0b0000111, 1, 2, 8, rhsOffset);  // flw f1, rhsOffset(s0)
        emitRType(assembler, 0b1010011, 0, 0, 0, 1, 0b0001000); // fmul.s f0, f0, f1
        emitSType(assembler, 0b0100111, 2, 8, 0, destOffset);  // fsw f0, destOffset(s0)
    }
}

void RiscV64::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  flw f0, " << lhsOperand << "\n";
        *os << "  flw f1, " << rhsOperand << "\n";
        *os << "  fdiv.s f0, f0, f1\n";
        *os << "  fsw f0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        emitIType(assembler, 0b0000111, 0, 2, 8, lhsOffset);  // flw f0, lhsOffset(s0)
        emitIType(assembler, 0b0000111, 1, 2, 8, rhsOffset);  // flw f1, rhsOffset(s0)
        emitRType(assembler, 0b1010011, 0, 0, 0, 1, 0b0001100); // fdiv.s f0, f0, f1
        emitSType(assembler, 0b0100111, 2, 8, 0, destOffset);  // fsw f0, destOffset(s0)
    }
}

void RiscV64::emitSub(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  " << getArithInstr("sub", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        // load
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        // sub
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0100000);
        // store
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitMul(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  " << getArithInstr("mul", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        // load
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        // mul
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 5, 6, 0b0000001);
        // store
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Udiv);

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  " << getLoadInstr(type) << " t0, " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << rhsOperand << "\n";
        *os << "  " << getArithInstr(isUnsigned ? "divu" : "div", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        // load
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        // div/divu
        uint8_t funct3 = isUnsigned ? 5 : 4;
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, funct3, 5, 6, 0b0000001);
        // store
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    bool isUnsigned = (instr.getOpcode() == ir::Instruction::Urem);

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  " << getLoadInstr(type) << " t0, " << lhsOperand << "\n";
        *os << "  " << getLoadInstr(type) << " t1, " << rhsOperand << "\n";
        *os << "  " << getArithInstr(isUnsigned ? "remu" : "rem", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        // load
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        // rem/remu
        uint8_t funct3 = isUnsigned ? 7 : 6;
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, funct3, 5, 6, 0b0000001);
        // store
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitAnd(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  and t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, 0b0110011, 5, 7, 5, 6, 0b0000000); // and t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitOr(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  or t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, 0b0110011, 5, 6, 5, 6, 0b0000000); // or t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitXor(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  xor t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, 0b0110011, 5, 4, 5, 6, 0b0000000); // xor t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitShl(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  " << getArithInstr("sll", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 1, 5, 6, 0b0000000); // sll t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitShr(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  " << getArithInstr("srl", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 5, 5, 6, 0b0000000); // srl t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitSar(CodeGen& cg, ir::Instruction& instr) {
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
        *os << "  " << getArithInstr("sra", type) << " t0, t0, t1\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        emitLoadValue(cg, assembler, lhs, 5);
        emitLoadValue(cg, assembler, rhs, 6);
        emitRType(assembler, getArithOpcode(0b0110011, type), 5, 5, 5, 6, 0b0100000); // sra t0, t0, t1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, static_cast<int16_t>(destOffset));
    }
}

void RiscV64::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        if (type->isFloatTy()) {
            *os << "  flw f0, " << opOperand << "\n";
            *os << "  fneg.s f0, f0\n";
            *os << "  fsw f0, " << destOperand << "\n";
        } else if (type->isDoubleTy()) {
            *os << "  fld f0, " << opOperand << "\n";
            *os << "  fneg.d f0, f0\n";
            *os << "  fsd f0, " << destOperand << "\n";
        } else {
            *os << "  " << getLoadInstr(type) << " t0, " << opOperand << "\n";
            *os << "  " << getArithInstr("neg", type) << " t0, t0\n";
            *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t opOffset = cg.getStackOffset(op);
        int32_t destOffset = cg.getStackOffset(dest);
        if (type->isFloatingPoint()) {
            emitIType(assembler, 0b0000111, 0, (type->getSize() == 4 ? 2 : 3), 8, opOffset); // flw/fld f0
            // fsgnjn.s/d f0, f0, f0
            uint8_t funct7 = (type->getSize() == 4 ? 0b0010000 : 0b0010001);
            emitRType(assembler, 0b1010011, 0, 0b001, 0, 0, funct7);
            emitSType(assembler, 0b0100111, (type->getSize() == 4 ? 2 : 3), 8, 0, destOffset); // fsw/fsd f0
        } else {
            // load
            emitIType(assembler, 0b0000011, 5, getLoadFunct3(type), 8, opOffset);
            // sub t0, x0, t0
            emitRType(assembler, getArithOpcode(0b0110011, type), 5, 0, 0, 5, 0b0100000);
            // store
            emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, destOffset);
        }
    }
}

void RiscV64::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* src = instr.getOperands()[0]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string srcOperand = cg.getValueAsOperand(src);
        const ir::Type* type = src->getType();
        if (type->isFloatingPoint()) {
            if (type->isDoubleTy()) {
                *os << "  fld ft0, " << srcOperand << "\n";
                *os << "  fsd ft0, " << destOperand << "\n";
            } else {
                *os << "  flw ft0, " << srcOperand << "\n";
                *os << "  fsw ft0, " << destOperand << "\n";
            }
        } else {
            *os << "  ld t0, " << srcOperand << "\n";
            *os << "  sd t0, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* src = instr.getOperands()[0]->get();
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);
        // ld t0, srcOffset(s0)
        emitIType(assembler, 0b0000011, 5, 3, 8, srcOffset);
        // sd t0, destOffset(s0)
        emitSType(assembler, 0b0100011, 3, 8, 5, destOffset);
    }
}

void RiscV64::emitCall(CodeGen& cg, ir::Instruction& instr) {
    const auto& int_regs = getIntegerArgumentRegisters();
    const auto& float_regs = getFloatArgumentRegisters();
    unsigned int_reg_idx = 0;
    unsigned float_reg_idx = 0;
    std::vector<ir::Value*> stack_args;

    if (auto* os = cg.getTextStream()) {
        ir::Value* calleeValue = instr.getOperands()[0]->get();
        std::string callee = calleeValue->getName();
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            TypeInfo info = getTypeInfo(arg->getType());
            std::string argOperand = cg.getValueAsOperand(arg);

            if (info.regClass == RegisterClass::Float) {
                if (float_reg_idx < 8) {
                    // RISC-V uses flw/fld for float loads
                    std::string instr = (info.size == 32) ? "flw" : "fld";
                    *os << "  " << instr << " " << float_regs[float_reg_idx++] << ", " << argOperand << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            } else {
                if (int_reg_idx < 8) {
                    *os << "  " << getLoadInstr(arg->getType()) << " " << int_regs[int_reg_idx++] << ", " << argOperand << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            std::string argOperand = cg.getValueAsOperand(arg);
            TypeInfo info = getTypeInfo(arg->getType());
            if (info.regClass == RegisterClass::Float) {
                std::string lInstr = (info.size == 32) ? "flw" : "fld";
                std::string sInstr = (info.size == 32) ? "fsw" : "fsd";
                *os << "  " << lInstr << " ft0, " << argOperand << "\n";
                *os << "  addi sp, sp, -8\n";
                *os << "  " << sInstr << " ft0, 0(sp)\n";
            } else {
                *os << "  " << getLoadInstr(arg->getType()) << " t0, " << argOperand << "\n";
                *os << "  addi sp, sp, -8\n";
                *os << "  " << getStoreInstr(arg->getType()) << " t0, 0(sp)\n";
            }
        }

        *os << "  call " << callee << "\n";

        if (!stack_args.empty()) {
            *os << "  addi sp, sp, " << stack_args.size() * 8 << "\n";
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string destOperand = cg.getValueAsOperand(&instr);
            TypeInfo info = getTypeInfo(instr.getType());
            if (info.regClass == RegisterClass::Float) {
                std::string sInstr = (info.size == 32) ? "fsw" : "fsd";
                *os << "  " << sInstr << " fa0, " << destOperand << "\n";
            } else {
                *os << "  " << getStoreInstr(instr.getType()) << " a0, " << destOperand << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            TypeInfo info = getTypeInfo(arg->getType());
            if (info.regClass == RegisterClass::Float) {
                if (float_reg_idx < 8) {
                    // flw/fld f(10+idx), offset(s0)
                    emitIType(assembler, 0b0000111, 10 + float_reg_idx++, (info.size == 32 ? 2 : 3), 8, cg.getStackOffset(arg));
                } else {
                    stack_args.push_back(arg);
                }
            } else {
                if (int_reg_idx < 8) {
                    emitLoadValue(cg, assembler, arg, 10 + int_reg_idx++);
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            TypeInfo info = getTypeInfo(arg->getType());
            if (info.regClass == RegisterClass::Float) {
                // flw/fld ft0, offset(s0)
                emitIType(assembler, 0b0000111, 0, (info.size == 32 ? 2 : 3), 8, cg.getStackOffset(arg));
                // addi sp, sp, -8
                emitIType(assembler, 0b0010011, 2, 0, 2, -8);
                // fsw/fsd ft0, 0(sp)
                emitSType(assembler, 0b0100111, (info.size == 32 ? 2 : 3), 2, 0, 0);
            } else {
                emitLoadValue(cg, assembler, arg, 5); // load into t0
                // addi sp, sp, -8
                emitIType(assembler, 0b0010011, 2, 0, 2, -8);
                // store t0, 0(sp)
                emitSType(assembler, 0b0100011, getStoreFunct3(arg->getType()), 2, 5, 0);
            }
        }

        // auipc ra, 0
        assembler.emitDWord(0x00000097);
        // jalr ra, <offset>(ra)
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0x00008067);

        if (!stack_args.empty()) {
            // addi sp, sp, stack_args.size() * 8
            emitIType(assembler, 0b0010011, 2, 0, 2, stack_args.size() * 8);
        }

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        reloc.symbolName = instr.getOperands()[0]->get()->getName();
        reloc.type = "R_RISCV_CALL";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            TypeInfo info = getTypeInfo(instr.getType());
            if (info.regClass == RegisterClass::Float) {
                // fsw/fsd fa0, destOffset(s0)
                emitSType(assembler, 0b0100111, (info.size == 32 ? 2 : 3), 8, 10, destOffset);
            } else {
                // store a0, destOffset(s0)
                emitSType(assembler, 0b0100011, getStoreFunct3(instr.getType()), 8, 10, destOffset);
            }
        }
    }
}

// === TargetInfo Implementation ===
TypeInfo RiscV64::getTypeInfo(const ir::Type* type) const {
    TypeInfo info;
    info.size = type->getSize() * 8; // bits
    info.align = type->getAlignment() * 8; // bits
    info.isFloatingPoint = type->isFloatingPoint();
    info.isSigned = true;
    
    if (type->isFloatingPoint()) {
        info.regClass = RegisterClass::Float;
    } else if (type->isVector()) {
        info.regClass = RegisterClass::Vector;
    } else {
        info.regClass = RegisterClass::Integer;
    }
    
    return info;
}

const std::vector<std::string>& RiscV64::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {
        "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
        "x5", "x6", "x7", "x28", "x29", "x30", "x31"
    };
    static const std::vector<std::string> floatRegs = {
        "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17",
        "f5", "f6", "f7", "f28", "f29", "f30", "f31"
    };
    static const std::vector<std::string> vectorRegs = {
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
    };
    
    switch (regClass) {
        case RegisterClass::Integer: return intRegs;
        case RegisterClass::Float: return floatRegs;
        case RegisterClass::Vector: return vectorRegs;
        default: return intRegs;
    }
}

const std::string& RiscV64::getReturnRegister(const ir::Type* type) const {
    if (type->isFloatingPoint()) {
        return getFloatReturnRegister();
    }
    return getIntegerReturnRegister();
}

void RiscV64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    // Reserve space for ra and s0 (16 bytes)
    currentStackOffset = -16;
    int int_reg_idx = 0;
    int float_reg_idx = 0;
    int stack_arg_idx = 0;

    for (auto& param : func.getParameters()) {
        TypeInfo info = getTypeInfo(param->getType());
        if (info.regClass == RegisterClass::Float) {
            if (float_reg_idx < 8) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[param.get()] = currentStackOffset;
                float_reg_idx++;
            } else {
                cg.getStackOffsets()[param.get()] = (stack_arg_idx++) * 8;
            }
        } else {
            if (int_reg_idx < 8) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[param.get()] = currentStackOffset;
                int_reg_idx++;
            } else {
                cg.getStackOffsets()[param.get()] = (stack_arg_idx++) * 8;
            }
        }
    }

    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
                currentStackOffset -= 8; // Allocate 8 bytes for value-producing instructions
                cg.getStackOffsets()[instr.get()] = currentStackOffset;
            }
            if (instr->getOpcode() == ir::Instruction::Call) hasCalls = true;
        }
    }

    int local_area_size = -currentStackOffset;

    // Optimization: leaf function with no locals and no parameters
    bool isSimpleLeaf = !hasCalls && local_area_size == 0 && func.getParameters().empty();
    if (isSimpleLeaf) {
        if (auto* os = cg.getTextStream()) {
            *os << "  # Simple leaf function: frame pointer and stack adjustment omitted\n";
        }
        return;
    }

    int aligned_stack_size = (local_area_size + 15) & ~15;

    emitPrologue(cg, aligned_stack_size);

    // Save arguments to stack
    int_reg_idx = 0;
    float_reg_idx = 0;
    for (auto& param : func.getParameters()) {
        TypeInfo info = getTypeInfo(param->getType());
        int32_t offset = cg.getStackOffset(param.get());

        if (info.regClass == RegisterClass::Float) {
            if (float_reg_idx < 8) {
                if (auto* os = cg.getTextStream()) {
                    std::string sInstr = (info.size == 32) ? "fsw" : "fsd";
                    *os << "  " << sInstr << " " << getFloatArgumentRegisters()[float_reg_idx] << ", " << offset << "(s0)\n";
                } else {
                    auto& assembler = cg.getAssembler();
                    // fsw/fsd faX, offset(s0)
                    uint8_t opcode = 0b0100111;
                    uint8_t funct3 = (info.size == 32) ? 2 : 3;
                    emitSType(assembler, opcode, funct3, 8, 10 + float_reg_idx, offset);
                }
                float_reg_idx++;
            }
        } else {
            if (int_reg_idx < 8) {
                if (auto* os = cg.getTextStream()) {
                    *os << "  " << getStoreInstr(param->getType()) << " " << getIntegerArgumentRegisters()[int_reg_idx] << ", " << offset << "(s0)\n";
                } else {
                    auto& assembler = cg.getAssembler();
                    // store aX, offset(s0)
                    emitSType(assembler, 0b0100011, getStoreFunct3(param->getType()), 8, 10 + int_reg_idx, offset);
                }
                int_reg_idx++;
            }
        }
    }
}

void RiscV64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) { hasCalls = true; break; }
        }
        if (hasCalls) break;
    }
    int local_area_size = -currentStackOffset;

    bool isSimpleLeaf = !hasCalls && local_area_size == 0 && func.getParameters().empty();
    if (isSimpleLeaf) {
        if (auto* os = cg.getTextStream()) {
            *os << "  ret\n";
        } else {
            // jr ra (jalr x0, x1, 0)
            emitIType(cg.getAssembler(), 0b1100111, 0, 0, 1, 0);
        }
        return;
    }

    emitEpilogue(cg);
}

void RiscV64::emitPassArgument(CodeGen& cg, size_t argIndex,
                              const std::string& value, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V ABI: Integer arguments in a0-a7, floating-point in fa0-fa7
        if (type->isFloatingPoint()) {
            const auto& floatRegs = getFloatArgumentRegisters();
            if (argIndex < floatRegs.size()) {
                *os << "  fmv.s " << floatRegs[argIndex] << ", " << value << "\n";
            } else {
                // Stack argument
                *os << "  addi sp, sp, -8\n";
                *os << "  fsw " << value << ", 0(sp)\n";
            }
        } else {
            const auto& intRegs = getIntegerArgumentRegisters();
            if (argIndex < intRegs.size()) {
                *os << "  mv " << intRegs[argIndex] << ", " << value << "\n";
            } else {
                // Stack argument
                *os << "  addi sp, sp, -8\n";
                *os << "  sd " << value << ", 0(sp)\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        if (type->isFloatingPoint()) {
             // FP binary emission handled in higher level functions
        } else {
            const auto& intRegs = getIntegerArgumentRegisters();
            if (argIndex < intRegs.size()) {
                // mv aX, reg (addi aX, reg, 0)
                // We don't have the register index here, only the name.
                // This is a limitation of the current TargetInfo interface.
            } else {
                // Stack argument
                emitIType(assembler, 0b0010011, 2, 0, 2, -8); // addi sp, sp, -8
                // sd reg, 0(sp)
            }
        }
    }
}

void RiscV64::emitGetArgument(CodeGen& cg, size_t argIndex,
                             const std::string& dest, const ir::Type* type) {
    (void)cg; (void)argIndex; (void)dest; (void)type;
    if (auto* os = cg.getTextStream()) {
        // RISC-V ABI: Integer arguments in a0-a7, floating-point in fa0-fa7
        if (type->isFloatingPoint()) {
            const auto& floatRegs = getFloatArgumentRegisters();
            if (argIndex < floatRegs.size()) {
                *os << "  fmv.s " << dest << ", " << floatRegs[argIndex] << "\n";
            } else {
                // Stack argument
                *os << "  flw " << dest << ", " << (argIndex - floatRegs.size()) * 8 << "(sp)\n";
            }
        } else {
            const auto& intRegs = getIntegerArgumentRegisters();
            if (argIndex < intRegs.size()) {
                *os << "  mv " << dest << ", " << intRegs[argIndex] << "\n";
            } else {
                // Stack argument
                *os << "  ld " << dest << ", " << (argIndex - intRegs.size()) * 8 << "(sp)\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        // Similar to emitPassArgument, we need register indices.
    }
}

void RiscV64::emitNot(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        *os << "  " << getLoadInstr(type) << " t0, " << opOperand << "\n";
        *os << "  not t0, t0\n";
        *os << "  " << getStoreInstr(type) << " t0, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t opOffset = cg.getStackOffset(op);
        int32_t destOffset = cg.getStackOffset(dest);
        emitIType(assembler, 0b0000011, 5, getLoadFunct3(type), 8, opOffset);
        emitIType(assembler, 0b0010011, 5, 4, 5, -1);          // xori t0, t0, -1
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 5, destOffset);
    }
}

void RiscV64::emitCast(CodeGen& cg, ir::Instruction& instr,
                      const ir::Type* fromType, const ir::Type* toType) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* operand = instr.getOperands()[0]->get();
        std::string srcOperand = cg.getValueAsOperand(operand);
        std::string destOperand = cg.getValueAsOperand(&instr);

        *os << "  ld t0, " << srcOperand << "\n";

        // Handle different cast types
        if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            *os << "  mv t1, t0\n";
        } else if (fromType->isIntegerTy() && toType->isFloatingPoint()) {
            bool is64 = fromType->getSize() == 8;
            if (toType->isDoubleTy()) {
                *os << (is64 ? "  fcvt.d.l ft1, t0\n" : "  fcvt.d.w ft1, t0\n");
            } else {
                *os << (is64 ? "  fcvt.s.l ft1, t0\n" : "  fcvt.s.w ft1, t0\n");
            }
        } else if (fromType->isFloatingPoint() && toType->isIntegerTy()) {
            bool to64 = toType->getSize() == 8;
            if (fromType->isDoubleTy()) {
                *os << (to64 ? "  fcvt.l.d t1, ft0\n" : "  fcvt.w.d t1, ft0\n");
            } else {
                *os << (to64 ? "  fcvt.l.s t1, ft0\n" : "  fcvt.w.s t1, ft0\n");
            }
        } else if (fromType->isFloatingPoint() && toType->isFloatingPoint()) {
            if (fromType->isDoubleTy() && toType->isFloatTy()) {
                *os << "  fcvt.s.d ft1, ft0\n";
            } else if (fromType->isFloatTy() && toType->isDoubleTy()) {
                *os << "  fcvt.d.s ft1, ft0\n";
            } else {
                *os << "  fmv.d ft1, ft0\n";
            }
        } else {
            *os << "  mv t1, t0\n";
        }

        if (toType->isFloatingPoint()) {
            if (toType->isDoubleTy()) {
                *os << "  fsd ft1, " << destOperand << "\n";
            } else {
                *os << "  fsw ft1, " << destOperand << "\n";
            }
        } else {
            *os << "  sd t1, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        if (fromType->isIntegerTy() && toType->isFloatTy()) {
            emitIType(assembler, 0b0000011, 5, 3, 8, srcOffset);       // ld t0, srcOffset(s0)
            emitRType(assembler, 0b1010011, 0, 0, 5, 0, 0b1101000);   // fcvt.s.l f0, t0
            emitSType(assembler, 0b0100111, 2, 8, 0, destOffset);    // fsw f0, destOffset(s0)
        } else if (fromType->isFloatTy() && toType->isIntegerTy()) {
            emitIType(assembler, 0b0000111, 0, 2, 8, srcOffset);       // flw f0, srcOffset(s0)
            emitRType(assembler, 0b1010011, 5, 0, 0, 0, 0b1100000);   // fcvt.l.s t0, f0
            emitSType(assembler, 0b0100011, 3, 8, 5, destOffset);    // sd t0, destOffset(s0)
        } else if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            emitIType(assembler, 0b0000011, 5, 3, 8, srcOffset);       // ld t0, srcOffset(s0)
            emitSType(assembler, 0b0100011, 3, 8, 5, destOffset);    // sd t0, destOffset(s0)
        } else {
            // Other cast types handled as no-ops or in binary-specific logic
        }
    }
}

void RiscV64::emitVAStart(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // In RISC-V, variadic arguments are passed on the stack after the named arguments
        // We need to set up the va_list structure to point to the first variadic argument
        // For simplicity, we'll assume the va_list is a pointer to the stack location

        ir::Value* dest = &instr;
        std::string destOperand = cg.getValueAsOperand(dest);

        // Get the address of the first variadic argument
        // This would typically be the stack pointer plus offset for named arguments
        *os << "  addi t0, sp, 16\n";  // Skip return address and frame pointer
        *os << "  sd t0, " << destOperand << "\n";
    } else {
        // Binary mode: VAStart stub
    }
}

void RiscV64::emitVAArg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Extract the next argument from the va_list
        ir::Value* vaList = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;

        std::string vaListOperand = cg.getValueAsOperand(vaList);
        std::string destOperand = cg.getValueAsOperand(dest);

        // Load the current va_list pointer
        *os << "  ld t0, " << vaListOperand << "\n";

        // Load the argument value
        *os << "  ld t1, 0(t0)\n";

        // Store the argument value
        *os << "  sd t1, " << destOperand << "\n";

        // Advance the va_list pointer
        *os << "  addi t0, t0, 8\n";
        *os << "  sd t0, " << vaListOperand << "\n";
    } else {
        // Binary mode: VAArg stub
    }
}

void RiscV64::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    (void)instr;
    if (auto* os = cg.getTextStream()) {
        // Clean up the va_list (typically a no-op on RISC-V)
        // Just add a comment for debugging purposes
        *os << "  # va_end called\n";
    } else {
        // Binary mode: VAEnd stub
    }
}

void RiscV64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V Syscall ABI: a7 (number), a0-a5 (args)
        *os << "  li a7, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            *os << "  " << getLoadInstr(instr.getOperands()[i]->get()->getType()) << " a" << (i-1) << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
        }
        *os << "  ecall\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  " << getStoreInstr(instr.getType()) << " a0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // Load syscall number into a7 (index 17)
        emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 17);
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), 10 + (i-1)); // a0 is 10
        }
        // ecall -> 0x00000073
        assembler.emitDWord(0x00000073);
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            emitSType(assembler, 0b0100011, getStoreFunct3(instr.getType()), 8, 10, destOffset);
        }
    }
}

void RiscV64::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* addr = instr.getOperands()[0]->get();
    const ir::Type* type = instr.getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string addrOperand = cg.getValueAsOperand(addr);
        *os << "  ld t0, " << addrOperand << "\n"; // Load pointer from stack slot
        *os << "  " << getLoadInstr(type) << " t1, 0(t0)\n";              // Dereference pointer
        *os << "  " << getStoreInstr(type) << " t1, " << destOperand << "\n"; // Store value to destination stack slot
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);

        // load t0, addr
        emitLoadValue(cg, assembler, addr, 5);
        // load t1, 0(t0)
        emitIType(assembler, 0b0000011, 6, getLoadFunct3(type), 5, 0);
        // store t1, destOffset(s0)
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 8, 6, destOffset);
    }
}

void RiscV64::emitStore(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* value = instr.getOperands()[0]->get();
    ir::Value* addr = instr.getOperands()[1]->get();
    const ir::Type* type = value->getType();

    if (auto* os = cg.getTextStream()) {
        std::string valueOperand = cg.getValueAsOperand(value);
        std::string addrOperand = cg.getValueAsOperand(addr);
        *os << "  " << getLoadInstr(type) << " t0, " << valueOperand << "\n"; // Load value from stack
        *os << "  ld t1, " << addrOperand << "\n"; // Load pointer from stack
        *os << "  " << getStoreInstr(type) << " t0, 0(t1)\n";              // Store value to memory pointed by t1
    } else {
        auto& assembler = cg.getAssembler();

        // load t0, value
        emitLoadValue(cg, assembler, value, 5);
        // load t1, addr
        emitLoadValue(cg, assembler, addr, 6);
        // store t0, 0(t1)
        emitSType(assembler, 0b0100011, getStoreFunct3(type), 6, 5, 0);
    }
}

void RiscV64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::ConstantInt* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get());
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  la t0, __heap_ptr\n";
        *os << "  ld t1, 0(t0)\n";
        *os << "  sd t1, " << pointerOffset << "(s0)\n";
        *os << "  addi t1, t1, " << alignedSize << "\n";
        *os << "  sd t1, 0(t0)\n";
    } else {
        auto& assembler = cg.getAssembler();
        // la t0, __heap_ptr
        // auipc t5, 0
        assembler.emitDWord(0x00000297);
        CodeGen::RelocationInfo reloc_la;
        reloc_la.offset = assembler.getCodeSize() - 4;
        reloc_la.symbolName = "__heap_ptr";
        reloc_la.type = "R_RISCV_GOT_HI20";
        reloc_la.sectionName = ".text";
        cg.addRelocation(reloc_la);
        // l[d/w] t0, 0(t5)
        emitIType(assembler, 0b0000011, 5, 3, 5, 0);

        // ld t1, 0(t0)
        emitIType(assembler, 0b0000011, 6, 3, 5, 0);

        // sd t1, pointerOffset(s0)
        emitSType(assembler, 0b0100011, 3, 8, 6, pointerOffset);

        // addi t1, t1, alignedSize
        emitIType(assembler, 0b0010011, 6, 0, 6, static_cast<int16_t>(alignedSize));

        // sd t1, 0(t0)
        emitSType(assembler, 0b0100011, 3, 5, 6, 0);
    }
}

void RiscV64::emitBr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* condition = instr.getOperands()[0]->get();
    const ir::Type* type = condition->getType();

    if (auto* os = cg.getTextStream()) {
        // Conditional branch - we expect a condition value and a target label
        ir::Value* target = instr.getOperands()[1]->get();

        std::string conditionOperand = cg.getValueAsOperand(condition);
        std::string targetOperand = cg.getValueAsOperand(target);

        // Load the condition
        *os << "  " << getLoadInstr(type) << " t0, " << conditionOperand << "\n";

        // Branch if condition is non-zero
        *os << "  bnez t0, " << targetOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t condOffset = cg.getStackOffset(condition);

        // load t0, condOffset(s0)
        emitIType(assembler, 0b0000011, 5, getLoadFunct3(type), 8, condOffset);

        // bnez t0, <target> (bne t0, x0, <target>)
        emitBType(assembler, 0b1100011, 1, 5, 0, 0);

        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        reloc.symbolName = instr.getOperands()[1]->get()->getName();
        reloc.type = "R_RISCV_BRANCH";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);
    }
}

void RiscV64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n# --- Executable Entry Point ---\n";
        *os << ".globl _start\n";
        *os << "_start:\n";
        *os << "  # Call the user's main function\n";
        *os << "  call main\n\n";
        *os << "  # Exit with the return code from main\n";
        *os << "  # a0 already contains the return value\n";
        *os << "  li a7, 93       # syscall number for exit\n";
        *os << "  ecall           # Invoke kernel\n";
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

        // auipc ra, 0
        assembler.emitDWord(0x00000097);
        // jalr ra, main(ra)
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0x00008067);

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        reloc.symbolName = "main";
        reloc.type = "R_RISCV_CALL";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);

        // li a7, 93 (addi a7, x0, 93)
        emitIType(assembler, 0b0010011, 17, 0, 0, 93);

        // ecall
        assembler.emitDWord(0x00000073);
    }
}

void RiscV64::emitJmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Unconditional jump to target
        ir::Value* target = instr.getOperands()[0]->get();
        std::string targetOperand = cg.getValueAsOperand(target);

        *os << "  j " << targetOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        // jal x0, <offset>
        emitJType(assembler, 0b1101111, 0, 0);

        CodeGen::RelocationInfo reloc;
        reloc.offset = assembler.getCodeSize() - 4;
        reloc.symbolName = instr.getOperands()[0]->get()->getName();
        reloc.type = "R_RISCV_JAL";
        reloc.sectionName = ".text";
        reloc.addend = 0;
        cg.addRelocation(reloc);
    }
}

// === SIMD/Vector Support ===
VectorCapabilities RiscV64::getVectorCapabilities() const {
    VectorCapabilities caps;
    caps.supportedWidths = {128, 256, 512};
    caps.supportsIntegerVectors = true;
    caps.supportsFloatVectors = true;
    caps.supportsDoubleVectors = true;
    caps.supportsFMA = true;
    caps.supportsGatherScatter = true;
    caps.supportsHorizontalOps = true;
    caps.supportsMaskedOps = true;
    caps.maxVectorWidth = 512;
    caps.simdExtension = "RVV";
    return caps;
}

bool RiscV64::supportsVectorWidth(unsigned width) const {
    return width <= 512 && (width == 128 || width == 256 || width == 512);
}

bool RiscV64::supportsVectorType(const ir::VectorType* type) const {
    return type->getBitWidth() <= 512;
}

void RiscV64::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string addr = cg.getValueAsOperand(instr.getOperands()[0]->get());

        // Emit vector load
        *os << "  vl.v " << dest << ", " << addr << "\n";
    }
}

void RiscV64::emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string addr = cg.getValueAsOperand(instr.getOperands()[1]->get());

        // Emit vector store
        *os << "  vs.v " << src << ", " << addr << "\n";
    }
}

void RiscV64::emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        // Emit shuffle instruction (simplified)
        *os << "  vslideup.vx " << dest << ", " << lhs << ", " << rhs << ", zero\n";
    }
}

// === Fused Instruction Support ===
void RiscV64::emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        auto& operands = instr.getOperands();
        std::string a = cg.getValueAsOperand(operands[0]->get());
        std::string b = cg.getValueAsOperand(operands[1]->get());
        std::string c = cg.getValueAsOperand(operands[2]->get());

        // Emit fused multiply-add
        *os << "  vfmacc.vv " << dest << ", " << a << ", " << b << ", " << c << "\n";
    }
}

void RiscV64::emitLoadAndOperate(CodeGen& cg, ir::Instruction& load, ir::Instruction& op) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&op);
        std::string addr = cg.getValueAsOperand(load.getOperands()[0]->get());

        // Emit fused load-operate
        switch (op.getOpcode()) {
            case ir::Instruction::Add:
                *os << "  vladd.v " << dest << ", " << addr << "\n";
                break;
            case ir::Instruction::Mul:
                *os << "  vlmul.v " << dest << ", " << addr << "\n";
                break;
            default:
                // Fallback to separate instructions
                *os << "  vl.v t0, " << addr << "\n";
                *os << "  # Apply operation separately\n";
                break;
        }
    }
}

void RiscV64::emitComplexAddressing(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Emit complex addressing mode
        *os << "  # Complex addressing mode fusion not yet implemented\n";
    }
}

// Debug information

// === Additional SIMD/Vector Support ===
unsigned RiscV64::getOptimalVectorWidth(const ir::Type* elementType) const {
    // Return the optimal vector width for the given element type
    // For RISC-V Vector, we'll return 128-bit as a good default
    return 128;
}

void RiscV64::emitVectorGather(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V Vector gather operation
        // This is a simplified implementation - in practice, this would use vrgather instructions
        std::string dest = cg.getValueAsOperand(&instr);
        std::string base = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string indices = cg.getValueAsOperand(instr.getOperands()[1]->get());

        *os << "  # Vector gather operation\n";
        *os << "  vrgather.vv " << dest << ", " << base << ", " << indices << "\n";
    }
}

void RiscV64::emitVectorScatter(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V Vector scatter operation
        // This is a simplified implementation - in practice, this would use vssub instructions
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string base = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string indices = cg.getValueAsOperand(instr.getOperands()[2]->get());

        *os << "  # Vector scatter operation\n";
        *os << "  vssub.vv " << base << ", " << src << ", " << indices << "\n";
    }
}

void RiscV64::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string instrName;
        switch (instr.getOpcode()) {
            case ir::Instruction::VAdd: instrName = "vadd.vv"; break;
            case ir::Instruction::VSub: instrName = "vsub.vv"; break;
            case ir::Instruction::VMul: instrName = "vmul.vv"; break;
            case ir::Instruction::VDiv: instrName = "vdiv.vv"; break;
            case ir::Instruction::VFAdd: instrName = "vfadd.vv"; break;
            case ir::Instruction::VFSub: instrName = "vfsub.vv"; break;
            case ir::Instruction::VFMul: instrName = "vfmul.vv"; break;
            case ir::Instruction::VFDiv: instrName = "vfdiv.vv"; break;
            default: return;
        }
        *os << "  # Load vector operands (not implemented in simple text mode)\n";
        *os << "  " << instrName << " " << dest << ", " << lhs << ", " << rhs << "\n";
    } else {
        // Binary emission placeholder for RVV
    }
}

void RiscV64::emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(&instr);
        *os << "  vneg.v " << dest << ", " << src << "\n";
    } else {
        // Binary emission placeholder
    }
}

void RiscV64::emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Handle vector logical operations based on the instruction opcode
        switch (instr.getOpcode()) {
            case ir::Instruction::VAnd:
            case ir::Instruction::VOr:
            case ir::Instruction::VXor:
            case ir::Instruction::VShl:
            case ir::Instruction::VShr:
                *os << "  # Vector logical op not implemented\n";
                break;
            default:
                *os << "  # Unsupported vector logical operation\n";
                break;
        }
    } else {
        // Binary emission placeholder
    }
}

void RiscV64::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V Vector horizontal operations
        // This is a simplified implementation - in practice, this would use reduction instructions
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        *os << "  # Vector horizontal operation\n";
        switch (instr.getOpcode()) {
            case ir::Instruction::VHAdd:
                *os << "  vredsum.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHMul:
                *os << "  vredmul.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHAnd:
                *os << "  vredand.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHOr:
                *os << "  vredor.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHXor:
                *os << "  vredxor.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            default:
                *os << "  # Unsupported horizontal operation\n";
                break;
        }
    }
}

void RiscV64::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // RISC-V Vector reduction operations
        // This is a simplified implementation - in practice, this would use specific reduction instructions
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        *os << "  # Vector reduction operation\n";
        switch (instr.getOpcode()) {
            case ir::Instruction::VHAdd:
                *os << "  vredsum.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHMul:
                *os << "  vredmul.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHAnd:
                *os << "  vredand.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHOr:
                *os << "  vredor.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            case ir::Instruction::VHXor:
                *os << "  vredxor.vs " << dest << ", " << src << ", " << dest << "\n";
                break;
            default:
                *os << "  # Unsupported reduction operation\n";
                break;
        }
    }
}

void RiscV64::emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        // Emit vector comparison (simplified implementation)
        *os << "  vmseq.vv " << dest << ", " << lhs << ", " << rhs << "\n";
    } else {
        // Binary emission placeholder
    }
}

void RiscV64::emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        // Emit vector broadcast (simplified implementation)
        *os << "  vpbroadcast " << dest << ", " << src << "\n";
    } else {
        // Binary emission placeholder
    }
}

void RiscV64::emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        // Emit vector extract (simplified implementation)
        *os << "  vextract " << dest << ", " << src << "\n";
    } else {
        // Binary emission placeholder
    }
}

void RiscV64::emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        // Emit vector insert (simplified implementation)
        *os << "  vinsert " << dest << ", " << src << "\n";
    } else {
        // Binary emission placeholder
    }
}

// === Fused Instruction Support ===
bool RiscV64::supportsFusedPattern(FusedPattern pattern) const {
    // RISC-V supports several fused patterns
    switch (pattern) {
        case FusedPattern::MultiplyAdd:
        case FusedPattern::LoadAndOperate:
        case FusedPattern::AddressCalculation:
            return true;
        default:
            return false;
    }
}

// === Register Information ===
bool RiscV64::isCallerSaved(const std::string& reg) const {
    // In RISC-V, caller-saved registers include:
    // - Temporary registers: t0-t6 (x5-x31)
    // - Argument registers: a0-a7 (x10-x17)
    // - Return registers: a0-a1 (x10-x11)
    // - Floating-point temporaries: ft0-ft11 (f0-f11)
    // - Floating-point arguments: fa0-fa7 (f10-f17)
    
    return (reg[0] == 't' || reg[0] == 'a' || reg[0] == 'f');
}

bool RiscV64::isCalleeSaved(const std::string& reg) const {
    // In RISC-V, callee-saved registers include:
    // - Saved registers: s0-s11 (x8-x19)
    // - Global pointer: gp (x3)
    // - Thread pointer: tp (x4)
    // - Frame pointer: fp/s0 (x8)
    // - Floating-point saved: fs0-fs11 (f8-f19)
    
    return (reg[0] == 's' || reg == "gp" || reg == "tp" || reg == "fp");
}

// === Fused Multiply Subtract (placeholder implementation) ===
void RiscV64::emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        const auto& operands = instr.getOperands();
        std::string a = cg.getValueAsOperand(operands[0]->get());
        std::string b = cg.getValueAsOperand(operands[1]->get());
        std::string c = cg.getValueAsOperand(operands[2]->get());

        // Emit fused multiply-subtract (simplified)
        *os << "  # Fused multiply-subtract: " << dest << " = " << a << " * " << b << " - " << c << "\n";
        *os << "  vfnmsac.vv " << dest << ", " << a << ", " << b << ", " << c << "\n";
    }
}

// === Debug and Runtime Support ===
void RiscV64::emitDebugInfo(CodeGen& cg, const ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // Emit debug information for the function
        *os << "  # Debug info for function: " << func.getName() << "\n";
    }
}

void RiscV64::emitLineInfo(CodeGen& cg, unsigned line, const std::string& file) {
    if (auto* os = cg.getTextStream()) {
        // Emit line information
        *os << "  # Line " << line << " in file " << file << "\n";
    }
}

void RiscV64::emitProfilingHook(CodeGen& cg, const std::string& hookName) {
    if (auto* os = cg.getTextStream()) {
        // Emit profiling hook
        *os << "  # Profiling hook: " << hookName << "\n";
    }
}

void RiscV64::emitStackUnwindInfo(CodeGen& cg, const ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // Emit stack unwind information
        *os << "  # Stack unwind info for function: " << func.getName() << "\n";
    }
}

// SIMD instruction helpers
SIMDContext RiscV64::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    // Set element suffix based on element type
    const ir::Type* elemType = type->getElementType();
    if (elemType->isIntegerTy()) {
        ctx.elementSuffix = "i";
    } else if (elemType->isFloatTy()) {
        ctx.elementSuffix = "s";
    } else if (elemType->isDoubleTy()) {
        ctx.elementSuffix = "d";
    }
    // Set width suffix based on vector width
    if (ctx.vectorWidth <= 128) {
        ctx.widthSuffix = "x";
    } else if (ctx.vectorWidth <= 256) {
        ctx.widthSuffix = "y";
    } else {
        ctx.widthSuffix = "z";
    }
    return ctx;
}

std::string RiscV64::getVectorRegister(const std::string& baseReg, unsigned width) const {
    // Return vector register name based on base register and width
    return baseReg; // Simplified implementation
}

std::string RiscV64::getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const {
    // Return vector instruction name based on base instruction and context
    return baseInstr; // Simplified implementation
}

} // namespace target
} // namespace codegen