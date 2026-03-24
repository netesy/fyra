#include "codegen/target/SystemV_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Function.h"
#include <ostream>
#include <algorithm>
#include <iostream>
#include <string>

using namespace ir;
using namespace codegen;
using namespace codegen::target;

namespace {
    std::string getEpilogueLabel(const ir::Function* func) {
        return func ? func->getName() + "_epilogue" : ".L_epilogue";
    }

    void emitRegMem(execgen::Assembler& assembler, uint8_t rex, const std::vector<uint8_t>& opcodes, uint8_t reg, int32_t offset) {
        if (rex) assembler.emitByte(rex);
        for (uint8_t op : opcodes) assembler.emitByte(op);
        if (offset >= -128 && offset <= 127) {
            // Mod=01 ([rbp + disp8]), RM=101 (rbp)
            assembler.emitByte(0x45 | ((reg & 7) << 3));
            assembler.emitByte(static_cast<int8_t>(offset));
        } else {
            // Mod=10 ([rbp + disp32]), RM=101 (rbp)
            assembler.emitByte(0x85 | ((reg & 7) << 3));
            assembler.emitDWord(static_cast<uint32_t>(offset));
        }
    }

    void emitRegMem(execgen::Assembler& assembler, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
        emitRegMem(assembler, rex, std::vector<uint8_t>{opcode}, reg, offset);
    }

    uint8_t getRex(const ir::Type* type) {
        if (type->isPointerTy()) return 0x48;
        if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
            if (intTy->getBitwidth() == 64) return 0x48;
        }
        return 0;
    }

    uint8_t getOpcode(uint8_t baseOpcode, const ir::Type* type) {
        if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
            if (intTy->getBitwidth() == 8) {
                if (baseOpcode == 0x8B) return 0x8A;
                if (baseOpcode == 0x89) return 0x88;
                if (baseOpcode == 0x83) return 0x80;
                if (baseOpcode == 0x03) return 0x02;
                if (baseOpcode == 0x2B) return 0x2A;
                return baseOpcode - 1;
            }
        }
        return baseOpcode;
    }

    std::string getLoadStoreSuffix(const ir::Type* type) {
        if (type->isPointerTy()) return "q";
        if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
            switch (intTy->getBitwidth()) {
                case 8: return "b";
                case 16: return "w";
                case 32: return "l";
                case 64: return "q";
                default: return "q";
            }
        }
        if (type->isFloatTy()) return "ss";
        if (type->isDoubleTy()) return "sd";
        return "q";
    }

    std::string getMoveInstruction(const ir::Type* type) {
        if (type->isFloatTy()) return "movss";
        if (type->isDoubleTy()) return "movsd";
        return "mov" + getLoadStoreSuffix(type);
    }

    void emitLoadValue(CodeGen& cg, execgen::Assembler& assembler, ir::Value* val, uint8_t reg) {
        uint8_t rex = getRex(val->getType());
        
        // Handle physical registers if already assigned
        if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
            if (instr->hasPhysicalRegister()) {
                uint8_t phys = static_cast<uint8_t>(instr->getPhysicalRegister());
                // Mapping from allocator index to x64 register index
                // allocator regs: rax, rcx, rdx, rsi, rdi, r8, r9, r10, rbx, r12, r13, r14, r15
                static const uint8_t x64_map[] = {0, 1, 2, 6, 7, 8, 9, 10, 3, 12, 13, 14, 15};
                if (phys >= sizeof(x64_map)) return; // Should not happen
                uint8_t src_reg = x64_map[phys];

                if (src_reg == reg) return; // Already in target register
                
                // mov reg, src_reg
                uint8_t local_rex = getRex(val->getType());
                if (reg >= 8) local_rex |= 0x44; // REX.R for destination 'reg'
                if (src_reg >= 8) local_rex |= 0x41; // REX.B for source 'src_reg'
                if (local_rex) assembler.emitByte(local_rex);
                assembler.emitByte(0x8B); // mov r, rm
                assembler.emitByte(0xC0 | ((reg & 7) << 3) | (src_reg & 7));
                return;
            }
        }

        if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
            if (reg >= 8) rex |= 0x41; // REX.B=1
            if (rex) assembler.emitByte(rex);
            assembler.emitByte(0xB8 + (reg & 7));
            if (rex & 0x08) assembler.emitQWord(constInt->getValue());
            else assembler.emitDWord(constInt->getValue());
        } else if (auto* global = dynamic_cast<ir::GlobalValue*>(val)) {
            uint8_t rex_g = 0x48;
            if (reg >= 8) rex_g |= 0x04; // REX.R=1
            assembler.emitByte(rex_g);
            assembler.emitByte(0x8D); // LEA
            assembler.emitByte(0x05 | ((reg & 7) << 3));
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = global->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);
        } else if (auto* gv = dynamic_cast<ir::GlobalVariable*>(val)) {
            // All string literals/global pointers in Fyra backend currently use LEA to get address
            uint8_t rex_g = 0x48;
            if (reg >= 8) rex_g |= 0x04; // REX.R=1
            assembler.emitByte(rex_g);
            assembler.emitByte(0x8D); // LEA
            assembler.emitByte(0x05 | ((reg & 7) << 3));
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = gv->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);
        } else {
            int32_t offset = cg.getStackOffset(val);
            if (reg >= 8) rex |= 0x44; // REX.R=1
            emitRegMem(assembler, rex, getOpcode(0x8B, val->getType()), reg & 7, offset);
        }
    }
}

