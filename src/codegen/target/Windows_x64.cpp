#include "codegen/target/Windows_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include <ostream>
#include <algorithm>

namespace {
    using namespace codegen;
    void emitRegMem(execgen::Assembler& assembler, uint8_t rex, const std::vector<uint8_t>& opcodes, uint8_t reg, int32_t offset) {
        if (rex) assembler.emitByte(rex);
        for (uint8_t op : opcodes) assembler.emitByte(op);
        if (offset >= -128 && offset <= 127) {
            assembler.emitByte(0x45 | ((reg & 7) << 3));
            assembler.emitByte(static_cast<int8_t>(offset));
        } else {
            assembler.emitByte(0x85 | ((reg & 7) << 3));
            assembler.emitDWord(static_cast<uint32_t>(offset));
        }
    }

    void emitRegMem(execgen::Assembler& assembler, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
        emitRegMem(assembler, rex, std::vector<uint8_t>{opcode}, reg, offset);
    }

    void emitLoadValue(CodeGen& cg, execgen::Assembler& assembler, ir::Value* val, uint8_t reg) {
        if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
            uint8_t rex = 0x48;
            if (reg >= 8) rex |= 0x01; // REX.B
            assembler.emitByte(rex);
            assembler.emitByte(0xB8 + (reg & 7));
            assembler.emitQWord(constInt->getValue());
        } else {
            int32_t offset = cg.getStackOffset(val);
            uint8_t rex = 0x48;
            if (reg >= 8) rex |= 0x04; // REX.R
            emitRegMem(assembler, rex, 0x8B, reg, offset);
        }
    }
}

namespace codegen {
namespace target {

Windows_x64::Windows_x64() {
    initRegisters();
}

void Windows_x64::initRegisters() {
    // Integer registers
    integerRegs = {"%rax", "%rbx", "%rcx", "%rdx", "%rsi", "%rdi",
                   "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15"};

    // Floating-point registers
    for (int i = 0; i < 16; ++i) {
        floatRegs.push_back("%xmm" + std::to_string(i));
    }

    // Vector registers (same as float for now)
    vectorRegs = floatRegs;

    // Caller-saved registers (Windows x64)
    callerSaved = {
        {"%rax", true}, {"%rcx", true}, {"%rdx", true},
        {"%r8", true}, {"%r9", true}, {"%r10", true}, {"%r11", true}
    };
    for (int i = 0; i < 6; ++i) {
        callerSaved["%xmm" + std::to_string(i)] = true;
    }

    // Callee-saved registers (Windows x64)
    calleeSaved = {
        {"%rbx", true}, {"%rbp", true}, {"%rdi", true}, {"%rsi", true},
        {"%r12", true}, {"%r13", true}, {"%r14", true}, {"%r15", true}
    };
    for (int i = 6; i < 16; ++i) {
        calleeSaved["%xmm" + std::to_string(i)] = true;
    }

    // Return registers
    intReturnReg = "%rax";
    floatReturnReg = "%xmm0";

    // Stack and frame pointers
    stackPtrReg = "rsp";
    framePtrReg = "rbp";

    // Argument registers for Windows x64 ABI
    integerArgRegs = {"%rcx", "%rdx", "%r8", "%r9"};
    floatArgRegs = {"%xmm0", "%xmm1", "%xmm2", "%xmm3"};
}

void Windows_x64::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        *os << "  pushq %rbp\n";
        *os << "  movq %rsp, %rbp\n";
        // Allocate shadow store (32 bytes) + space for local variables
        int total_stack = stack_size + 32;
        // Ensure 16-byte alignment
        if (total_stack % 16 != 0) {
            total_stack += 16 - (total_stack % 16);
        }
        if (total_stack > 0) {
            *os << "  subq $" << total_stack << ", %rsp\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // pushq %rbp
        assembler.emitByte(0x55);
        // movq %rsp, %rbp
        assembler.emitByte(0x48);
        assembler.emitByte(0x89);
        assembler.emitByte(0xE5);
        // subq $size, %rsp
        int total_stack = stack_size + 32;
        if (total_stack % 16 != 0) total_stack += 16 - (total_stack % 16);
        if (total_stack > 0) {
            if (total_stack <= 127) {
                assembler.emitByte(0x48);
                assembler.emitByte(0x83);
                assembler.emitByte(0xEC);
                assembler.emitByte(static_cast<uint8_t>(total_stack));
            } else {
                assembler.emitByte(0x48);
                assembler.emitByte(0x81);
                assembler.emitByte(0xEC);
                assembler.emitDWord(static_cast<uint32_t>(total_stack));
            }
        }
    }
}

void Windows_x64::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  leave\n";
        *os << "  ret\n";
    } else {
        auto& assembler = cg.getAssembler();
        // leave
        assembler.emitByte(0xC9);
        // ret
        assembler.emitByte(0xC3);
    }
}

const std::vector<std::string>& Windows_x64::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> regs = {"%rcx", "%rdx", "%r8", "%r9"};
    return regs;
}

const std::vector<std::string>& Windows_x64::getFloatArgumentRegisters() const {
    static const std::vector<std::string> regs = {"%xmm0", "%xmm1", "%xmm2", "%xmm3"};
    return regs;
}

const std::string& Windows_x64::getIntegerReturnRegister() const {
    static const std::string reg = "%rax";
    return reg;
}

const std::string& Windows_x64::getFloatReturnRegister() const {
    static const std::string reg = "%xmm0";
    return reg;
}

void Windows_x64::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (!instr.getOperands().empty()) {
        ir::Value* retVal = instr.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            std::string retval = cg.getValueAsOperand(retVal);
            *os << "  movq " << retval << ", " << getIntegerReturnRegister() << "\n";
        } else {
            auto& assembler = cg.getAssembler();
            if (auto* constInt = dynamic_cast<ir::ConstantInt*>(retVal)) {
                // mov rax, imm
                assembler.emitByte(0x48); assembler.emitByte(0xB8);
                assembler.emitQWord(constInt->getValue());
            } else {
                int32_t offset = cg.getStackOffset(retVal);
                // mov rax, [rbp + offset]
                emitRegMem(assembler, 0x48, 0x8B, 0, offset);
            }
        }
    }
    emitFunctionEpilogue(cg, *instr.getParent()->getParent());
}

