#include "target/architecture/x64/X64Architecture.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include "codegen/asm/Assembler.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include <ostream>

namespace target {

X64Architecture::X64Architecture(X64ABI abi) : abi(abi) {
    initRegisters();
}

void X64Architecture::initRegisters() {
    if (abi == X64ABI::SystemV) {
        integerRegs = {"r10", "r11", "rbx", "r12", "r13", "r14", "r15"};
        integerArgRegs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    } else {
        integerRegs = {"rbx", "rsi", "rdi", "r12", "r13", "r14", "r15"};
        integerArgRegs = {"rcx", "rdx", "r8", "r9"};
        floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3"};
    }
    floatRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    intReturnReg = "rax"; floatReturnReg = "xmm0"; framePtrReg = "rbp"; stackPtrReg = "rsp";
    for (const auto& r : integerRegs) { callerSaved[r] = false; calleeSaved[r] = true; }
}

TypeInfo X64Architecture::getTypeInfo(const ir::Type* type) const {
    if (!type || type->isVoidTy()) return {0, 0, RegisterClass::Integer, false, false};
    if (type->isPointerTy()) return {8, 8, RegisterClass::Integer, false, false};
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        int w = it->getBitwidth();
        if (w <= 8) return {1, 1, RegisterClass::Integer, false, false};
        if (w <= 16) return {2, 2, RegisterClass::Integer, false, false};
        if (w <= 32) return {4, 4, RegisterClass::Integer, false, false};
        return {8, 8, RegisterClass::Integer, false, false};
    }
    return {8, 8, RegisterClass::Integer, false, false};
}

const std::vector<std::string>& X64Architecture::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Integer) return integerRegs;
    if (regClass == RegisterClass::Float) return floatRegs;
    return vectorRegs;
}

const std::string& X64Architecture::getReturnRegister(const ir::Type* type) const {
    if (type && (type->isFloatTy() || type->isDoubleTy())) return floatReturnReg;
    return intReturnReg;
}

void X64Architecture::emitHeader(CodeGen& cg) {
}

void X64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (abi == X64ABI::SystemV) {
        if (auto* os = cg.getTextStream()) {
            *os << "  pushq %rbp\n  movq %rsp, %rbp\n";
            *os << "  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n";
        }
        int current_offset = -48;
        for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } } }
        int stack_alloc = std::abs(current_offset + 40);
        if ((stack_alloc + 48 + 8) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 48 + 8) % 16);
        if (auto* os = cg.getTextStream()) {
            if (stack_alloc > 0) *os << "  subq $" << stack_alloc << ", %rsp\n";
            int j = 0; for (auto& param : func.getParameters()) { if (j < 6) *os << "  movq %" << integerArgRegs[j] << ", " << formatStackOperand(cg.getStackOffsets()[param.get()]) << "\n"; j++; }
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << "  push rbp\n  mov rbp, rsp\n";
            *os << "  push rbx\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n";
        } else {
            auto& as = cg.getAssembler();
            as.emitByte(0x55); as.emitBytes({0x48, 0x89, 0xE5});
            as.emitByte(0x53); as.emitByte(0x56); as.emitByte(0x57);
            as.emitBytes({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
        }
        int current_offset = -64;
        for (auto& param : func.getParameters()) { cg.getStackOffsets()[param.get()] = current_offset; current_offset -= 8; }
        for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { cg.getStackOffsets()[instr.get()] = current_offset; current_offset -= 8; } } }
        int stack_alloc = std::abs(current_offset + 56) + 32; // Shadow space
        if ((stack_alloc + 64 + 8) % 16 != 0) stack_alloc += 16 - ((stack_alloc + 64 + 8) % 16);
        if (auto* os = cg.getTextStream()) {
            if (stack_alloc > 0) *os << "  sub rsp, " << stack_alloc << "\n";
            int j = 0; for (auto& param : func.getParameters()) { if (j < 4) *os << "  mov " << formatStackOperand(cg.getStackOffsets()[param.get()]) << ", " << integerArgRegs[j] << "\n"; j++; }
        } else {
            auto& as = cg.getAssembler();
            if (stack_alloc > 0) { if (stack_alloc <= 127) as.emitBytes({0x48, 0x83, 0xEC, (uint8_t)stack_alloc}); else { as.emitBytes({0x48, 0x81, 0xEC}); as.emitDWord(stack_alloc); } }
            int j = 0; for (auto& param : func.getParameters()) { if (j < 4) { uint8_t r = getArchRegIndex(integerArgRegs[j]); emitRegMem(as, (r >= 8 ? 0x4C : 0x48), 0x89, r & 7, cg.getStackOffsets()[param.get()]); } j++; }
        }
    }
}