namespace codegen {
namespace target {

void SystemV_x64::initRegisters() {
    // Integer registers - reordered to put caller-saved registers first
    // This allows simple functions to avoid saving callee-saved registers.
    // %r11 is reserved as a scratch register for global loads and PHIs.
    integerRegs = {"%rax", "%rcx", "%rdx", "%rsi", "%rdi", "%r8", "%r9", "%r10",
                  "%rbx", "%r12", "%r13", "%r14", "%r15"};
    
    // Floating-point registers
    for (int i = 0; i < 16; ++i) {
        floatRegs.push_back("%xmm" + std::to_string(i));
    }
    
    // Vector registers (same as float for now)
    vectorRegs = floatRegs;
    
    // Caller-saved registers
    callerSaved = {
        {"%rax", true}, {"%rcx", true}, {"%rdx", true}, 
        {"%rsi", true}, {"%rdi", true}, {"%r8", true}, 
        {"%r9", true}, {"%r10", true}, {"%r11", true}
    };
    
    // Callee-saved registers
    calleeSaved = {
        {"%rbx", true}, {"%r12", true}, {"%r13", true}, 
        {"%r14", true}, {"%r15", true}, {"%rbp", true}
    };
    
    // Return registers
    intReturnReg = "%rax";
    floatReturnReg = "%xmm0";
    
    // Stack and frame pointers
    stackPtrReg = "rsp";
    framePtrReg = "rbp";
    
    // Initialize argument registers for System V ABI
    integerArgRegs = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    floatArgRegs = {"%xmm0", "%xmm1", "%xmm2", "%xmm3", 
                   "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
}

SystemV_x64::SystemV_x64() : X86_64Base() {
    initRegisters();
}

const std::string& SystemV_x64::getReturnRegister(const ir::Type* type) const {
    if (type->isFloatTy() || type->isDoubleTy()) {
        return floatReturnReg;
    }
    return intReturnReg;
}

void SystemV_x64::emitPrologue(CodeGen& cg, int stack_size) {
    if (auto* os = cg.getTextStream()) {
        *os << "  pushq %" << framePtrReg << "\n";
        *os << "  movq %" << stackPtrReg << ", %" << framePtrReg << "\n";
        if (stack_size > 0) {
            *os << "  subq $" << stack_size << ", %" << stackPtrReg << "\n";
        }

        // Always save callee-saved registers to ensure consistent stack frames
        *os << "  pushq %rbx\n";
        *os << "  pushq %r12\n";
        *os << "  pushq %r13\n";
        *os << "  pushq %r14\n";
        *os << "  pushq %r15\n";
    } else {
        auto& assembler = cg.getAssembler();
        // pushq %rbp
        assembler.emitByte(0x55);
        // movq %rsp, %rbp
        assembler.emitByte(0x48);
        assembler.emitByte(0x89);
        assembler.emitByte(0xe5);
        // subq $size, %rsp
        if (stack_size > 0) {
            if (stack_size <= 127) {
                assembler.emitByte(0x48);
                assembler.emitByte(0x83);
                assembler.emitByte(0xec);
                assembler.emitByte(static_cast<uint8_t>(stack_size));
            } else {
                assembler.emitByte(0x48);
                assembler.emitByte(0x81);
                assembler.emitByte(0xec);
                assembler.emitDWord(static_cast<uint32_t>(stack_size));
            }
        }
        // push rbx, r12-r15
        assembler.emitByte(0x53); // push rbx
        assembler.emitBytes({0x41, 0x54}); // push r12
        assembler.emitBytes({0x41, 0x55}); // push r13
        assembler.emitBytes({0x41, 0x56}); // push r14
        assembler.emitBytes({0x41, 0x57}); // push r15
    }
}

void SystemV_x64::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        // Always restore callee-saved registers
        *os << "  popq %r15\n";
        *os << "  popq %r14\n";
        *os << "  popq %r13\n";
        *os << "  popq %r12\n";
        *os << "  popq %rbx\n";

        *os << "  movq %" << framePtrReg << ", %" << stackPtrReg << "\n";
        *os << "  popq %" << framePtrReg << "\n";
        *os << "  ret\n";
    } else {
        auto& assembler = cg.getAssembler();

        // pop r15-r12, rbx
        assembler.emitBytes({0x41, 0x5F}); // pop r15
        assembler.emitBytes({0x41, 0x5E}); // pop r14
        assembler.emitBytes({0x41, 0x5D}); // pop r13
        assembler.emitBytes({0x41, 0x5C}); // pop r12
        assembler.emitByte(0x5B); // pop rbx

        // movq %rbp, %rsp
        assembler.emitByte(0x48);
        assembler.emitByte(0x89);
        assembler.emitByte(0xec);

        // popq %rbp
        assembler.emitByte(0x5d);

        // ret
        assembler.emitByte(0xc3);
        // Ensure 16-byte alignment after ret
        while (assembler.getCodeSize() % 16 != 0) assembler.emitByte(0x90); // nop
    }
}

void SystemV_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    // Add function symbol for binary emission
    if (!cg.getTextStream()) {
        CodeGen::SymbolInfo func_sym;
        func_sym.name = func.getName();
        func_sym.sectionName = ".text";
        func_sym.value = cg.getAssembler().getCodeSize();
        func_sym.type = 2; // STT_FUNC
        func_sym.binding = 1; // STB_GLOBAL
        cg.addSymbol(func_sym);
    }

    resetStackOffset();
    int int_reg_idx = 0;

    for (auto& param : func.getParameters()) {
        if (int_reg_idx < 6) {
            currentStackOffset -= 8;
            cg.getStackOffsets()[param.get()] = currentStackOffset;
        } else {
            // Arguments 7+ are on the stack from the caller: [rbp + 16], [rbp + 24], ...
            cg.getStackOffsets()[param.get()] = 16 + (int_reg_idx - 6) * 8;
        }
        int_reg_idx++;
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

    // Stack alignment logic for System V x64:
    // Entry RSP = 16N + 8
    // 1. push rbp (8) -> RSP = 16N
    // 2. sub locals_size -> RSP = 16N - locals_size
    // 3. push 5 callee-saved registers (40) -> RSP = 16N - locals_size - 40
    // We want (locals_size + 40) % 16 == 0.
    // 40 % 16 = 8. So we want locals_size % 16 == 8.
    int additional_stack = locals_size;
    if ((additional_stack + 40) % 16 != 0) {
        additional_stack += 16 - ((additional_stack + 40) % 16);
    }

    emitPrologue(cg, additional_stack);

    if (auto* os = cg.getTextStream()) {
        int_reg_idx = 0;
        int float_reg_idx = 0;
        for (auto& param : func.getParameters()) {
            const ir::Type* paramType = param->getType();
            if (paramType->isFloatingPoint()) {
                if (float_reg_idx < static_cast<int>(floatArgRegs.size())) {
                    std::string instr = paramType->isFloatTy() ? "movss" : "movsd";
                    *os << "  " << instr << " " << floatArgRegs[float_reg_idx++] << ", "
                        << cg.getStackOffset(param.get()) << "(%rbp)\n";
                }
            } else if (int_reg_idx < 6) {
                std::string suffix = getLoadStoreSuffix(paramType);
                std::string srcReg = getRegisterName(integerArgRegs[int_reg_idx++], paramType);
                *os << "  mov" << suffix << " " << srcReg << ", " << cg.getStackOffset(param.get()) << "(%rbp)\n";
            }
        }
    } else {
        // Move arguments from registers to stack
        int_reg_idx = 0;
        for (auto& param : func.getParameters()) {
            if (int_reg_idx >= 6) break;
            int32_t offset = cg.getStackOffset(param.get());
            // movq %reg, offset(%rbp)
            auto& assembler = cg.getAssembler();
            uint8_t rex = 0x48;
            uint8_t reg = 0;
            switch(int_reg_idx) {
                case 0: reg = 7; break; // rdi
                case 1: reg = 6; break; // rsi
                case 2: reg = 2; break; // rdx
                case 3: reg = 1; break; // rcx
                case 4: rex = 0x4C; reg = 0; break; // r8
                case 5: rex = 0x4C; reg = 1; break; // r9
            }
            emitRegMem(assembler, rex, 0x89, reg, offset);
            int_reg_idx++;
        }
    }
}

void SystemV_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getEpilogueLabel(&func) << ":\n";
    }
    emitEpilogue(cg);
}

void SystemV_x64::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  add" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0x03, type));
        assembler.emitByte(0xC1); // add %rcx, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
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
        auto& assembler = cg.getAssembler();
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