void Windows_x64::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  addq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // add rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x03, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  subq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // sub rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x2B, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  imulq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // imul rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, {0x0F, 0xAF}, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        std::string sizeSuffix = getLoadStoreSuffix(type);
        bool isUnsigned = (instr.getOpcode() == ir::Instruction::Udiv);

        if (isUnsigned) {
             if (sizeSuffix == "b") {
                  *os << "  movb " << lhsOperand << ", %al\n"; *os << "  movb $0, %ah\n"; *os << "  divb " << rhsOperand << "\n"; *os << "  movb %al, " << destOperand << "\n";
             } else if (sizeSuffix == "w") {
                  *os << "  movw " << lhsOperand << ", %ax\n"; *os << "  movw $0, %dx\n"; *os << "  divw " << rhsOperand << "\n"; *os << "  movw %ax, " << destOperand << "\n";
             } else if (sizeSuffix == "l") {
                  *os << "  movl " << lhsOperand << ", %eax\n"; *os << "  movl $0, %edx\n"; *os << "  divl " << rhsOperand << "\n"; *os << "  movl %eax, " << destOperand << "\n";
             } else {
                  *os << "  movq " << lhsOperand << ", %rax\n"; *os << "  movq $0, %rdx\n"; *os << "  divq " << rhsOperand << "\n"; *os << "  movq %rax, " << destOperand << "\n";
             }
        } else {
             if (sizeSuffix == "b") {
                  *os << "  movb " << lhsOperand << ", %al\n"; *os << "  cbw\n"; *os << "  idivb " << rhsOperand << "\n"; *os << "  movb %al, " << destOperand << "\n";
             } else if (sizeSuffix == "w") {
                  *os << "  movw " << lhsOperand << ", %ax\n"; *os << "  cwd\n"; *os << "  idivw " << rhsOperand << "\n"; *os << "  movw %ax, " << destOperand << "\n";
             } else if (sizeSuffix == "l") {
                  *os << "  movl " << lhsOperand << ", %eax\n"; *os << "  cltd\n"; *os << "  idivl " << rhsOperand << "\n"; *os << "  movl %eax, " << destOperand << "\n";
             } else {
                  *os << "  movq " << lhsOperand << ", %rax\n"; *os << "  cqto\n"; *os << "  idivq " << rhsOperand << "\n"; *os << "  movq %rax, " << destOperand << "\n";
             }
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        bool isUnsigned = (instr.getOpcode() == ir::Instruction::Udiv);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);

        if (isUnsigned) {
            // xor rdx, rdx
            assembler.emitBytes({0x48, 0x31, 0xD2});
            // div qword ptr [rbp + rhsOffset]
            int32_t rhsOffset = cg.getStackOffset(rhs);
            emitRegMem(assembler, 0x48, 0xF7, 6, rhsOffset); // /6 is DIV
        } else {
            // cqto
            assembler.emitBytes({0x48, 0x99});
            // idiv qword ptr [rbp + rhsOffset]
            int32_t rhsOffset = cg.getStackOffset(rhs);
            emitRegMem(assembler, 0x48, 0xF7, 7, rhsOffset); // /7 is IDIV
        }

        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        std::string sizeSuffix = getLoadStoreSuffix(type);
        bool isUnsigned = (instr.getOpcode() == ir::Instruction::Urem);

        if (isUnsigned) {
             if (sizeSuffix == "b") {
                  *os << "  movb " << lhsOperand << ", %al\n"; *os << "  movb $0, %ah\n"; *os << "  divb " << rhsOperand << "\n"; *os << "  movb %ah, " << destOperand << "\n";
             } else if (sizeSuffix == "w") {
                  *os << "  movw " << lhsOperand << ", %ax\n"; *os << "  movw $0, %dx\n"; *os << "  divw " << rhsOperand << "\n"; *os << "  movw %dx, " << destOperand << "\n";
             } else if (sizeSuffix == "l") {
                  *os << "  movl " << lhsOperand << ", %eax\n"; *os << "  movl $0, %edx\n"; *os << "  divl " << rhsOperand << "\n"; *os << "  movl %edx, " << destOperand << "\n";
             } else {
                  *os << "  movq " << lhsOperand << ", %rax\n"; *os << "  movq $0, %rdx\n"; *os << "  divq " << rhsOperand << "\n"; *os << "  movq %rdx, " << destOperand << "\n";
             }
        } else {
             if (sizeSuffix == "b") {
                  *os << "  movb " << lhsOperand << ", %al\n"; *os << "  cbw\n"; *os << "  idivb " << rhsOperand << "\n"; *os << "  movb %ah, " << destOperand << "\n";
             } else if (sizeSuffix == "w") {
                  *os << "  movw " << lhsOperand << ", %ax\n"; *os << "  cwd\n"; *os << "  idivw " << rhsOperand << "\n"; *os << "  movw %dx, " << destOperand << "\n";
             } else if (sizeSuffix == "l") {
                  *os << "  movl " << lhsOperand << ", %eax\n"; *os << "  cltd\n"; *os << "  idivl " << rhsOperand << "\n"; *os << "  movl %edx, " << destOperand << "\n";
             } else {
                  *os << "  movq " << lhsOperand << ", %rax\n"; *os << "  cqto\n"; *os << "  idivq " << rhsOperand << "\n"; *os << "  movq %rdx, " << destOperand << "\n";
             }
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);
        bool isUnsigned = (instr.getOpcode() == ir::Instruction::Urem);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);

        if (isUnsigned) {
            // xor rdx, rdx
            assembler.emitBytes({0x48, 0x31, 0xD2});
            // div qword ptr [rbp + rhsOffset]
            int32_t rhsOffset = cg.getStackOffset(rhs);
            emitRegMem(assembler, 0x48, 0xF7, 6, rhsOffset);
        } else {
            // cqto
            assembler.emitBytes({0x48, 0x99});
            // idiv qword ptr [rbp + rhsOffset]
            int32_t rhsOffset = cg.getStackOffset(rhs);
            emitRegMem(assembler, 0x48, 0xF7, 7, rhsOffset);
        }

        // mov [rbp + destOffset], rdx
        emitRegMem(assembler, 0x48, 0x89, 2, destOffset); // rdx is register index 2
    }
}

void Windows_x64::emitAnd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  andq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // and rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x23, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitOr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  orq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // or rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x0B, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitXor(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  xorq " << rhsOperand << ", %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // xor rax, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x33, 0, rhsOffset);
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitShl(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  movq " << rhsOperand << ", %rcx\n";
        *os << "  shlq %cl, %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // mov rcx, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x8B, 1, rhsOffset);
        // shl rax, cl
        assembler.emitBytes({0x48, 0xD3, 0xE0});
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitShr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  movq " << rhsOperand << ", %rcx\n";
        *os << "  shrq %cl, %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // mov rcx, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x8B, 1, rhsOffset);
        // shr rax, cl
        assembler.emitBytes({0x48, 0xD3, 0xE8});
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitSar(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string lhsOperand = cg.getValueAsOperand(lhs);
        std::string rhsOperand = cg.getValueAsOperand(rhs);
        *os << "  movq " << lhsOperand << ", %rax\n";
        *os << "  movq " << rhsOperand << ", %rcx\n";
        *os << "  sarq %cl, %rax\n";
        *os << "  movq %rax, " << destOperand << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + lhsOffset]
        int32_t lhsOffset = cg.getStackOffset(lhs);
        emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
        // mov rcx, [rbp + rhsOffset]
        int32_t rhsOffset = cg.getStackOffset(rhs);
        emitRegMem(assembler, 0x48, 0x8B, 1, rhsOffset);
        // sar rax, cl
        assembler.emitBytes({0x48, 0xD3, 0xF8});
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* op = instr.getOperands()[0]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        if (op->getType()->isFloatTy()) {
            *os << "  movss " << opOperand << ", %xmm0\n";
            *os << "  xorps " << getLabelPrefix() << "neg_mask_float(%rip), %xmm0\n";
            *os << "  movss %xmm0, " << destOperand << "\n";
        } else if (op->getType()->isDoubleTy()) {
            *os << "  movsd " << opOperand << ", %xmm0\n";
            *os << "  xorpd " << getLabelPrefix() << "neg_mask_double(%rip), %xmm0\n";
            *os << "  movsd %xmm0, " << destOperand << "\n";
        } else {
            *os << "  movq " << opOperand << ", %rax\n";
            *os << "  negq %rax\n";
            *os << "  movq %rax, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* op = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t opOffset = cg.getStackOffset(op);
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + opOffset]
        emitRegMem(assembler, 0x48, 0x8B, 0, opOffset);
        // neg rax
        assembler.emitBytes({0x48, 0xF7, 0xD8});
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* src = instr.getOperands()[0]->get();
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string srcOperand = cg.getValueAsOperand(src);
        if (src->getType()->isFloatTy()) {
            *os << "  movss " << srcOperand << ", %xmm0\n";
            *os << "  movss %xmm0, " << destOperand << "\n";
        } else if (src->getType()->isDoubleTy()) {
            *os << "  movsd " << srcOperand << ", %xmm0\n";
            *os << "  movsd %xmm0, " << destOperand << "\n";
        } else {
            *os << "  movq " << srcOperand << ", %rax\n";
            *os << "  movq %rax, " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        if (src->getType()->isFloatingPoint()) {
            uint8_t prefix = (src->getType()->getSize() == 4) ? 0xF3 : 0xF2;
            // movss xmm0, [rbp + srcOffset]
            emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, srcOffset);
            // movss [rbp + destOffset], xmm0
            emitRegMem(assembler, 0, {prefix, 0x0F, 0x11}, 0, destOffset);
        } else {
            // mov rax, [rbp + srcOffset]
            emitRegMem(assembler, 0x48, 0x8B, 0, srcOffset);
            // mov [rbp + destOffset], rax
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
        }
    }
}