void X64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (abi == X64ABI::SystemV) {
        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            *os << "  leaq -40(%rbp), %rsp\n";
            *os << "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n";
            *os << "  leave\n  ret\n";
        }
    } else {
        if (auto* os = cg.getTextStream()) {
            *os << func.getName() << "_epilogue" << ":\n";
            *os << "  lea rsp, [rbp - 56]\n";
            *os << "  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbx\n";
            *os << "  leave\n  ret\n";
        } else {
            auto& as = cg.getAssembler();
            as.emitBytes({0x48, 0x8D, 0x65, 0xC8});
            as.emitBytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C});
            as.emitByte(0x5F); as.emitByte(0x5E); as.emitByte(0x5B);
            as.emitByte(0xC9); as.emitByte(0xC3);
        }
    }
}

void X64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  jmp " << i.getParent()->getParent()->getName() << "_epilogue" << "\n";
    } else {
        if (!i.getOperands().empty()) emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize();
        cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getParent()->getParent()->getName() + "_epilogue", ".text"});
    }
}

void X64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  addq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x01, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  subq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x29, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  imulq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x0F, 0xAF, 0xC1});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  cqto\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  idivq " << rcx << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    std::string rdx = (abi == X64ABI::SystemV) ? "%rdx" : "rdx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  cqto\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  idivq " << rcx << "\n";
        *os << "  movq " << rdx << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x99});
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xF9});
        emitStoreResult(cg, i, 2);
    }
}

void X64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  andq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x21, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  orq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x09, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  xorq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x31, 0xC8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  shlq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xE0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  shrq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xE8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rcx = (abi == X64ABI::SystemV) ? "%rcx" : "rcx";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rcx << "\n";
        *os << "  sarq %cl, " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0xD3, 0xF8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  negq " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xD8});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  notq " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0xF7, 0xD0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        size_t maxArgs = (abi == X64ABI::SystemV) ? 6 : 4;
        for (size_t j = 1; j < std::min(i.getOperands().size(), maxArgs + 1); ++j) {
            *os << "  movq " << cg.getValueAsOperand(i.getOperands()[j]->get()) << ", " << getRegisterName(integerArgRegs[j-1], i.getOperands()[j]->get()->getType()) << "\n";
        }
        *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << getRegisterName("rax", i.getType()) << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    } else {
        size_t maxArgs = (abi == X64ABI::SystemV) ? 6 : 4;
        for (size_t j = 1; j < std::min(i.getOperands().size(), maxArgs + 1); ++j) {
            uint8_t r = getArchRegIndex(integerArgRegs[j-1]);
            emitLoadValue(cg, cg.getAssembler(), i.getOperands()[j]->get(), r);
        }
        cg.getAssembler().emitByte(0xE8);
        uint64_t off = cg.getAssembler().getCodeSize();
        cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, i.getOperands()[0]->get()->getName(), ".text"});
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void X64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void X64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void X64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}