void SystemV_x64::emitSub(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  sub" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0x2B, type));
        assembler.emitByte(0xC1); // sub %rcx, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
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

void SystemV_x64::emitMul(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();
    std::string sizeSuffix = getLoadStoreSuffix(type);

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        if (sizeSuffix == "b") {
            *os << "  movb " << lhsOp << ", %al\n";
            *os << "  imulb " << rhsOp << "\n";
            *os << "  movb %al, " << destOp << "\n";
        } else {
            std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
            *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
            *os << "  imul" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
            *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        if (sizeSuffix == "b") {
            emitLoadValue(cg, assembler, lhs, 0); // al
            emitLoadValue(cg, assembler, rhs, 1); // cl
            assembler.emitBytes({0xF6, 0xE9}); // imul %cl
            emitRegMem(assembler, 0, 0x88, 0, destOffset);
        } else {
            emitLoadValue(cg, assembler, lhs, 0);
            emitLoadValue(cg, assembler, rhs, 1);
            if (type->isIntegerTy() && static_cast<const ir::IntegerType*>(type)->getBitwidth() == 16) assembler.emitByte(0x66);
            if (rex) assembler.emitByte(rex);
            assembler.emitBytes({0x0F, 0xAF, 0xC1}); // imul %rcx, %rax
            emitRegMem(assembler, rex, 0x89, 0, destOffset);
        }
    }
}

void SystemV_x64::emitFMul(CodeGen& cg, ir::Instruction& instr) {
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

void SystemV_x64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        std::string sizeSuffix = getLoadStoreSuffix(type);
        bool isSigned = (instr.getOpcode() == ir::Instruction::Div);
        if (isSigned) {
            if (sizeSuffix == "b") {
                *os << "  movb " << lhsOp << ", %al\n"; *os << "  cbw\n"; *os << "  idivb " << rhsOp << "\n"; *os << "  movb %al, " << destOp << "\n";
            } else if (sizeSuffix == "w") {
                *os << "  movw " << lhsOp << ", %ax\n"; *os << "  cwd\n"; *os << "  idivw " << rhsOp << "\n"; *os << "  movw %ax, " << destOp << "\n";
            } else if (sizeSuffix == "l") {
                *os << "  movl " << lhsOp << ", %eax\n"; *os << "  cltd\n"; *os << "  idivl " << rhsOp << "\n"; *os << "  movl %eax, " << destOp << "\n";
            } else {
                *os << "  movq " << lhsOp << ", %rax\n"; *os << "  cqto\n"; *os << "  idivq " << rhsOp << "\n"; *os << "  movq %rax, " << destOp << "\n";
            }
        } else {
            if (sizeSuffix == "b") {
                *os << "  movb " << lhsOp << ", %al\n"; *os << "  movb $0, %ah\n"; *os << "  divb " << rhsOp << "\n"; *os << "  movb %al, " << destOp << "\n";
            } else if (sizeSuffix == "w") {
                *os << "  movw " << lhsOp << ", %ax\n"; *os << "  movw $0, %dx\n"; *os << "  divw " << rhsOp << "\n"; *os << "  movw %ax, " << destOp << "\n";
            } else if (sizeSuffix == "l") {
                *os << "  movl " << lhsOp << ", %eax\n"; *os << "  movl $0, %edx\n"; *os << "  divl " << rhsOp << "\n"; *os << "  movl %eax, " << destOp << "\n";
            } else {
                *os << "  movq " << lhsOp << ", %rax\n"; *os << "  movq $0, %rdx\n"; *os << "  divq " << rhsOp << "\n"; *os << "  movq %rax, " << destOp << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        uint8_t rex = getRex(type);
        std::string sizeSuffix = getLoadStoreSuffix(type);

        emitLoadValue(cg, assembler, lhs, 0); // Load to %rax

        if (instr.getOpcode() == ir::Instruction::Div) {
            if (sizeSuffix == "b") assembler.emitByte(0x98); // cbw
            else if (sizeSuffix == "w") { assembler.emitByte(0x66); assembler.emitByte(0x99); } // cwd
            else if (sizeSuffix == "l") assembler.emitByte(0x99); // cdq
            else { assembler.emitByte(0x48); assembler.emitByte(0x99); } // cqo
        } else {
            if (sizeSuffix == "b") { assembler.emitByte(0xB4); assembler.emitByte(0x00); } // mov $0, %ah
            else if (sizeSuffix == "w") { assembler.emitBytes({0x66, 0x31, 0xD2}); } // xor %dx, %dx
            else if (sizeSuffix == "l") { assembler.emitBytes({0x31, 0xD2}); } // xor %edx, %edx
            else { assembler.emitBytes({0x48, 0x31, 0xD2}); } // xor %rdx, %rdx
        }

        uint8_t div_opcode = (instr.getOpcode() == ir::Instruction::Div) ? 7 : 6;
        emitLoadValue(cg, assembler, rhs, 1); // Load to %rcx
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xF7, type));
        assembler.emitByte(0xC0 | (div_opcode << 3) | 1); // /div_opcode %rcx
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        const ir::Type* type = lhs->getType();
        std::string sizeSuffix = getLoadStoreSuffix(type);
        bool isSigned = (instr.getOpcode() == ir::Instruction::Rem);

        if (isSigned) {
            if (sizeSuffix == "b") {
                *os << "  movb " << lhsOp << ", %al\n"; *os << "  cbw\n"; *os << "  idivb " << rhsOp << "\n"; *os << "  movb %ah, " << destOp << "\n";
            } else if (sizeSuffix == "w") {
                *os << "  movw " << lhsOp << ", %ax\n"; *os << "  cwd\n"; *os << "  idivw " << rhsOp << "\n"; *os << "  movw %dx, " << destOp << "\n";
            } else if (sizeSuffix == "l") {
                *os << "  movl " << lhsOp << ", %eax\n"; *os << "  cltd\n"; *os << "  idivl " << rhsOp << "\n"; *os << "  movl %edx, " << destOp << "\n";
            } else {
                *os << "  movq " << lhsOp << ", %rax\n"; *os << "  cqto\n"; *os << "  idivq " << rhsOp << "\n"; *os << "  movq %rdx, " << destOp << "\n";
            }
        } else {
            if (sizeSuffix == "b") {
                *os << "  movb " << lhsOp << ", %al\n"; *os << "  movb $0, %ah\n"; *os << "  divb " << rhsOp << "\n"; *os << "  movb %ah, " << destOp << "\n";
            } else if (sizeSuffix == "w") {
                *os << "  movw " << lhsOp << ", %ax\n"; *os << "  movw $0, %dx\n"; *os << "  divw " << rhsOp << "\n"; *os << "  movw %dx, " << destOp << "\n";
            } else if (sizeSuffix == "l") {
                *os << "  movl " << lhsOp << ", %eax\n"; *os << "  movl $0, %edx\n"; *os << "  divl " << rhsOp << "\n"; *os << "  movl %edx, " << destOp << "\n";
            } else {
                *os << "  movq " << lhsOp << ", %rax\n"; *os << "  movq $0, %rdx\n"; *os << "  divq " << rhsOp << "\n"; *os << "  movq %rdx, " << destOp << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* dest = &instr;
        ir::Value* lhs = instr.getOperands()[0]->get();
        ir::Value* rhs = instr.getOperands()[1]->get();
        int32_t destOffset = cg.getStackOffset(dest);
        const ir::Type* type = lhs->getType();
        uint8_t rex = getRex(type);
        std::string sizeSuffix = getLoadStoreSuffix(type);

        emitLoadValue(cg, assembler, lhs, 0); // Load to %rax

        if (instr.getOpcode() == ir::Instruction::Rem) {
            if (sizeSuffix == "b") assembler.emitByte(0x98);
            else if (sizeSuffix == "w") { assembler.emitByte(0x66); assembler.emitByte(0x99); }
            else if (sizeSuffix == "l") assembler.emitByte(0x99);
            else { assembler.emitByte(0x48); assembler.emitByte(0x99); }
        } else {
            if (sizeSuffix == "b") { assembler.emitByte(0xB4); assembler.emitByte(0x00); }
            else if (sizeSuffix == "w") { assembler.emitBytes({0x66, 0x31, 0xD2}); }
            else if (sizeSuffix == "l") { assembler.emitBytes({0x31, 0xD2}); }
            else { assembler.emitBytes({0x48, 0x31, 0xD2}); }
        }

        uint8_t div_opcode = (instr.getOpcode() == ir::Instruction::Rem) ? 7 : 6;
        emitLoadValue(cg, assembler, rhs, 1); // Load to %rcx
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xF7, type));
        assembler.emitByte(0xC0 | (div_opcode << 3) | 1); // /div_opcode %rcx

        uint8_t rem_reg = (sizeSuffix == "b") ? 4 : 2; // ah=4, rdx=2
        emitRegMem(assembler, rex, getOpcode(0x89, type), rem_reg, destOffset);
    }
}