void Windows_x64::emitCall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const auto& int_regs = getIntegerArgumentRegisters();
        const auto& float_regs = getFloatArgumentRegisters();
        unsigned int_reg_idx = 0;
        unsigned float_reg_idx = 0;

        ir::Value* calleeValue = instr.getOperands()[0]->get();
        std::string callee = cg.getValueAsOperand(calleeValue);
        std::vector<ir::Value*> stack_args;
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            if (dynamic_cast<ir::IntegerType*>(arg->getType())) {
                if (int_reg_idx < int_regs.size()) {
                    std::string argOperand = cg.getValueAsOperand(arg);
                    *os << "  movq " << argOperand << ", " << int_regs[int_reg_idx++] << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        size_t shadow_space = 32;
        size_t space_for_args = stack_args.size() * 8;
        size_t total_stack_space = shadow_space + space_for_args;

        if ((total_stack_space % 16) != 0) {
            total_stack_space += 16 - (total_stack_space % 16);
        }

        if (total_stack_space > 0) {
            *os << "  subq $" << total_stack_space << ", %rsp\n";
        }

        for (size_t i = 0; i < stack_args.size(); ++i) {
            std::string argOperand = cg.getValueAsOperand(stack_args[i]);
            *os << "  movq " << argOperand << ", " << shadow_space + (i * 8) << "(%rsp)\n";
        }

        *os << "  call " << callee << "\n";

        if (total_stack_space > 0) {
            *os << "  addq $" << total_stack_space << ", %rsp\n";
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string destOperand = cg.getValueAsOperand(&instr);
            *os << "  movq " << getIntegerReturnRegister() << ", " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        unsigned int_reg_idx = 0;
        ir::Value* calleeValue = instr.getOperands()[0]->get();
        std::vector<ir::Value*> stack_args;

        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            if (arg->getType()->isIntegerTy()) {
                if (int_reg_idx < 4) {
                    uint8_t reg = 0;
                    switch(int_reg_idx) {
                        case 0: reg = 1; break; // rcx
                        case 1: reg = 2; break; // rdx
                        case 2: reg = 8; break; // r8
                        case 3: reg = 9; break; // r9
                    }
                    // Load arg into reg
                    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(arg)) {
                        uint8_t rex = 0x48;
                        if (reg >= 8) rex |= 0x01; // REX.B=1 for dest register
                        assembler.emitByte(rex);
                        assembler.emitByte(0xB8 + (reg & 7));
                        assembler.emitQWord(constInt->getValue());
                    } else {
                        int32_t offset = cg.getStackOffset(arg);
                        uint8_t rex = 0x48;
                        if (reg >= 8) rex |= 0x04; // REX.R=1 for dest register
                        assembler.emitByte(rex);
                        assembler.emitByte(0x8B);
                        assembler.emitByte(0x45 | ((reg & 7) << 3));
                        assembler.emitByte(static_cast<uint8_t>(offset));
                    }
                    int_reg_idx++;
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        size_t shadow_space = 32;
        size_t total_stack_space = shadow_space + (stack_args.size() * 8);
        if (total_stack_space % 16 != 0) total_stack_space += 16 - (total_stack_space % 16);

        if (total_stack_space > 0) {
            assembler.emitBytes({0x48, 0x81, 0xEC});
            assembler.emitDWord(static_cast<uint32_t>(total_stack_space));
        }

        for (size_t i = 0; i < stack_args.size(); ++i) {
            // mov rax, [rbp + argOffset]
            int32_t argOffset = cg.getStackOffset(stack_args[i]);
            emitRegMem(assembler, 0x48, 0x8B, 0, argOffset);
            // mov [rsp + shadow_space + i*8], rax
            // For [rsp + offset], we need to be careful. rsp is RM=4.
            // SIB byte is needed.
            // Simplified: we'll use [rsp + disp8] or [rsp + disp32]
            uint8_t disp_offset = static_cast<uint8_t>(shadow_space + i*8);
            assembler.emitBytes({0x48, 0x89, 0x44, 0x24, disp_offset});
        }

        // call rel32
        assembler.emitByte(0xE8);
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        reloc.symbolName = calleeValue->getName();
        reloc.type = "R_X86_64_PC32";
        reloc.sectionName = ".text";
        reloc.addend = -4;
        cg.addRelocation(reloc);

        if (total_stack_space > 0) {
            assembler.emitBytes({0x48, 0x81, 0xC4});
            assembler.emitDWord(static_cast<uint32_t>(total_stack_space));
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            // mov [rbp + destOffset], rax
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
        }
    }
}

// === SIMD/Vector Support ===
void Windows_x64::emitVectorAdd(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get vector type information
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        unsigned elemCount = vecType->getNumElements();

        // Determine instruction suffix based on element type
        std::string suffix = getVectorSuffix(elemType, elemCount);
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());

        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        // Emit vector addition
        if (elemType->isIntegerTy()) {
            *os << "  " << widthPrefix << "padd" << suffix << " " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isFloatTy()) {
            *os << "  " << widthPrefix << "addps " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isDoubleTy()) {
            *os << "  " << widthPrefix << "addpd " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        // Load lhs into xmm0, rhs into xmm1
        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = 0;
            if (bitWidth == 8) opcode = 0xFC;
            else if (bitWidth == 16) opcode = 0xFD;
            else if (bitWidth == 32) opcode = 0xFE;
            else opcode = 0xD4;
            assembler.emitBytes({0x66, 0x0F, opcode, 0xC1}); // padd xmm0, xmm1
        } else if (elemType->isFloatTy()) {
            assembler.emitBytes({0x0F, 0x58, 0xC1}); // addps xmm0, xmm1
        } else {
            assembler.emitBytes({0x66, 0x0F, 0x58, 0xC1}); // addpd xmm0, xmm1
        }
        // Store xmm0 to dest
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorSub(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        unsigned elemCount = vecType->getNumElements();
        std::string suffix = getVectorSuffix(elemType, elemCount);
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        if (elemType->isIntegerTy()) {
            *os << "  " << widthPrefix << "psub" << suffix << " " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isFloatTy()) {
            *os << "  " << widthPrefix << "subps " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isDoubleTy()) {
            *os << "  " << widthPrefix << "subpd " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = (bitWidth == 8) ? 0xF8 : (bitWidth == 16) ? 0xF9 : (bitWidth == 32) ? 0xFA : 0xFB;
            assembler.emitBytes({0x66, 0x0F, opcode, 0xC1});
        } else if (elemType->isFloatTy()) {
            assembler.emitBytes({0x0F, 0x5C, 0xC1});
        } else {
            assembler.emitBytes({0x66, 0x0F, 0x5C, 0xC1});
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorMul(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        unsigned elemCount = vecType->getNumElements();
        std::string suffix = getVectorSuffix(elemType, elemCount);
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        if (elemType->isIntegerTy()) {
            *os << "  " << widthPrefix << "pmul" << suffix << " " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isFloatTy()) {
            *os << "  " << widthPrefix << "mulps " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isDoubleTy()) {
            *os << "  " << widthPrefix << "mulpd " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = (bitWidth == 16) ? 0xD5 : (bitWidth == 32) ? 0x38 : 0x38; // Simplified
            if (bitWidth == 16) assembler.emitBytes({0x66, 0x0F, 0xD5, 0xC1}); // pmullw
            else assembler.emitBytes({0x66, 0x0F, 0x38, 0x40, 0xC1}); // pmulld
        } else if (elemType->isFloatTy()) {
            assembler.emitBytes({0x0F, 0x59, 0xC1}); // mulps
        } else {
            assembler.emitBytes({0x66, 0x0F, 0x59, 0xC1}); // mulpd
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorDiv(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        if (elemType->isFloatTy()) {
            *os << "  " << widthPrefix << "divps " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        } else if (elemType->isDoubleTy()) {
            *os << "  " << widthPrefix << "divpd " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isFloatTy()) {
            assembler.emitBytes({0x0F, 0x5E, 0xC1});
        } else if (elemType->isDoubleTy()) {
            assembler.emitBytes({0x66, 0x0F, 0x5E, 0xC1});
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorShl(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        unsigned elemCount = vecType->getNumElements();
        std::string suffix = getVectorSuffix(elemType, elemCount);
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        if (elemType->isIntegerTy()) {
            *os << "  " << widthPrefix << "psll" << suffix << " " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = (bitWidth == 16) ? 0xF1 : (bitWidth == 32) ? 0xF2 : 0xF3;
            assembler.emitBytes({0x66, 0x0F, opcode, 0xC1});
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorShr(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        unsigned elemCount = vecType->getNumElements();
        std::string suffix = getVectorSuffix(elemType, elemCount);
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        if (elemType->isIntegerTy()) {
            *os << "  " << widthPrefix << "psrl" << suffix << " " << rhs << ", " << lhs << "\n";
            *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = (bitWidth == 16) ? 0xD1 : (bitWidth == 32) ? 0xD2 : 0xD3;
            assembler.emitBytes({0x66, 0x0F, opcode, 0xC1});
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorAnd(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        std::string instrName;
        switch (instr.getOpcode()) {
            case ir::Instruction::VAnd: instrName = "pand"; break;
            case ir::Instruction::VOr: instrName = "por"; break;
            case ir::Instruction::VXor: instrName = "pxor"; break;
            default: return;
        }
        *os << "  " << widthPrefix << " " << instrName << " " << rhs << ", " << lhs << "\n";
        *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);

        uint8_t opcode;
        switch (instr.getOpcode()) {
            case ir::Instruction::VAnd: opcode = 0xDB; break; // pand
            case ir::Instruction::VOr: opcode = 0xEB; break; // por
            case ir::Instruction::VXor: opcode = 0xEF; break; // pxor
            default: return;
        }
        assembler.emitBytes({0x66, 0x0F, opcode, 0xC1});
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorOr(CodeGen& cg, ir::VectorInstruction& instr) {
    emitVectorLogical(cg, instr);
}

void Windows_x64::emitVectorXor(CodeGen& cg, ir::VectorInstruction& instr) {
    emitVectorLogical(cg, instr);
}

void Windows_x64::emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        if (elemType->isFloatTy() || elemType->isDoubleTy()) {
            *os << "  " << widthPrefix << "xor " << src << ", " << dest << "\n";
        } else {
            *os << "  pxor %xmm0, %xmm0\n";
            *os << "  " << widthPrefix << "psub" << getVectorSuffix(elemType, vecType->getNumElements())
               << " " << src << ", %xmm0\n";
            *os << "  mov" << widthPrefix << " %xmm0, " << dest << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        const ir::VectorType* vecType = instr.getVectorType();
        const ir::Type* elemType = vecType->getElementType();

        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        // xor xmm0, xmm0
        assembler.emitBytes({0x66, 0x0F, 0xEF, 0xC0});
        // Load src to xmm1
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, srcOffset);

        if (elemType->isIntegerTy()) {
            uint8_t bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
            uint8_t opcode = (bitWidth == 8) ? 0xF8 : (bitWidth == 16) ? 0xF9 : (bitWidth == 32) ? 0xFA : 0xFB;
            assembler.emitBytes({0x66, 0x0F, opcode, 0xC1}); // psub xmm0, xmm1
        } else {
             // simplified FP neg
        }
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        *os << "  pcmpeqd %xmm0, %xmm0\n";
        *os << "  " << widthPrefix << "pxor " << src << ", %xmm0\n";
        *os << "  mov" << widthPrefix << " %xmm0, " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        assembler.emitBytes({0x66, 0x0F, 0x76, 0xC0}); // pcmpeqd xmm0, xmm0
        emitRegMem(assembler, 0, {0x66, 0x0F, 0xEF}, 0, srcOffset); // pxor xmm0, [src]
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset); // store
    }
}

void Windows_x64::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get vector type information
        const ir::VectorType* vecType = instr.getVectorType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());

        // Get operands
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());

        // Emit vector load
        *os << "  " << widthPrefix << "mov" << widthPrefix << " " << src << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);
        // Load pointer into rax
        emitRegMem(assembler, 0x48, 0x8B, 0, srcOffset);
        // movups xmm0, [rax] (unaligned load is safer)
        assembler.emitBytes({0x0F, 0x10, 0x00});
        // Store xmm0 to dest stack slot
        emitRegMem(assembler, 0, {0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        // Get vector type information
        const ir::VectorType* vecType = instr.getVectorType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());

        // Get operands
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string dest = cg.getValueAsOperand(instr.getOperands()[1]->get());

        // Emit vector store
        *os << "  " << widthPrefix << "mov" << widthPrefix << " " << src << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = instr.getOperands()[1]->get();
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);
        // Load pointer into rax
        emitRegMem(assembler, 0x48, 0x8B, 0, destOffset);
        // Load vector into xmm0
        emitRegMem(assembler, 0, {0x0F, 0x10}, 0, srcOffset);
        // movups [rax], xmm0
        assembler.emitBytes({0x0F, 0x11, 0x00});
    }
}

void Windows_x64::emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const ir::VectorType* vecType = instr.getVectorType();
        std::string widthPrefix = getVectorWidthPrefix(vecType->getBitWidth());
        std::string dest = cg.getValueAsOperand(&instr);
        std::string lhs = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string rhs = cg.getValueAsOperand(instr.getOperands()[1]->get());

        *os << "  " << widthPrefix << "shufps " << rhs << ", " << lhs << ", $0\n";
        *os << "  mov" << widthPrefix << " " << lhs << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, lhsOffset);
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, rhsOffset);
        assembler.emitBytes({0x0F, 0xC6, 0xC1, 0x00}); // shufps xmm0, xmm1, 0
        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitVectorExtractElement(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        *os << "  movss " << src << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);
        emitRegMem(assembler, 0, {0xF3, 0x0F, 0x10}, 0, srcOffset);
        emitRegMem(assembler, 0, {0xF3, 0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitVectorInsertElement(CodeGen& cg, ir::VectorInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string dest = cg.getValueAsOperand(&instr);
        std::string src = cg.getValueAsOperand(instr.getOperands()[0]->get());
        *os << "  movss " << src << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);
        emitRegMem(assembler, 0, {0xF3, 0x0F, 0x10}, 0, srcOffset);
        emitRegMem(assembler, 0, {0xF3, 0x0F, 0x11}, 0, destOffset);
    }
}

// === Fused Instruction Support ===
void Windows_x64::emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string dest = cg.getValueAsOperand(&instr);
        std::string a = cg.getValueAsOperand(instr.getOperands()[0]->get());
        std::string b = cg.getValueAsOperand(instr.getOperands()[1]->get());
        std::string c = cg.getValueAsOperand(instr.getOperands()[2]->get());
        *os << "  vfmadd213ps " << b << ", " << a << ", " << c << "\n";
        *os << "  vmovaps " << c << ", " << dest << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* a = instr.getOperands()[0]->get();
        ir::Value* b = instr.getOperands()[1]->get();
        ir::Value* c = instr.getOperands()[2]->get();
        ir::Value* dest = const_cast<ir::FusedInstruction*>(&instr);

        int32_t aOffset = cg.getStackOffset(a);
        int32_t bOffset = cg.getStackOffset(b);
        int32_t cOffset = cg.getStackOffset(c);
        int32_t destOffset = cg.getStackOffset(dest);

        emitRegMem(assembler, 0, {0x0F, 0x28}, 0, aOffset); // movaps xmm0, [a]
        emitRegMem(assembler, 0, {0x0F, 0x28}, 1, bOffset); // movaps xmm1, [b]
        emitRegMem(assembler, 0, {0x0F, 0x28}, 2, cOffset); // movaps xmm2, [c]

        // VEX.213.66.0F38.W0 A8 /r VFMADD213PS xmm0, xmm1, xmm2/m128
        // xmm0 = xmm1 * xmm0 + xmm2
        // We use a simplified encoding for VEX (using 0xC4 3-byte prefix)
        // For xmm0, xmm1, xmm2:
        // VEX.R=1 (bit 7 of byte 1), VEX.X=1 (bit 6), VEX.B=1 (bit 5) -> 0xE1
        // VEX.m-mmmm=00010 (bit 0-4) -> 0x02
        // VEX.W=0 (bit 7 of byte 2), VEX.vvvv=~1 (1110) (bit 3-6) -> 0x70
        // VEX.L=0 (bit 2), VEX.pp=01 (66h) (bit 0-1) -> 0x01
        // Opcode A8, ModRM: 11 000 010 (0xC2)
        assembler.emitBytes({0xC4, 0xE2, 0x71, 0xA8, 0xC2});

        emitRegMem(assembler, 0, {0x0F, 0x29}, 0, destOffset);
    }
}