void X64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string al = (abi == X64ABI::SystemV) ? "%al" : "al";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    if (auto* os = cg.getTextStream()) {
        std::string set; switch(i.getOpcode()){case ir::Instruction::Ceq:set="sete";break;case ir::Instruction::Cne:set="setne";break;case ir::Instruction::Cslt:set="setl";break;case ir::Instruction::Csle:set="setle";break;case ir::Instruction::Csgt:set="setg";break;case ir::Instruction::Csge:set="setge";break;default:set="sete";break;}
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  cmpq " << cg.getValueAsOperand(i.getOperands()[1]->get()) << ", " << rax << "\n";
        *os << "  " << set << " " << al << "\n";
        if (abi == X64ABI::SystemV) *os << "  movzbq " << al << ", " << rax << "\n";
        else *os << "  movzx " << eax << ", " << al << "\n";
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[1]->get(), 1);
        cg.getAssembler().emitBytes({0x48, 0x39, 0xC8});
        uint8_t s = 0x94; switch(i.getOpcode()){case ir::Instruction::Ceq:s=0x94;break;case ir::Instruction::Cne:s=0x95;break;case ir::Instruction::Cslt:s=0x9C;break;case ir::Instruction::Csle:s=0x9E;break;case ir::Instruction::Csgt:s=0x9F;break;case ir::Instruction::Csge:s=0x9D;break;default:s=0x94;break;}
        cg.getAssembler().emitBytes({0x0F, s, 0xC0, 0x48, 0x0F, 0xB6, 0xC0});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {}
void X64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void X64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}

void X64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8; bool isSigned = true;
    switch(i.getOpcode()) {
        case ir::Instruction::Loadub: size = 1; isSigned = false; break;
        case ir::Instruction::Loadsb: size = 1; isSigned = true; break;
        case ir::Instruction::Loaduh: size = 2; isSigned = false; break;
        case ir::Instruction::Loadsh: size = 2; isSigned = true; break;
        case ir::Instruction::Loaduw: size = 4; isSigned = false; break;
        case ir::Instruction::Loadl:  size = 8; isSigned = true; break;
        default: size = 4; break;
    }
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    if (auto* os = cg.getTextStream()) {
        std::string op = cg.getValueAsOperand(i.getOperands()[0]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[0]->get()) != nullptr;
        if (isGlobal) {
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << (isSigned ? "  movsbq " : "  movzbq ") << op << ", " << rax << "\n";
                else if (size == 2) *os << (isSigned ? "  movswq " : "  movzwq ") << op << ", " << rax << "\n";
                else if (size == 4) *os << (isSigned ? "  movslq " : "  movl ") << op << ", " << eax << "\n";
                else *os << "  movq " << op << ", " << rax << "\n";
            } else {
                if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr " : "  movzx rax, byte ptr ") << op << "\n";
                else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr " : "  movzx rax, word ptr ") << op << "\n";
                else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr " : "  mov eax, dword ptr ") << op << "\n";
                else *os << "  mov rax, " << op << "\n";
            }
        } else {
            *os << "  movq " << op << ", " << rax << "\n";
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << (isSigned ? "  movsbq (%rax), %rax\n" : "  movzbq (%rax), %rax\n");
                else if (size == 2) *os << (isSigned ? "  movswq (%rax), %rax\n" : "  movzwq (%rax), %rax\n");
                else if (size == 4) *os << (isSigned ? "  movslq (%rax), %rax\n" : "  movl (%rax), %eax\n");
                else *os << "  movq (%rax), %rax\n";
            } else {
                if (size == 1) *os << (isSigned ? "  movsx rax, byte ptr [rax]\n" : "  movzx rax, byte ptr [rax]\n");
                else if (size == 2) *os << (isSigned ? "  movsx rax, word ptr [rax]\n" : "  movzx rax, word ptr [rax]\n");
                else if (size == 4) *os << (isSigned ? "  movsxd rax, dword ptr [rax]\n" : "  mov eax, dword ptr [rax]\n");
                else *os << "  mov rax, [rax]\n";
            }
        }
        *os << "  movq " << rax << ", " << cg.getValueAsOperand(&i) << "\n";
    } else {
        auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        if (size == 1) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBE : 0xB6), 0x00});
        else if (size == 2) as.emitBytes({0x48, 0x0F, (uint8_t)(isSigned ? 0xBF : 0xB7), 0x00});
        else if (size == 4) as.emitBytes(isSigned ? std::vector<uint8_t>{0x48, 0x63, 0x00} : std::vector<uint8_t>{0x8B, 0x00});
        else as.emitBytes({0x48, 0x8B, 0x00});
        emitStoreResult(cg, i, 0);
    }
}