void SystemV_x64::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
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

void SystemV_x64::emitAnd(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  and" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0x23, type));
        assembler.emitByte(0xC1); // and %rcx, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitOr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  or" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0x0B, type));
        assembler.emitByte(0xC1); // or %rcx, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitXor(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  xor" << sizeSuffix << " " << rhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0x33, type));
        assembler.emitByte(0xC1); // xor %rcx, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitShl(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << rhsOp << ", %ecx\n";
        *os << "  shl" << (sizeSuffix == "b" ? "b" : sizeSuffix) << " %cl, " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xD3, type));
        assembler.emitByte(0xE0); // shl %cl, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitShr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << rhsOp << ", %ecx\n";
        *os << "  shr" << (sizeSuffix == "b" ? "b" : sizeSuffix) << " %cl, " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xD3, type));
        assembler.emitByte(0xE8); // shr %cl, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitSar(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* lhs = instr.getOperands()[0]->get();
    ir::Value* rhs = instr.getOperands()[1]->get();
    const ir::Type* type = lhs->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string lhsOp = cg.getValueAsOperand(lhs);
        std::string rhsOp = cg.getValueAsOperand(rhs);
        std::string sizeSuffix = getLoadStoreSuffix(type);
        std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
        *os << "  mov" << sizeSuffix << " " << lhsOp << ", " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << rhsOp << ", %ecx\n";
        *os << "  sar" << (sizeSuffix == "b" ? "b" : sizeSuffix) << " %cl, " << reg << "\n";
        *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, lhs, 0);
        emitLoadValue(cg, assembler, rhs, 1);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xD3, type));
        assembler.emitByte(0xF8); // sar %cl, %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        TypeInfo typeInfo = getTypeInfo(type);
        if (typeInfo.isFloatingPoint) {
            std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
            std::string negInstr = typeInfo.size == 32 ? "xorps" : "xorpd";
            *os << "  " << movInstr << " " << opOperand << ", %xmm0\n";
            *os << "  " << negInstr << " %xmm1, %xmm1\n";
            *os << "  " << (typeInfo.size == 32 ? "subss" : "subsd") << " %xmm0, %xmm1\n";
            *os << "  " << movInstr << " %xmm1, " << destOp << "\n";
        } else {
            std::string sizeSuffix = getLoadStoreSuffix(type);
            std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
            *os << "  mov" << sizeSuffix << " " << opOperand << ", " << reg << "\n";
            *os << "  neg" << sizeSuffix << " " << reg << "\n";
            *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, op, 0);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xF7, type));
        assembler.emitByte(0xD8); // neg %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitNot(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* op = instr.getOperands()[0]->get();
    const ir::Type* type = op->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string opOperand = cg.getValueAsOperand(op);
        TypeInfo typeInfo = getTypeInfo(type);
        if (typeInfo.isFloatingPoint) {
             *os << "  # NOT on float not directly supported\n";
        } else {
            std::string sizeSuffix = getLoadStoreSuffix(type);
            std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
            *os << "  mov" << sizeSuffix << " " << opOperand << ", " << reg << "\n";
            *os << "  not" << sizeSuffix << " " << reg << "\n";
            *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, op, 0);
        if (rex) assembler.emitByte(rex);
        assembler.emitByte(getOpcode(0xF7, type));
        assembler.emitByte(0xD0); // not %rax
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* src = instr.getOperands()[0]->get();
    const ir::Type* type = src->getType();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string srcOp = cg.getValueAsOperand(src);
        TypeInfo typeInfo = getTypeInfo(type);
        if (typeInfo.isFloatingPoint) {
            std::string movInstr = typeInfo.size == 32 ? "movss" : "movsd";
            *os << "  " << movInstr << " " << srcOp << ", %xmm0\n";
            *os << "  " << movInstr << " %xmm0, " << destOp << "\n";
        } else {
            std::string sizeSuffix = getLoadStoreSuffix(type);
            std::string reg = getRegisterName(getRegisters(RegisterClass::Integer)[0], type);
            if (sizeSuffix == "b" || sizeSuffix == "w") {
                *os << "  movz" << sizeSuffix << (sizeSuffix == "b" ? "bl" : "wl") << " " << srcOp << ", %eax\n";
                *os << "  mov" << sizeSuffix << " %a" << (sizeSuffix == "b" ? "l" : "x") << ", " << destOp << "\n";
            } else {
                *os << "  mov" << sizeSuffix << " " << srcOp << ", " << reg << "\n";
                *os << "  mov" << sizeSuffix << " " << reg << ", " << destOp << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex = getRex(type);
        emitLoadValue(cg, assembler, src, 0);
        emitRegMem(assembler, rex, getOpcode(0x89, type), 0, destOffset);
    }
}

void SystemV_x64::emitCall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        const auto& int_regs = getIntegerArgumentRegisters();
        unsigned int_reg_idx = 0;
        ir::Value* calleeValue = instr.getOperands()[0]->get();
        std::string callee = calleeValue->getName();
        std::vector<ir::Value*> stack_args;
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            if (dynamic_cast<ir::IntegerType*>(arg->getType())) {
                if (int_reg_idx < int_regs.size()) {
                    std::string argOperand = cg.getValueAsOperand(arg);
                    std::string suffix = getLoadStoreSuffix(arg->getType());
                    std::string dstReg = getRegisterName(int_regs[int_reg_idx++], arg->getType());
                    *os << "  mov" << suffix << " " << argOperand << ", " << dstReg << "\n";
                } else {
                    stack_args.push_back(arg);
                }
            }
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            std::string argOperand = cg.getValueAsOperand(arg);
            *os << "  pushq " << argOperand << "\n";
        }
        size_t total_stack_space = stack_args.size() * 8;

        *os << "  call " << callee << "\n";

        if (total_stack_space > 0) {
            *os << "  addq $" << total_stack_space << ", %rsp\n";
        }

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            std::string destOperand = cg.getValueAsOperand(&instr);
            std::string suffix = getLoadStoreSuffix(instr.getType());
            std::string srcReg = getRegisterName(getIntegerReturnRegister(), instr.getType());
            *os << "  mov" << suffix << " " << srcReg << ", " << destOperand << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        unsigned int_reg_idx = 0;
        std::vector<ir::Value*> stack_args;
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            ir::Value* arg = instr.getOperands()[i]->get();
            if (arg->getType()->isIntegerTy() || arg->getType()->isPointerTy()) {
                if (int_reg_idx < 6) {
                    uint8_t target_reg = 0;
                    switch(int_reg_idx) {
                        case 0: target_reg = 7; break; // rdi
                        case 1: target_reg = 6; break; // rsi
                        case 2: target_reg = 2; break; // rdx
                        case 3: target_reg = 1; break; // rcx
                        case 4: target_reg = 8; break; // r8
                        case 5: target_reg = 9; break; // r9
                    }
                    emitLoadValue(cg, assembler, arg, target_reg);
                } else {
                    stack_args.push_back(arg);
                }
                int_reg_idx++;
            }
        }

        // Ensure stack alignment for call if we have stack arguments
        if (!stack_args.empty()) {
            // This is complex, let's skip for now and see if 6-arg limit helps
        }

        std::reverse(stack_args.begin(), stack_args.end());
        for (ir::Value* arg : stack_args) {
            emitLoadValue(cg, assembler, arg, 10); // load into r10
            assembler.emitByte(0x41); assembler.emitByte(0x52); // push %r10
        }

        assembler.emitByte(0xE8);
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0); // Placeholder for relocation

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

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            uint8_t rex = getRex(instr.getType());
            emitRegMem(assembler, rex, getOpcode(0x89, instr.getType()), 0, destOffset);
        }

        if (!stack_args.empty()) {
            uint32_t stack_size = stack_args.size() * 8;
            assembler.emitBytes({0x48, 0x81, 0xC4});
            assembler.emitDWord(stack_size);
        }
    }
}