void Windows_x64::emitLoadAndOperate(CodeGen& cg, ir::Instruction& load, ir::Instruction& op) {
    if (auto* os = cg.getTextStream()) {
        std::string dest = cg.getValueAsOperand(&op);
        std::string addr = cg.getValueAsOperand(load.getOperands()[0]->get());

        switch (op.getOpcode()) {
            case ir::Instruction::Add:
                *os << "  vaddss (" << addr << "), %xmm0, " << dest << "\n";
                break;
            case ir::Instruction::Mul:
                *os << "  vmulss (" << addr << "), %xmm0, " << dest << "\n";
                break;
            default:
                *os << "  vmovss (" << addr << "), %xmm0\n";
                break;
        }
    } else {
        // Simplified fallback: just emit standard load
        emitLoad(cg, load);
    }
}

void Windows_x64::emitComplexAddressing(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # Complex addressing mode fusion not yet implemented\n";
    } else {
        // Fallback to standard
    }
}

void Windows_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    resetStackOffset();
    int int_reg_idx = 0;

    for (auto& param : func.getParameters()) {
        currentStackOffset -= 8;
        cg.getStackOffsets()[param.get()] = currentStackOffset;
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

    int locals_size = -currentStackOffset;

    // Optimization: Omit frame pointer and shadow store for simple leaf functions with no locals
    bool isSimpleLeaf = !hasCalls && locals_size == 0 && func.getParameters().empty();
    if (isSimpleLeaf) {
        if (auto* os = cg.getTextStream()) {
            *os << "  # Leaf function: frame pointer and shadow store omitted\n";
        }
        return;
    }

    // Optimization: Leaf function with no locals but potentially parameters
    if (!hasCalls && locals_size == 0) {
        if (auto* os = cg.getTextStream()) {
             *os << "  # Optimized prologue for leaf function\n";
             *os << "  pushq %rbp\n";
             *os << "  movq %rsp, %rbp\n";
        } else {
            auto& assembler = cg.getAssembler();
            // pushq %rbp
            assembler.emitByte(0x55);
            // movq %rsp, %rbp
            assembler.emitByte(0x48); assembler.emitByte(0x89); assembler.emitByte(0xe5);
        }

        // Spill arguments if any
        if (auto* os = cg.getTextStream()) {
            int_reg_idx = 0;
            for (auto& param : func.getParameters()) {
                if (int_reg_idx < 4) {
                    *os << "  movq " << integerArgRegs[int_reg_idx] << ", " << cg.getStackOffset(param.get()) << "(%rbp)\n";
                    int_reg_idx++;
                }
            }
        } else {
            int_reg_idx = 0;
            for (auto& param : func.getParameters()) {
                if (int_reg_idx >= 4) break;
                int32_t offset = cg.getStackOffset(param.get());
                auto& assembler = cg.getAssembler();
                uint8_t rex = 0x48;
                uint8_t reg = 0;
                switch(int_reg_idx) {
                    case 0: reg = 1; break; // rcx
                    case 1: reg = 2; break; // rdx
                    case 2: reg = 8; break; // r8
                    case 3: reg = 9; break; // r9
                }
                emitRegMem(assembler, rex, 0x89, reg, offset);
                int_reg_idx++;
            }
        }
        return;
    }

    emitPrologue(cg, locals_size);

    if (auto* os = cg.getTextStream()) {
        int_reg_idx = 0;
        for (auto& param : func.getParameters()) {
            if (int_reg_idx < 4) {
                *os << "  movq " << integerArgRegs[int_reg_idx++] << ", " << cg.getStackOffset(param.get()) << "(%rbp)\n";
            }
        }
    } else {
        // Move arguments from registers to stack in binary mode
        auto& assembler = cg.getAssembler();
        int_reg_idx = 0;
        for (auto& param : func.getParameters()) {
            if (int_reg_idx >= 4) break; // Windows x64 ABI passes first 4 args in regs
            int32_t offset = cg.getStackOffset(param.get());
            // mov [rbp + offset], reg
            uint8_t reg = 0;
            switch(int_reg_idx) {
                case 0: reg = 1; break; // rcx
                case 1: reg = 2; break; // rdx
                case 2: reg = 8; break; // r8 (requires REX.R)
                case 3: reg = 9; break; // r9 (requires REX.R)
            }

            uint8_t rex = 0x48;
            if (reg >= 8) rex |= 0x04; // REX.R=1 for src register

            emitRegMem(assembler, rex, 0x89, reg, offset);

            int_reg_idx++;
        }
    }
}