void X64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    uint8_t size = 8;
    switch(i.getOpcode()) {
        case ir::Instruction::Storeb: size = 1; break;
        case ir::Instruction::Storeh: size = 2; break;
        case ir::Instruction::Storel: size = 8; break;
        default: size = 4; break;
    }
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    std::string rdx = (abi == X64ABI::SystemV) ? "%rdx" : "rdx";
    std::string al = (abi == X64ABI::SystemV) ? "%al" : "al";
    std::string ax = (abi == X64ABI::SystemV) ? "%ax" : "ax";
    std::string eax = (abi == X64ABI::SystemV) ? "%eax" : "eax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        std::string op = cg.getValueAsOperand(i.getOperands()[1]->get());
        bool isGlobal = dynamic_cast<ir::GlobalValue*>(i.getOperands()[1]->get()) != nullptr;
        if (isGlobal) {
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << "  movb " << al << ", " << op << "\n";
                else if (size == 2) *os << "  movw " << ax << ", " << op << "\n";
                else if (size == 4) *os << "  movl " << eax << ", " << op << "\n";
                else *os << "  movq " << rax << ", " << op << "\n";
            } else {
                if (size == 1) *os << "  mov byte ptr " << op << ", al\n";
                else if (size == 2) *os << "  mov word ptr " << op << ", ax\n";
                else if (size == 4) *os << "  mov dword ptr " << op << ", eax\n";
                else *os << "  mov " << op << ", rax\n";
            }
        } else {
            *os << "  movq " << op << ", " << rdx << "\n";
            if (abi == X64ABI::SystemV) {
                if (size == 1) *os << "  movb " << al << ", (" << rdx << ")\n";
                else if (size == 2) *os << "  movw " << ax << ", (" << rdx << ")\n";
                else if (size == 4) *os << "  movl " << eax << ", (" << rdx << ")\n";
                else *os << "  movq " << rax << ", (" << rdx << ")\n";
            } else {
                if (size == 1) *os << "  mov byte ptr [rdx], al\n";
                else if (size == 2) *os << "  mov word ptr [rdx], ax\n";
                else if (size == 4) *os << "  mov dword ptr [rdx], eax\n";
                else *os << "  mov [rdx], rax\n";
            }
        }
    } else {
        auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 0);
        emitLoadValue(cg, as, i.getOperands()[1]->get(), 2);
        if (size == 1) as.emitBytes({0x88, 0x02});
        else if (size == 2) as.emitBytes({0x66, 0x89, 0x02});
        else if (size == 4) as.emitBytes({0x89, 0x02});
        else as.emitBytes({0x48, 0x89, 0x02});
    }
}

void X64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    int32_t pointerOffset = cg.getStackOffset(&i);
    uint64_t size = 8;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) { if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())) size = sizeConst->getValue(); }
    uint64_t alignedSize = (size + 7) & ~7;
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  # Bump Allocation: " << size << " bytes\n";
        if (abi == X64ABI::SystemV) {
            *os << "  movq heap_ptr(%rip), " << rax << "\n";
            *os << "  movq " << rax << ", " << formatStackOperand(pointerOffset) << "\n";
            *os << "  addq $" << alignedSize << ", " << rax << "\n";
            *os << "  movq " << rax << ", heap_ptr(%rip)\n";
        } else {
            *os << "  mov rax, [rel heap_ptr]\n";
            *os << "  mov " << formatStackOperand(pointerOffset) << ", rax\n";
            *os << "  add rax, " << alignedSize << "\n";
            *os << "  mov [rel heap_ptr], rax\n";
        }
    } else {
        auto& as = cg.getAssembler(); ir::GlobalValue hp_val(cg.module.getContext()->getVoidType(), "heap_ptr"); emitLoadValue(cg, as, &hp_val, 0);
        emitRegMem(as, 0x48, 0x89, 0, pointerOffset); as.emitBytes({0x48, 0x05}); as.emitDWord(alignedSize);
        as.emitBytes({0x48, 0x89, 0x05}); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "heap_ptr", ".text"});
    }
}