const std::vector<std::string>& SystemV_x64::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Float) return floatRegs;
    return integerRegs;
}

void SystemV_x64::emitPassArgument(CodeGen& cg, size_t argIndex,
                                  const std::string& value, const ir::Type* type) {
    (void)argIndex; (void)value; (void)type;
    if (cg.getTextStream()) {
        // Text emission for passing arguments is handled in emitCall for now
    } else {
        // Binary emission handled in emitCall
    }
}

void SystemV_x64::emitGetArgument(CodeGen& cg, size_t argIndex,
                                 const std::string& dest, const ir::Type* type) {
    (void)argIndex; (void)dest; (void)type;
    if (cg.getTextStream()) {
        // Text emission for getting arguments is handled in emitFunctionPrologue for now
    } else {
        // Binary emission handled in emitFunctionPrologue
    }
}

void SystemV_x64::emitCmp(CodeGen& cg, ir::Instruction& instr) {
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
            emitRegMem(assembler, 0, {prefix, 0x0F, 0x10}, 0, lhsOffset);
            emitRegMem(assembler, 0, {0x0F, 0x2E}, 0, rhsOffset);
        } else {
            uint8_t rex = getRex(lhs->getType());
            emitLoadValue(cg, assembler, lhs, 0); // rax
            emitLoadValue(cg, assembler, rhs, 1); // rcx
            if (rex) assembler.emitByte(rex);
            assembler.emitByte(getOpcode(0x3B, lhs->getType()));
            assembler.emitByte(0xC1); // cmp %rcx, %rax
        }

        // setcc %al
        assembler.emitBytes({0x0F, set_byte, 0xC0});
        // movzbl %al, %eax
        assembler.emitBytes({0x0F, 0xB6, 0xC0});

        uint8_t rex_store = getRex(dest->getType());
        if (rex_store == 0x48) {
            // movzbq %al, %rax
            assembler.emitBytes({0x48, 0x0F, 0xB6, 0xC0});
        }
        emitRegMem(assembler, rex_store, getOpcode(0x89, dest->getType()), 0, cg.getStackOffset(dest));
    }
}

void SystemV_x64::emitCast(CodeGen& cg, ir::Instruction& instr,
                          const ir::Type* fromType, const ir::Type* toType) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = instr.getOperands()[0]->get();
        std::string srcOp = cg.getValueAsOperand(src);
        std::string destOp = cg.getValueAsOperand(&instr);

        if (fromType->isIntegerTy() && toType->isFloatTy()) {
            *os << "  cvtsi2ss " << srcOp << ", %xmm0\n";
            *os << "  movss %xmm0, " << destOp << "\n";
        } else if (fromType->isFloatTy() && toType->isIntegerTy()) {
            *os << "  cvttss2si " << srcOp << ", %rax\n";
            *os << "  movq %rax, " << destOp << "\n";
        } else if (fromType->isIntegerTy() && toType->isIntegerTy()) {
             auto* fromInt = static_cast<const ir::IntegerType*>(fromType);
             auto* toInt = static_cast<const ir::IntegerType*>(toType);
             if (fromInt->getBitwidth() < toInt->getBitwidth()) {
                 *os << "  movslq " << srcOp << ", %rax\n";
                 *os << "  movq %rax, " << destOp << "\n";
             } else {
                 *os << "  movl " << srcOp << ", %eax\n";
                 *os << "  movl %eax, " << destOp << "\n";
             }
        } else {
            *os << "  # Unhandled cast from " << fromType->toString() << " to " << toType->toString() << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        ir::Value* src = instr.getOperands()[0]->get();
        ir::Value* dest = &instr;
        int32_t destOffset = cg.getStackOffset(dest);

        if (fromType->isIntegerTy() && toType->isIntegerTy()) {
            auto* fromIntType = static_cast<const ir::IntegerType*>(fromType);
            auto* toIntType = static_cast<const ir::IntegerType*>(toType);
            if (fromIntType->getBitwidth() == 32 && toIntType->getBitwidth() == 64) {
                emitLoadValue(cg, assembler, src, 0); // eax
                // movslq %eax, %rax
                assembler.emitBytes({0x48, 0x63, 0xC0});
                emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
            }
            else if (fromIntType->getBitwidth() == 64 && toIntType->getBitwidth() == 32) {
                 emitLoadValue(cg, assembler, src, 0); // rax
                 emitRegMem(assembler, 0, 0x89, 0, destOffset);
            }
        } else if (fromType->isIntegerTy() && toType->isFloatTy()) {
            emitLoadValue(cg, assembler, src, 0); // rax
            // cvtsi2ss %eax, %xmm0
            assembler.emitBytes({0xF3, 0x0F, 0x2A, 0xC0});
            emitRegMem(assembler, 0, {0xF3, 0x0F, 0x11}, 0, destOffset);
        } else if (fromType->isFloatTy() && toType->isIntegerTy()) {
            // Load float to xmm0
            if (cg.getStackOffsets().count(src)) {
                emitRegMem(assembler, 0, {0xF3, 0x0F, 0x10}, 0, cg.getStackOffset(src));
            }
            // cvttss2si %xmm0, %rax
            assembler.emitBytes({0xF3, 0x48, 0x0F, 0x2C, 0xC0});
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset);
        }
    }
}

void SystemV_x64::emitVAStart(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # va_start not implemented\n";
    } else {
        // va_start(ap) stub for binary mode
    }
}

void SystemV_x64::emitVAArg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # va_arg not implemented\n";
    } else {
        // va_arg(ap, type) stub for binary mode
    }
}