void Windows_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) { hasCalls = true; break; }
        }
        if (hasCalls) break;
    }
    int locals_size = -currentStackOffset;
    bool isSimpleLeaf = !hasCalls && locals_size == 0 && func.getParameters().empty();

    if (isSimpleLeaf) {
        if (auto* os = cg.getTextStream()) {
            *os << "  ret\n";
        } else {
            cg.getAssembler().emitByte(0xc3);
        }
        return;
    }

    if (!hasCalls && locals_size == 0) {
        if (auto* os = cg.getTextStream()) {
            *os << "  popq %rbp\n";
            *os << "  ret\n";
        } else {
            auto& assembler = cg.getAssembler();
            // popq %rbp
            assembler.emitByte(0x5d);
            // ret
            assembler.emitByte(0xc3);
        }
        return;
    }

    emitEpilogue(cg);
}

// Helper functions for vector operations
std::string Windows_x64::getVectorSuffix(const ir::Type* elemType, unsigned elemCount) const {
    if (elemType->isIntegerTy()) {
        unsigned bitWidth = static_cast<const ir::IntegerType*>(elemType)->getBitwidth();
        if (bitWidth == 8) return "b";
        if (bitWidth == 16) return "w";
        if (bitWidth == 32) return "d";
        if (bitWidth == 64) return "q";
    } else if (elemType->isFloatTy()) {
        return "s";  // Single precision
    } else if (elemType->isDoubleTy()) {
        return "d";  // Double precision
    }
    return "d";  // Default
}