void X64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    std::string rax = (abi == X64ABI::SystemV) ? "%rax" : "rax";
    if (auto* os = cg.getTextStream()) {
        *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", " << rax << "\n";
        *os << "  testq " << rax << ", " << rax << "\n";
        *os << "  jne " << cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())) << "\n";
        *os << "  jmp " << cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())) << "\n";
    } else {
        emitLoadValue(cg, cg.getAssembler(), i.getOperands()[0]->get(), 0);
        cg.getAssembler().emitBytes({0x48, 0x85, 0xC0, 0x0F, 0x85});
        uint64_t off1 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get())), ".text"});
        cg.getAssembler().emitByte(0xE9);
        uint64_t off2 = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get())), ".text"});
    }
}

void X64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  jmp " << cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())) << "\n";
    } else {
        cg.getAssembler().emitByte(0xE9);
        uint64_t off = cg.getAssembler().getCodeSize(); cg.getAssembler().emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, cg.getTargetInfo()->getBBLabel(dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get())), ".text"});
    }
}

void X64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (abi == X64ABI::SystemV) {
        auto* si = dynamic_cast<ir::SyscallInstruction*>(&i);
        ir::SyscallId sid = si ? si->getSyscallId() : ir::SyscallId::None;
        if (auto* os = cg.getTextStream()) {
            if (sid != ir::SyscallId::None) *os << "  movq $" << osInfo.getSyscallNumber(sid) << ", %rax\n";
            else *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rax\n";
            size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
            for (size_t j = startArg; j < i.getOperands().size(); ++j) {
                size_t argIdx = (sid != ir::SyscallId::None) ? j + 1 : j; std::string dest;
                switch(argIdx) { case 1: dest = "%rdi"; break; case 2: dest = "%rsi"; break; case 3: dest = "%rdx"; break; case 4: dest = "%r10"; break; case 5: dest = "%r8"; break; case 6: dest = "%r9"; break; }
                if (!dest.empty()) *os << "  movq " << cg.getValueAsOperand(i.getOperands()[j]->get()) << ", " << dest << "\n";
            }
            *os << "  syscall\n"; if (i.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&i) << "\n";
        }
    } else {
        auto* si = dynamic_cast<ir::SyscallInstruction*>(&i);
        if (si && si->getSyscallId() == ir::SyscallId::Exit) {
            if (auto* os = cg.getTextStream()) {
                *os << "  mov rcx, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  call ExitProcess\n";
            } else {
                auto& as = cg.getAssembler(); emitLoadValue(cg, as, i.getOperands()[0]->get(), 1); // rcx
                as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
                cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "ExitProcess", ".text"});
            }
        }
    }
}

void X64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i);
    if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (!spec || !cg.getTargetInfo()->validateCapability(i, *spec)) {
        cg.getTargetInfo()->emitUnsupportedCapability(cg, i, spec);
        return;
    }
    cg.getTargetInfo()->emitDomainCapability(cg, i, *spec);
}

void X64Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    std::string rax = getRegisterName("rax", nullptr);
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $" << syscallNum << ", " << rax << "\n";
        static const char* sysregs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            *os << "  movq " << cg.getValueAsOperand(args[i]) << ", " << getRegisterName(sysregs[i], args[i]->getType()) << "\n";
        }
        *os << "  syscall\n";
    } else {
        auto& as = cg.getAssembler();
        as.emitBytes({0x48, 0xC7, 0xC0}); as.emitDWord(syscallNum);
        static const char* sysregs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            uint8_t r = getArchRegIndex(sysregs[i]);
            emitLoadValue(cg, as, args[i], r);
        }
        as.emitBytes({0x0F, 0x05});
    }
}