void SystemV_x64::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* dest = &instr;
    ir::Value* srcPtr = instr.getOperands()[0]->get();

    if (auto* os = cg.getTextStream()) {
        std::string destOp = cg.getValueAsOperand(dest);
        std::string srcOp = cg.getValueAsOperand(srcPtr);
        std::string mov_instr = getMoveInstruction(instr.getType());

        if (dynamic_cast<ir::GlobalValue*>(srcPtr) || dynamic_cast<ir::GlobalVariable*>(srcPtr)) {
            std::string ptr_reg = getRegisterName("%r11", instr.getType());
            *os << "  " << mov_instr << " " << srcPtr->getName() << "(%rip), " << ptr_reg << "\n";
            *os << "  " << getMoveInstruction(dest->getType()) << " " << ptr_reg << ", " << destOp << "\n";
        } else {
            std::string ptr_reg = "%r11";
            std::string val_reg = getRegisterName("%r11", instr.getType());
            *os << "  movq " << srcOp << ", " << ptr_reg << "\n";
            *os << "  " << mov_instr << " (" << ptr_reg << "), " << val_reg << "\n";
            *os << "  " << getMoveInstruction(dest->getType()) << " " << val_reg << ", " << destOp << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        int32_t destOffset = cg.getStackOffset(dest);
        uint8_t rex_base = getRex(dest->getType());

        if (auto* global = dynamic_cast<ir::GlobalValue*>(srcPtr)) {
            // Load the VALUE at the global address into %r11
            // movq global(%rip), %r11
            assembler.emitByte(0x4C); // REX.W=1, REX.R=1 (for r11)
            assembler.emitByte(0x8B);
            assembler.emitByte(0x1D); // ModRM: Mod=00, Reg=011, RM=101 (rip-relative)
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = global->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);

            emitRegMem(assembler, rex_base | 0x44, getOpcode(0x89, dest->getType()), 3, destOffset); // r11 is reg 3 with REX.R
        } else if (auto* gv = dynamic_cast<ir::GlobalVariable*>(srcPtr)) {
            // Load the VALUE at the global variable address into %r11
            assembler.emitByte(0x4C);
            assembler.emitByte(0x8B);
            assembler.emitByte(0x1D);
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = gv->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);

            emitRegMem(assembler, rex_base | 0x44, getOpcode(0x89, dest->getType()), 3, destOffset);
        } else {
            // Load address into %r11 (index 11)
            emitLoadValue(cg, assembler, srcPtr, 11);
            // mov (%r11), %r11
            auto op = instr.getOpcode();
            uint8_t rex = 0x4D; // W=1, R=1, B=1 for r11 to r11
            if (op == ir::Instruction::Loadub || op == ir::Instruction::Loadsb) {
                assembler.emitByte(0x45); // R=1, B=1
                assembler.emitByte(0x0F);
                assembler.emitByte(op == ir::Instruction::Loadub ? 0xB6 : 0xBE);
                assembler.emitByte(0x1B); // Mod=00, Reg=011, RM=011
            } else if (op == ir::Instruction::Loaduh || op == ir::Instruction::Loadsh) {
                assembler.emitByte(0x45);
                assembler.emitByte(0x0F);
                assembler.emitByte(op == ir::Instruction::Loaduh ? 0xB7 : 0xBF);
                assembler.emitByte(0x1B);
            } else {
                uint8_t size = dest->getType()->getSize();
                if (size == 1) {
                    assembler.emitBytes({0x45, 0x0F, 0xB6, 0x1B});
                } else {
                    if (size == 4) assembler.emitByte(0x45);
                    else assembler.emitByte(0x4D);
                    assembler.emitByte(0x8B);
                    assembler.emitByte(0x1B);
                }
            }
            // Store %r11 to stack
            uint8_t rex_store = (dest->getType()->getSize() == 8) ? 0x4C : 0x44;
            emitRegMem(assembler, rex_store, getOpcode(0x89, dest->getType()), 3, destOffset);
        }
    }
}

void SystemV_x64::emitStore(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* srcVal = instr.getOperands()[0]->get();
    ir::Value* destPtr = instr.getOperands()[1]->get();

    if (auto* os = cg.getTextStream()) {
        std::string srcOp = cg.getValueAsOperand(srcVal);
        std::string mov_instr = getMoveInstruction(srcVal->getType());
        std::string val_reg = getRegisterName("%r11", srcVal->getType());

        *os << "  " << mov_instr << " " << srcOp << ", " << val_reg << "\n";
        if (dynamic_cast<ir::GlobalValue*>(destPtr) || dynamic_cast<ir::GlobalVariable*>(destPtr)) {
            *os << "  " << mov_instr << " " << val_reg << ", " << destPtr->getName() << "(%rip)\n";
        } else {
            std::string ptr_reg = "%r11";
            std::string destOp = cg.getValueAsOperand(destPtr);
            *os << "  movq " << destOp << ", " << ptr_reg << "\n";
            *os << "  " << mov_instr << " " << val_reg << ", (" << ptr_reg << ")\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, srcVal, 11); // r11 = val

        if (auto* global = dynamic_cast<ir::GlobalValue*>(destPtr)) {
            uint8_t size = srcVal->getType()->getSize();
            uint8_t rex = (size == 8 ? 0x4C : 0x44); // REX.W if 64-bit, REX.R for r11
            assembler.emitByte(rex);
            assembler.emitByte(getOpcode(0x89, srcVal->getType()));
            assembler.emitByte(0x1D); // ModRM for RIP-relative
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = global->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);
        } else if (auto* gv = dynamic_cast<ir::GlobalVariable*>(destPtr)) {
            uint8_t size = srcVal->getType()->getSize();
            uint8_t rex = (size == 8 ? 0x4C : 0x44);
            assembler.emitByte(rex);
            assembler.emitByte(getOpcode(0x89, srcVal->getType()));
            assembler.emitByte(0x1D);
            uint64_t reloc_offset = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc;
            reloc.offset = reloc_offset;
            reloc.symbolName = gv->getName();
            reloc.type = "R_X86_64_PC32";
            reloc.sectionName = ".text";
            reloc.addend = -4;
            cg.addRelocation(reloc);
        } else {
            // Load address into %r10 (index 10)
            emitLoadValue(cg, assembler, destPtr, 10);
            // mov %r11, (%r10)
            auto op = instr.getOpcode();
            uint8_t size = srcVal->getType()->getSize();
            if (op == ir::Instruction::Storeb || size == 1) {
                assembler.emitBytes({0x45, 0x88, 0x1A}); // R=1, B=1, mov r11b, (r10)
            } else if (op == ir::Instruction::Storeh || size == 2) {
                assembler.emitByte(0x66);
                assembler.emitBytes({0x45, 0x89, 0x1A});
            } else if (size == 4) {
                assembler.emitBytes({0x45, 0x89, 0x1A});
            } else {
                assembler.emitBytes({0x4D, 0x89, 0x1A});
            }
        }
    }
}