std::string Windows_x64::getVectorWidthPrefix(unsigned bitWidth) const {
    if (bitWidth <= 128) return "x";
    if (bitWidth <= 256) return "y";
    return "z";
}

size_t Windows_x64::getMaxRegistersForArgs() const { return 4; }
void Windows_x64::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void Windows_x64::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}
void Windows_x64::emitNot(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();

    if (auto* os = cg.getTextStream()) {
        std::string destOperand = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        *os << "  mov rax, " << opOperand << "\n";
        *os << "  not rax\n";
        *os << "  mov " << destOperand << ", rax\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t opOffset = cg.getStackOffset(op);
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + opOffset]
        emitRegMem(assembler, 0x48, 0x8B, 0, opOffset);
        // not rax
        assembler.emitBytes({0x48, 0xF7, 0xD0});
        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}
void Windows_x64::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
        std::string addInstr = typeInfo.size == 32 ? "addss" : "addsd";
        *os << "  " << movInstr << " " << lhsOp << ", %xmm0\n";
        *os << "  " << addInstr << " " << rhsOp << ", %xmm0\n";
        *os << "  " << movInstr << " %xmm0, " << destOp << "\n";
    } else {
        // Binary emission logic is the same as SystemV_x64 for this instruction
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        uint8_t prefix = (typeInfo.size == 32) ? 0xF3 : 0xF2;
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x58}, 0, rhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
        std::string subInstr = typeInfo.size == 32 ? "subss" : "subsd";
        *os << "  " << movInstr << " " << lhsOp << ", %xmm0\n";
        *os << "  " << subInstr << " " << rhsOp << ", %xmm0\n";
        *os << "  " << movInstr << " %xmm0, " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        uint8_t prefix = (typeInfo.size == 32) ? 0xF3 : 0xF2;
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x5C}, 0, rhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitFMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
        std::string mulInstr = typeInfo.size == 32 ? "mulss" : "mulsd";
        *os << "  " << movInstr << " " << lhsOp << ", %xmm0\n";
        *os << "  " << mulInstr << " " << rhsOp << ", %xmm0\n";
        *os << "  " << movInstr << " %xmm0, " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        uint8_t prefix = (typeInfo.size == 32) ? 0xF3 : 0xF2;
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x59}, 0, rhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
        std::string divInstr = typeInfo.size == 32 ? "divss" : "divsd";
        *os << "  " << movInstr << " " << lhsOp << ", %xmm0\n";
        *os << "  " << divInstr << " " << rhsOp << ", %xmm0\n";
        *os << "  " << movInstr << " %xmm0, " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        TypeInfo typeInfo = getTypeInfo(type);
        uint8_t prefix = (typeInfo.size == 32) ? 0xF3 : 0xF2;
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x5E}, 0, rhsOffset);
        emitRegMem(assembler, 0, {prefix, 0x0F, 0x11}, 0, destOffset);
    }
}