void X64Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        if (abi == X64ABI::Windows) {
            *os << "  subq $32, %rsp\n";
            static const char* winRegs[] = {"rcx", "rdx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)4); ++i) {
                *os << "  movq " << cg.getValueAsOperand(args[i]) << ", " << getRegisterName(winRegs[i], args[i]->getType()) << "\n";
            }
            *os << "  call " << name << "\n";
            *os << "  addq $32, %rsp\n";
        } else {
            static const char* sysvRegs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
                *os << "  movq " << cg.getValueAsOperand(args[i]) << ", " << getRegisterName(sysvRegs[i], args[i]->getType()) << "\n";
            }
            *os << "  call " << name << "\n";
        }
    } else {
        auto& as = cg.getAssembler();
        if (abi == X64ABI::Windows) {
            as.emitBytes({0x48, 0x83, 0xEC, 0x20});
            static const char* winRegs[] = {"rcx", "rdx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)4); ++i) {
                emitLoadValue(cg, as, args[i], getArchRegIndex(winRegs[i]));
            }
            as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, name, ".text"});
            as.emitBytes({0x48, 0x83, 0xC4, 0x20});
        } else {
            static const char* sysvRegs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
                emitLoadValue(cg, as, args[i], getArchRegIndex(sysvRegs[i]));
            }
            as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, name, ".text"});
        }
    }
}

std::string X64Architecture::formatStackOperand(int offset) const {
    if (abi == X64ABI::SystemV) return std::to_string(offset) + "(%rbp)";
    return "[rbp + " + std::to_string(offset) + "]";
}

std::string X64Architecture::formatGlobalOperand(const std::string& name) const {
    if (abi == X64ABI::SystemV) return name + "(%rip)";
    return "[rel " + name + "]";
}

bool X64Architecture::isCallerSaved(const std::string& reg) const { return callerSaved.count(reg) && callerSaved.at(reg); }
bool X64Architecture::isCalleeSaved(const std::string& reg) const { return calleeSaved.count(reg) && calleeSaved.at(reg); }
bool X64Architecture::isReserved(const std::string& reg) const {
    return reg == "rsp" || reg == "rbp" || reg == "%rsp" || reg == "%rbp";
}

std::string X64Architecture::getRegisterName(const std::string& base, const ir::Type* type) const {
    if (abi == X64ABI::SystemV) {
        if (base[0] == '%') return base;
        return "%" + base;
    }
    if (base[0] == '%') return base.substr(1);
    return base;
}

// Helpers
void X64Architecture::emitRegMem(asm_::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
    if (rex) as.emitByte(rex);
    as.emitByte(opcode);
    if (offset >= -128 && offset <= 127) { as.emitByte(0x45 | (reg << 3)); as.emitByte((uint8_t)offset); }
    else { as.emitByte(0x85 | (reg << 3)); as.emitDWord(offset); }
}

void X64Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& as, ir::Value* v, uint8_t regIdx) {
    if (!v) { uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(0); return; }
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) { uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(ci->getValue()); }
    else if (v->getName() == "__heap_ptr" || dynamic_cast<ir::GlobalVariable*>(v) || dynamic_cast<ir::GlobalValue*>(v)) {
        uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; as.emitByte(rex); as.emitByte(0x8B); as.emitByte(0x05 | ((regIdx & 7) << 3));
        uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, v->getName(), ".text"});
    } else {
        int32_t offset = cg.getStackOffset(v); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(as, rex, 0x8B, regIdx & 7, offset);
    }
}

void X64Architecture::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx) {
    int32_t offset = cg.getStackOffset(&instr); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(cg.getAssembler(), rex, 0x89, regIdx & 7, offset);
}

uint8_t X64Architecture::getRex(const ir::Type* t) { if (!t || t->isPointerTy()) return 0x48; if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 64) return 0x48; } return 0; }
uint8_t X64Architecture::getOpcode(uint8_t baseOp, const ir::Type* t) { if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 8) return baseOp - 1; } return baseOp; }
uint8_t X64Architecture::getArchRegIndex(const std::string& regName) {
    static std::map<std::string, uint8_t> regToIdx = {{"rax",0}, {"rcx",1}, {"rdx",2}, {"rbx",3}, {"rsp",4}, {"rbp",5}, {"rsi",6}, {"rdi",7}, {"r8",8}, {"r9",9}, {"r10",10}, {"r11",11}, {"r12",12}, {"r13",13}, {"r14",14}, {"r15",15}};
    std::string name = regName; if (name[0] == '%') name = name.substr(1);
    auto it = regToIdx.find(name); return (it != regToIdx.end()) ? it->second : 0;
}

void X64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void X64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
}