void SystemV_x64::emitAlloc(CodeGen& cg, ir::Instruction& instr) {
    int32_t pointerOffset = cg.getStackOffset(&instr);
    ir::Value* sizeVal = instr.getOperands().empty() ? nullptr : instr.getOperands()[0]->get();
    ir::ConstantInt* sizeConst = (sizeVal) ? dynamic_cast<ir::ConstantInt*>(sizeVal) : nullptr;
    uint64_t size = sizeConst ? sizeConst->getValue() : 8;
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        *os << "  movq __heap_ptr(%rip), %r11\n";
        *os << "  movq %r11, " << pointerOffset << "(%rbp)\n";
        *os << "  addq $" << alignedSize << ", %r11\n";
        *os << "  movq %r11, __heap_ptr(%rip)\n";
    } else {
        auto& assembler = cg.getAssembler();
        // Load __heap_ptr into %r11
        assembler.emitBytes({0x4C, 0x8B, 0x1D});
        uint64_t reloc_offset_load = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc_load;
        reloc_load.offset = reloc_offset_load;
        reloc_load.symbolName = "__heap_ptr";
        reloc_load.type = "R_X86_64_PC32";
        reloc_load.sectionName = ".text";
        reloc_load.addend = -4;
        cg.addRelocation(reloc_load);

        // movq %r11, pointerOffset(%rbp)
        emitRegMem(assembler, 0x4C, 0x89, 3, pointerOffset);

        // addq $alignedSize, %r11
        if (alignedSize <= 127) {
            assembler.emitBytes({0x49, 0x83, 0xC3, static_cast<uint8_t>(alignedSize)});
        } else {
            assembler.emitBytes({0x49, 0x81, 0xC3});
            assembler.emitDWord(static_cast<uint32_t>(alignedSize));
        }

        // Store %r11 back to __heap_ptr
        assembler.emitBytes({0x4C, 0x89, 0x1D});
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

void SystemV_x64::emitJmp(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* target = instr.getOperands()[0]->get();
    ir::BasicBlock* targetBB = dynamic_cast<ir::BasicBlock*>(target);
    ir::BasicBlock* currentBB = instr.getParent();

    // PHI lowering
    if (targetBB) {
        for (auto& t_instr : targetBB->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(t_instr.get())) {
                ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                if (incoming) {
                    if (auto* os = cg.getTextStream()) {
                        std::string src = cg.getValueAsOperand(incoming);
                        std::string dst = cg.getValueAsOperand(phi);
                        if (src != dst) {
                             std::string suffix = getLoadStoreSuffix(phi->getType());
                             std::string reg = getRegisterName("%rax", phi->getType());
                             *os << "  mov" << suffix << " " << src << ", " << reg << "\n";
                             *os << "  mov" << suffix << " " << reg << ", " << dst << "\n";
                        }
                    } else {
                        // Binary mode PHI lowering
                        auto& assembler = cg.getAssembler();
                        emitLoadValue(cg, assembler, incoming, 11); // R11
                        
                        if (phi->hasPhysicalRegister()) {
                            uint8_t phys = static_cast<uint8_t>(phi->getPhysicalRegister());
                            static const uint8_t x64_map[] = {0, 1, 2, 6, 7, 8, 9, 10, 3, 12, 13, 14, 15};
                            uint8_t dst_reg = x64_map[phys];
                            
                            // mov dst_reg, r11
                            uint8_t rex = getRex(phi->getType()) | 0x41; // REX.B for r11 source
                            if (dst_reg >= 8) rex |= 0x44; // REX.R for destination
                            assembler.emitByte(rex);
                            assembler.emitByte(0x8B); // mov r, rm
                            assembler.emitByte(0xC0 | ((dst_reg & 7) << 3) | (11 & 7));
                        } else {
                            int32_t dstOffset = cg.getStackOffset(phi);
                            uint8_t rex = getRex(phi->getType()) | 0x44; // REX.R for R11
                            emitRegMem(assembler, rex, getOpcode(0x89, phi->getType()), 11 & 7, dstOffset);
                        }
                    }
                }
            } else break;
        }
    }

    if (auto* os = cg.getTextStream()) {
        if (targetBB) {
            *os << "  jmp " << targetBB->getParent()->getName() << "_" << targetBB->getName() << "\n";
        } else {
            *os << "  jmp " << target->getName() << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        assembler.emitByte(0xE9);
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0); // Placeholder for relocation

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        if (targetBB) {
            reloc.symbolName = targetBB->getParent()->getName() + "_" + targetBB->getName();
        } else {
            reloc.symbolName = target->getName();
        }
        reloc.type = "R_X86_64_PC32";
        reloc.sectionName = ".text";
        reloc.addend = -4;
        cg.addRelocation(reloc);
    }
}

void SystemV_x64::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getOperands().size() > 0) {
            ir::Value* retVal = instr.getOperands()[0]->get();
            std::string retOp = cg.getValueAsOperand(retVal);
            if (retVal->getType()->isFloatingPoint()) {
                std::string movInstr = retVal->getType()->isFloatTy() ? "movss" : "movsd";
                *os << "  " << movInstr << " " << retOp << ", %xmm0\n";
            } else {
                std::string sizeSuffix = getLoadStoreSuffix(retVal->getType());
                std::string reg = getRegisterName("%rax", retVal->getType());
                *os << "  mov" << sizeSuffix << " " << retOp << ", " << reg << "\n";
            }
        }
        *os << "  jmp " << getEpilogueLabel(instr.getParent()->getParent()) << "\n";
        return;
    } else {
        if (!instr.getOperands().empty()) {
            auto& assembler = cg.getAssembler();
            ir::Value* retVal = instr.getOperands()[0]->get();
            if (retVal != nullptr) {
                emitLoadValue(cg, assembler, retVal, 0); // Load to %rax
            }
        }
    }
    emitFunctionEpilogue(cg, *instr.getParent()->getParent());
}