void Windows_x64::emitCmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string destOp = cg.getValueAsOperand(&instr);

        std::string set_instr;
        switch(instr.getOpcode()) {
            case ir::Instruction::Ceq:  set_instr = "sete"; break;
            case ir::Instruction::Cne:  set_instr = "setne"; break;
            case ir::Instruction::Cslt: set_instr = "setl"; break;
            case ir::Instruction::Csle: set_instr = "setle"; break;
            case ir::Instruction::Csgt: set_instr = "setg"; break;
            case ir::Instruction::Csge: set_instr = "setge"; break;
            case ir::Instruction::Cule: set_instr = "setbe"; break;
            case ir::Instruction::Cult: set_instr = "setb"; break;
            case ir::Instruction::Cuge: set_instr = "setae"; break;
            case ir::Instruction::Cugt: set_instr = "seta"; break;
            case ir::Instruction::Ceqf: set_instr = "sete"; break;
            case ir::Instruction::Cnef: set_instr = "setne"; break;
            case ir::Instruction::Clt: set_instr = "setb"; break;
            case ir::Instruction::Cle: set_instr = "setbe"; break;
            case ir::Instruction::Cgt: set_instr = "seta"; break;
            case ir::Instruction::Cge: set_instr = "setae"; break;
            case ir::Instruction::Co: set_instr = "setp"; break;
            case ir::Instruction::Cuo: set_instr = "setnp"; break;
            default: *os << "# Unhandled comparison\n"; return;
        }

        const ir::Type* type = lhs->getType();
        if (type->isFloatTy() || type->isDoubleTy()) {
             *os << "  ucomiss " << rhsOp << ", " << lhsOp << "\n";
        } else {
             *os << "  cmpq " << rhsOp << ", " << lhsOp << "\n";
        }

        *os << "  " << set_instr << " %al\n";
        *os << "  movzbl %al, %eax\n";
        *os << "  movl %eax, " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        ir::Value* dest = &instr;
        int32_t lhsOffset = cg.getStackOffset(lhs);
        int32_t rhsOffset = cg.getStackOffset(rhs);
        int32_t destOffset = cg.getStackOffset(dest);

        uint8_t set_byte = 0;
        bool is_float = lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy();
        switch (instr.getOpcode()) {
            case ir::Instruction::Ceq:
            case ir::Instruction::Ceqf: set_byte = 0x94; break; // sete
            case ir::Instruction::Cne:
            case ir::Instruction::Cnef: set_byte = 0x95; break; // setne
            case ir::Instruction::Cslt: set_byte = 0x9c; break; // setl
            case ir::Instruction::Csle: set_byte = 0x9e; break; // setle
            case ir::Instruction::Csgt: set_byte = 0x9f; break; // setg
            case ir::Instruction::Csge: set_byte = 0x9d; break; // setge
            case ir::Instruction::Cult:
            case ir::Instruction::Clt:  set_byte = 0x92; break; // setb
            case ir::Instruction::Cule:
            case ir::Instruction::Cle:  set_byte = 0x96; break; // setbe
            case ir::Instruction::Cugt:
            case ir::Instruction::Cgt:  set_byte = 0x97; break; // seta
            case ir::Instruction::Cuge:
            case ir::Instruction::Cge:  set_byte = 0x93; break; // setae
            case ir::Instruction::Co:   if(is_float) set_byte = 0x9A; break; // setp
            case ir::Instruction::Cuo:  if(is_float) set_byte = 0x9B; break; // setnp
            default: return;
        }

        if (is_float) {
            uint8_t prefix = (lhs->getType()->isFloatTy()) ? 0xF3 : 0xF2;
            // movss [rbp+lhsOffset], %xmm0
            emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
            // ucomiss [rbp+rhsOffset], %xmm0
            emitRegMem(assembler, 0, {0x0F, 0x2E}, 0, rhsOffset);
        } else {
            // movq [rbp+lhsOffset], %rax
            emitRegMem(assembler, 0x48, 0x8B, 0, lhsOffset);
            // cmpq [rbp+rhsOffset], %rax
            emitRegMem(assembler, 0x48, 0x3B, 0, rhsOffset);
        }

        // setcc %al
        assembler.emitBytes({0x0F, set_byte, 0xC0});
        // movzbl %al, %eax
        assembler.emitBytes({0x0F, 0xB6, 0xC0});
        // movl %eax, [rbp+destOffset]
        emitRegMem(assembler, 0, 0x89, 0, destOffset);
    }
}
void Windows_x64::emitCast(CodeGen& cg, ir::Instruction& instr, const ir::Type* fromType, const ir::Type* toType) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = instr.getOperands()[0]->get();
        std::string srcOp = cg.getValueAsOperand(src);
        std::string destOp = cg.getValueAsOperand(&instr);
        if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            if (fromType->getSize() < toType->getSize()) {
                *os << "  movsx rax, " << getLoadStoreSuffix(fromType) << " ptr " << srcOp << "\n";
                *os << "  mov " << destOp << ", rax\n";
            } else {
                *os << "  mov rax, " << srcOp << "\n";
                *os << "  mov " << destOp << ", rax\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t srcOffset = cg.getStackOffset(src);
        int32_t destOffset = cg.getStackOffset(dest);

        if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            // mov rax, [rbp + srcOffset]
            emitRegMem(assembler, 0x48, 0x8B, 0, srcOffset);
            if (fromType->getSize() < toType->getSize()) {
                if (fromType->getSize() == 4) {
                    // movsxd rax, eax
                    assembler.emitBytes({0x48, 0x63, 0xC0});
                }
                // (Others handled by default 64-bit load if we are simplified)
            }
            // mov [rbp + destOffset], rax
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
        }
    }
}

void Windows_x64::emitVAStart(CodeGen& cg, ir::Instruction& instr) {}
void Windows_x64::emitVAArg(CodeGen& cg, ir::Instruction& instr) {}

uint64_t Windows_x64::getSyscallNumber(ir::SyscallId id) const {
    // Note: Windows syscall numbers change between build versions.
    // These are from Windows 10/11 x64 and are not guaranteed to work everywhere.
    // Usually, you'd call through ntdll.dll or Win32 API.
    switch (id) {
        case ir::SyscallId::Exit: return 0x002C; // NtTerminateProcess
        case ir::SyscallId::Read: return 0x0006; // NtReadFile
        case ir::SyscallId::Write: return 0x0008; // NtWriteFile
        case ir::SyscallId::Close: return 0x000F; // NtClose
        default: return 0;
    }
}

void Windows_x64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    // Windows does not encourage direct syscalls. Usually, one calls the Win32 API.
    // However, if we must map 'syscall' in Fyra, we'll use the Windows x64 syscall ABI:
    // rax: syscall number, arguments in: r10, rdx, r8, r9
    if (auto* os = cg.getTextStream()) {
        *os << "  # Windows x64 Syscall (Direct syscalls are unstable on Windows)\n";
        if (sid != ir::SyscallId::None) {
            *os << "  mov rax, " << getSyscallNumber(sid) << "\n";
        } else {
            *os << "  mov rax, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i + 1 : i;
            std::string reg;
            switch(argIdx) {
                case 1: reg = "r10"; break;
                case 2: reg = "rdx"; break;
                case 3: reg = "r8"; break;
                case 4: reg = "r9"; break;
                default: continue;
            }
            if (!reg.empty()) {
                *os << "  mov " << reg << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
            }
        }
        *os << "  syscall\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  mov " << cg.getValueAsOperand(&instr) << ", rax\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // Load syscall number into rax
        if (sid != ir::SyscallId::None) {
            uint64_t num = getSyscallNumber(sid);
            assembler.emitByte(0x48); assembler.emitByte(0xC7); assembler.emitByte(0xC0);
            assembler.emitDWord(static_cast<uint32_t>(num));
        } else {
            emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 0);
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i + 1 : i;
            uint8_t reg;
            switch(argIdx) {
                case 1: reg = 10; break; // r10
                case 2: reg = 2; break; // rdx
                case 3: reg = 8; break; // r8
                case 4: reg = 9; break; // r9
                default: continue;
            }
            emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), reg);
        }
        assembler.emitBytes({0x0F, 0x05}); // syscall
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset); // mov rax, [rbp+offset]
        }
    }
}

void Windows_x64::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* ptr = instr.getOperands()[0]->get();
    const ir::Type* type = instr.getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string ptrOp = cg.getValueAsOperand(ptr);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        *os << "  mov rax, " << ptrOp << "\n";
        *os << "  mov rax, " << sizeSuffix << " ptr [rax]\n";
        *os << "  mov " << destOp << ", rax\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t ptrOffset = cg.getStackOffset(ptr);
        int32_t destOffset = cg.getStackOffset(dest);

        // mov rax, [rbp + ptrOffset]
        emitRegMem(assembler, 0x48, 0x8B, 0, ptrOffset);

        // mov rax, [rax] (with size)
        if (type->getSize() == 1) {
            assembler.emitBytes({0x0F, 0xB6, 0x00}); // movzx rax, byte ptr [rax]
        } else if (type->getSize() == 2) {
            assembler.emitBytes({0x0F, 0xB7, 0x00}); // movzx rax, word ptr [rax]
        } else if (type->getSize() == 4) {
            assembler.emitBytes({0x8B, 0x00}); // mov eax, dword ptr [rax]
        } else {
            assembler.emitBytes({0x48, 0x8B, 0x00}); // mov rax, qword ptr [rax]
        }

        // mov [rbp + destOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
    }
}

void Windows_x64::emitStore(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* val = instr.getOperands()[0]->get();
    ir::Value* ptr = instr.getOperands()[1]->get();
    const ir::Type* type = val->getType();

    if (auto* os = cg.getTextStream()) {
        std::string valOp = cg.getValueAsOperand(val);
        std::string ptrOp = cg.getValueAsOperand(ptr);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        *os << "  mov rax, " << valOp << "\n";
        *os << "  mov rdx, " << ptrOp << "\n";
        *os << "  mov " << sizeSuffix << " ptr [rdx], rax\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t valOffset = cg.getStackOffset(val);
        int32_t ptrOffset = cg.getStackOffset(ptr);

        // mov rax, [rbp + valOffset]
        emitRegMem(assembler, 0x48, 0x8B, 0, valOffset);
        // mov rdx, [rbp + ptrOffset]
        emitRegMem(assembler, 0x48, 0x8B, 2, ptrOffset);

        // mov [rdx], rax (with size)
        if (type->getSize() == 1) {
            assembler.emitBytes({0x88, 0x02}); // mov byte ptr [rdx], al
        } else if (type->getSize() == 2) {
            assembler.emitBytes({0x66, 0x89, 0x02}); // mov word ptr [rdx], ax
        } else if (type->getSize() == 4) {
            assembler.emitBytes({0x89, 0x02}); // mov dword ptr [rdx], eax
        } else {
            assembler.emitBytes({0x48, 0x89, 0x02}); // mov qword ptr [rdx], rax
        }
    }
}

void Windows_x64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::ConstantInt* sizeConst = dynamic_cast<ir::ConstantInt*>(instr.getOperands()[0]->get());
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  mov rax, [rip + __heap_ptr]\n";
        *os << "  mov [rbp + " << pointerOffset << "], rax\n";
        *os << "  add rax, " << alignedSize << "\n";
        *os << "  mov [rip + __heap_ptr], rax\n";
    } else {
        auto& assembler = cg.getAssembler();
        // Load __heap_ptr into %rax
        assembler.emitBytes({0x48, 0x8B, 0x05});
        uint64_t reloc_offset_load = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc_load;
        reloc_load.offset = reloc_offset_load;
        reloc_load.symbolName = "__heap_ptr";
        reloc_load.type = "R_X86_64_PC32";
        reloc_load.sectionName = ".text";
        reloc_load.addend = -4;
        cg.addRelocation(reloc_load);

        // mov [rbp + pointerOffset], rax
        emitRegMem(assembler, 0x48, 0x89, 0, pointerOffset);

        // add rax, alignedSize
        if (alignedSize <= 127) {
            assembler.emitBytes({0x48, 0x83, 0xC0, static_cast<uint8_t>(alignedSize)});
        } else {
            assembler.emitBytes({0x48, 0x05});
            assembler.emitDWord(static_cast<uint32_t>(alignedSize));
        }

        // Store %rax back to __heap_ptr
        assembler.emitBytes({0x48, 0x89, 0x05});
        uint64_t reloc_offset_store = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc_store;
        reloc_store.offset = reloc_offset_store;
        reloc_store.symbolName = "__heap_ptr";
        reloc_store.type = "R_X86_64_PC32";
        reloc_store.sectionName = ".text";
        reloc_store.addend = -4;
        cg.addRelocation(reloc_store);
    }
}

void Windows_x64::emitBr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  cmp qword ptr " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", 0\n";
        *os << "  jne " << instr.getOperands()[1]->get()->getName() << "\n";
        if (instr.getOperands().size() > 2) {
            *os << "  jmp " << instr.getOperands()[2]->get()->getName() << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* cond = instr.getOperands()[0]->get();
        int32_t condOffset = cg.getStackOffset(cond);

        // cmp qword ptr [rbp + condOffset], 0
        emitRegMem(assembler, 0x48, 0x83, 7, condOffset); // /7 is CMP imm8
        assembler.emitByte(0x00);

        // jne rel32
        assembler.emitBytes({0x0F, 0x85});
        uint64_t reloc_offset_true = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc_true;
        reloc_true.offset = reloc_offset_true;
        ir::Value* trueTarget = instr.getOperands()[1]->get();
        if (auto* bb = dynamic_cast<ir::BasicBlock*>(trueTarget)) {
            reloc_true.symbolName = bb->getParent()->getName() + "_" + bb->getName();
        } else {
            reloc_true.symbolName = trueTarget->getName();
        }
        reloc_true.type = "R_X86_64_PC32";
        reloc_true.sectionName = ".text";
        reloc_true.addend = -4;
        cg.addRelocation(reloc_true);

        if (instr.getOperands().size() > 2) {
            // jmp rel32
            assembler.emitByte(0xE9);
            uint64_t reloc_offset_false = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc_false;
            reloc_false.offset = reloc_offset_false;
            ir::Value* falseTarget = instr.getOperands()[2]->get();
            if (auto* bb = dynamic_cast<ir::BasicBlock*>(falseTarget)) {
                reloc_false.symbolName = bb->getParent()->getName() + "_" + bb->getName();
            } else {
                reloc_false.symbolName = falseTarget->getName();
            }
            reloc_false.type = "R_X86_64_PC32";
            reloc_false.sectionName = ".text";
            reloc_false.addend = -4;
            cg.addRelocation(reloc_false);
        }
    }
}

void Windows_x64::emitJmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  jmp " << instr.getOperands()[0]->get()->getName() << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        assembler.emitByte(0xE9);
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        ir::Value* target = instr.getOperands()[0]->get();
        if (auto* bb = dynamic_cast<ir::BasicBlock*>(target)) {
            reloc.symbolName = bb->getParent()->getName() + "_" + bb->getName();
        } else {
            reloc.symbolName = target->getName();
        }
        reloc.type = "R_X86_64_PC32";
        reloc.sectionName = ".text";
        reloc.addend = -4;
        cg.addRelocation(reloc);
    }
}
void Windows_x64::emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) {}
void Windows_x64::emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) {}

} // namespace target
} // namespace codegen