void SystemV_x64::emitBr(CodeGen& cg, ir::Instruction& instr) {
    ir::Value* trueTarget = instr.getOperands()[1]->get();
    ir::BasicBlock* trueBB = dynamic_cast<ir::BasicBlock*>(trueTarget);
    ir::BasicBlock* currentBB = instr.getParent();

    if (auto* os = cg.getTextStream()) {
        ir::Value* cond = instr.getOperands()[0]->get();
        std::string condOp = cg.getValueAsOperand(cond);
        std::string suffix = getLoadStoreSuffix(cond->getType());
        if (suffix == "ss" || suffix == "sd") {
            *os << "  # unsupported floating-point branch condition, treating as integer compare\n";
            suffix = "q";
        }
        *os << "  cmp" << suffix << " $0, " << condOp << "\n";

        // We can't easily lower PHIs on conditional branches if both targets have PHIs
        // because we might clobber the condition or values needed for the other branch.
        // However, most SSA-to-machine lowering does this by inserting blocks.
        // For now, we assume simple CFGs or handle PHIs before the branch.
        
        // This is a hack: lower PHIs for true branch if condition is true, false branch otherwise?
        // No, that's not how it works. PHIs are conceptually simultaneous.
        // Real compilers insert "split blocks" for this.
        
        if (trueBB) {
            *os << "  jne " << trueBB->getParent()->getName() << "_" << trueBB->getName() << "\n";
        } else {
            *os << "  jne " << trueTarget->getName() << "\n";
        }

        if (instr.getOperands().size() > 2) {
            ir::Value* falseTarget = instr.getOperands()[2]->get();
            ir::BasicBlock* falseBB = dynamic_cast<ir::BasicBlock*>(falseTarget);
            if (falseBB) {
                // Lower PHIs for false branch since it's a fallthrough/jmp
                for (auto& f_instr : falseBB->getInstructions()) {
                    if (auto* phi = dynamic_cast<ir::PhiNode*>(f_instr.get())) {
                        ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                        if (incoming) {
                             std::string src = cg.getValueAsOperand(incoming);
                             std::string dst = cg.getValueAsOperand(phi);
                             if (src != dst) {
                                 std::string suffix = getLoadStoreSuffix(phi->getType());
                                 std::string reg = getRegisterName("%rax", phi->getType());
                                 *os << "  mov" << suffix << " " << src << ", " << reg << "\n";
                                 *os << "  mov" << suffix << " " << reg << ", " << dst << "\n";
                             }
                        }
                    } else break;
                }
                *os << "  jmp " << falseBB->getParent()->getName() << "_" << falseBB->getName() << "\n";
            } else {
                *os << "  jmp " << falseTarget->getName() << "\n";
            }
        }
    } else {
        auto& assembler = cg.getAssembler();
        emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 0); // rax
        // cmpq $0, %rax
        assembler.emitBytes({0x48, 0x83, 0xF8, 0x00});
        // jne <true_label>
        assembler.emitBytes({0x0F, 0x85});
        uint64_t reloc_offset_true = assembler.getCodeSize();
        assembler.emitDWord(0);

        CodeGen::RelocationInfo reloc_true;
        reloc_true.offset = reloc_offset_true;
        if (trueBB) {
            reloc_true.symbolName = trueBB->getParent()->getName() + "_" + trueBB->getName();
        } else {
            reloc_true.symbolName = trueTarget->getName();
        }
        reloc_true.type = "R_X86_64_PC32";
        reloc_true.sectionName = ".text";
        reloc_true.addend = -4;
        cg.addRelocation(reloc_true);

        if (instr.getOperands().size() > 2) {
            ir::Value* falseTarget = instr.getOperands()[2]->get();
            ir::BasicBlock* falseBB = dynamic_cast<ir::BasicBlock*>(falseTarget);
            if (falseBB) {
                // Lower PHIs for false path
                for (auto& f_instr : falseBB->getInstructions()) {
                    if (auto* phi = dynamic_cast<ir::PhiNode*>(f_instr.get())) {
                        ir::Value* incoming = phi->getIncomingValueForBlock(currentBB);
                        if (incoming) {
                            emitLoadValue(cg, assembler, incoming, 11); // R11
                            if (phi->hasPhysicalRegister()) {
                                uint8_t phys = static_cast<uint8_t>(phi->getPhysicalRegister());
                                static const uint8_t x64_map[] = {0, 1, 2, 6, 7, 8, 9, 10, 3, 12, 13, 14, 15};
                                uint8_t dst_reg = x64_map[phys];
                                uint8_t rex = getRex(phi->getType()) | 0x41;
                                if (dst_reg >= 8) rex |= 0x44;
                                assembler.emitByte(rex);
                                assembler.emitByte(0x8B);
                                assembler.emitByte(0xC0 | ((dst_reg & 7) << 3) | (11 & 7));
                            } else {
                                int32_t dstOffset = cg.getStackOffset(phi);
                                uint8_t rex = getRex(phi->getType()) | 0x44;
                                emitRegMem(assembler, rex, getOpcode(0x89, phi->getType()), 11 & 7, dstOffset);
                            }
                        }
                    } else break;
                }
            }
            // jmp <false_label>
            assembler.emitByte(0xE9);
            uint64_t reloc_offset_false = assembler.getCodeSize();
            assembler.emitDWord(0);

            CodeGen::RelocationInfo reloc_false;
            reloc_false.offset = reloc_offset_false;
            if (falseBB) {
                reloc_false.symbolName = falseBB->getParent()->getName() + "_" + falseBB->getName();
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


void SystemV_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n# --- Executable Entry Point ---\n";
        *os << ".globl _start\n";
        *os << "_start:\n";
        *os << "  # Entry RSP = 16N + 8\n";
        *os << "  pushq %rax # Just to align to 16 bytes: RSP = 16N\n";
        *os << "  # Call the user's main function (pushes 8, RSP = 16N-8 = 16M+8)\n";
        *os << "  # Wait, System V says RSP should be 16-aligned BEFORE call.\n";
        *os << "  # So RSP should be 16N before call.\n";
        *os << "  # Entry RSP=16N+8. After push: 16N. Good.\n";
        *os << "  call main\n\n";
        *os << "  addq $8, %rsp # Cleanup alignment push\n";
        *os << "  # Exit with the return code from main\n";
        *os << "  mov %eax, %edi  # Move return value to exit code argument\n";
        *os << "  mov $60, %eax   # 60 is the syscall number for exit\n";
        *os << "  syscall         # Invoke kernel\n";
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

        // align RSP to 16-byte boundary: andq $-16, %rsp
        assembler.emitBytes({0x48, 0x83, 0xE4, 0xF0});
        // push dummy to make it 16N-8 before call
        assembler.emitByte(0x50); // push %rax -> RSP = 16N-8
        assembler.emitByte(0xE8);
        uint64_t reloc_offset = assembler.getCodeSize();
        assembler.emitDWord(0); // Placeholder for relocation

        CodeGen::RelocationInfo reloc;
        reloc.offset = reloc_offset;
        reloc.symbolName = "main";
        reloc.type = "R_X86_64_PC32";
        reloc.sectionName = ".text";
        reloc.addend = -4;
        cg.addRelocation(reloc);

        // exit
        assembler.emitBytes({0x48, 0x89, 0xC7}); // mov %rax, %rdi
        assembler.emitBytes({0x48, 0xC7, 0xC0}); // mov $60, %rax
        assembler.emitDWord(60);
        assembler.emitBytes({0x0F, 0x05});
        
    }
}

void SystemV_x64::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
}

uint64_t SystemV_x64::getSyscallNumber(ir::SyscallId id) const {
    switch (id) {
        case ir::SyscallId::Exit: return 60;
        case ir::SyscallId::Read: return 0;
        case ir::SyscallId::Write: return 1;
        case ir::SyscallId::OpenAt: return 257;
        case ir::SyscallId::Close: return 3;
        case ir::SyscallId::LSeek: return 8;
        case ir::SyscallId::MMap: return 9;
        case ir::SyscallId::MUnmap: return 11;
        case ir::SyscallId::MProtect: return 10;
        case ir::SyscallId::Brk: return 12;
        case ir::SyscallId::MkDirAt: return 258;
        case ir::SyscallId::UnlinkAt: return 263;
        case ir::SyscallId::RenameAt: return 264;
        case ir::SyscallId::GetDents64: return 217;
        case ir::SyscallId::ClockGetTime: return 228;
        case ir::SyscallId::NanoSleep: return 35;
        case ir::SyscallId::RtSigAction: return 13;
        case ir::SyscallId::RtSigProcMask: return 14;
        case ir::SyscallId::RtSigReturn: return 15;
        case ir::SyscallId::GetRandom: return 318;
        case ir::SyscallId::Uname: return 63;
        case ir::SyscallId::GetPid: return 39;
        case ir::SyscallId::GetTid: return 186;
        case ir::SyscallId::Fork: return 57;
        case ir::SyscallId::Execve: return 59;
        case ir::SyscallId::Clone: return 56;
        case ir::SyscallId::Wait4: return 61;
        case ir::SyscallId::Kill: return 62;
        default: return 0;
    }
}

void SystemV_x64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    if (auto* os = cg.getTextStream()) {
        // Syscall ABI: rax (number), rdi, rsi, rdx, r10, r8, r9

        // Move syscall number to rax
        if (sid != ir::SyscallId::None) {
            *os << "  movq $" << getSyscallNumber(sid) << ", %rax\n";
        } else {
            ir::Value* numVal = instr.getOperands()[0]->get();
            *os << "  movq " << cg.getValueAsOperand(numVal) << ", %rax\n";
        }

        // Move arguments
        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i + 1 : i;
            ir::Value* arg = instr.getOperands()[i]->get();
            std::string dest_reg;
            switch(argIdx) {
                case 1: dest_reg = "%rdi"; break;
                case 2: dest_reg = "%rsi"; break;
                case 3: dest_reg = "%rdx"; break;
                case 4: dest_reg = "%r10"; break;
                case 5: dest_reg = "%r8"; break;
                case 6: dest_reg = "%r9"; break;
            }
            if (!dest_reg.empty()) {
                *os << "  movq " << cg.getValueAsOperand(arg) << ", " << dest_reg << "\n";
            }
        }
        *os << "  syscall\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
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
                case 1: reg = 7; break; // rdi
                case 2: reg = 6; break; // rsi
                case 3: reg = 2; break; // rdx
                case 4: reg = 10; break; // r10
                case 5: reg = 8; break; // r8
                case 6: reg = 9; break; // r9
                default: continue;
            }
            emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), reg);
        }

        // syscall (0F 05)
        assembler.emitBytes({0x0F, 0x05});

        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            emitRegMem(assembler, 0x48, 0x89, 0, destOffset); // mov rax, [rbp+offset]
        }
    }
}

} // namespace target
} // namespace codegen
