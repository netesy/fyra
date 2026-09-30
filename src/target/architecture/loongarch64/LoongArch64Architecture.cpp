#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "target/architecture/loongarch64/LoongArch64Architecture.h"
#include "codegen/CodeGen.h"
#include "codegen/asm/Assembler.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>
#include <vector>
#include <set>

namespace target {

LoongArch64Architecture::LoongArch64Architecture() {}

void LoongArch64Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& as, ir::Value* val, uint8_t reg) {
    if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
        if (instr->hasPhysicalRegister()) {
            uint8_t src = static_cast<uint8_t>(instr->getPhysicalRegister());
            if (src == reg) return;
            // move reg, src
            uint32_t opcode = 0x00000000 | (src << 5) | (reg << 0);
            as.emitDWord(opcode);
            return;
        }
    }
    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        int64_t v = static_cast<int64_t>(constInt->getValue());
        if (v >= -2048 && v <= 2047) {
            // addi.w reg, $r0, v
            uint32_t opcode = 0x02800000 | ((v & 0x7FF) << 10) | (reg << 0);
            as.emitDWord(opcode);
        } else {
            int32_t imm32 = static_cast<int32_t>(v);
            int32_t hi = (imm32 + 0x800) >> 12;
            int32_t lo = imm32 - (hi << 12);
            as.emitDWord(0x14000000 | ((hi & 0xFFFF) << 10) | (reg << 0)); // lu12i.w reg, hi
            as.emitDWord(0x02800000 | ((lo & 0xFFF) << 10) | (reg << 5) | (reg << 0)); // ori reg, reg, lo
        }
    } else {
        int32_t offset = cg.getStackOffset(val);
        uint32_t funct = (val->getType() && val->getType()->getSize() <= 4) ? 0xA : 0xC;
        as.emitDWord(0x2C000000 | ((offset & 0xFFF) << 10) | (3 << 5) | (reg << 0)); // ld.w/d reg, $r3, offset
    }
}

void LoongArch64Architecture::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t reg) {
    if (instr.hasPhysicalRegister()) {
        uint8_t dst = static_cast<uint8_t>(instr.getPhysicalRegister());
        if (dst != reg) {
            cg.getAssembler().emitDWord(0x00000000 | (reg << 5) | (dst << 0)); // move dst, reg
        }
        return;
    }
    int32_t offset = cg.getStackOffset(&instr);
    uint32_t funct = (instr.getType() && instr.getType()->getSize() <= 4) ? 0xA : 0xC;
    uint32_t opcode = 0x29000000 | ((offset & 0xFFF) << 10) | (reg << 5) | (3 << 0); // st.w/d reg, $r3, offset
    cg.getAssembler().emitDWord(opcode);
}

TypeInfo LoongArch64Architecture::getTypeInfo(const ir::Type* type) const {
    return {type->getSize() * 8, type->getAlignment() * 8, type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer, type->isFloatingPoint(), true};
}

const std::vector<std::string>& LoongArch64Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"$r4", "$r5", "$r6", "$r7", "$r8", "$r9", "$r10", "$r11", "$r20", "$r21", "$r22", "$r23", "$r24", "$r25", "$r26", "$r27"};
    static const std::vector<std::string> floatRegs = {"$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8", "$f9", "$f10", "$f11", "$f12", "$f13", "$f14", "$f15"};
    static const std::vector<std::string> vecRegs = {"$v0", "$v1", "$v2", "$v3", "$v4", "$v5", "$v6", "$v7", "$v8", "$v9", "$v10", "$v11", "$v12", "$v13", "$v14", "$v15"};
    return (regClass == RegisterClass::Float) ? floatRegs : (regClass == RegisterClass::Vector ? vecRegs : intRegs);
}

const std::string& LoongArch64Architecture::getReturnRegister(const ir::Type* type) const {
    static const std::string i = "$r4", f = "$f0"; return type->isFloatingPoint() ? f : i;
}
const std::vector<std::string>& LoongArch64Architecture::getIntegerArgumentRegisters() const { static const std::vector<std::string> r = {"$r4", "$r5", "$r6", "$r7", "$r8", "$r9", "$r10", "$r11"}; return r; }
const std::vector<std::string>& LoongArch64Architecture::getFloatArgumentRegisters() const { static const std::vector<std::string> r = {"$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7"}; return r; }
const std::string& LoongArch64Architecture::getIntegerReturnRegister() const { static const std::string r = "$r4"; return r; }
const std::string& LoongArch64Architecture::getFloatReturnRegister() const { static const std::string r = "$f0"; return r; }

void LoongArch64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    currentStackOffset = -16; int i_idx = 0, f_idx = 0, s_arg_idx = 0;
    for (auto& param : func.getParameters()) {
        TypeInfo inf = getTypeInfo(param->getType());
        if (inf.regClass == RegisterClass::Float) { if (f_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; f_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 8; }
        else { if (i_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; i_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 8; }
    }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { currentStackOffset -= 8; cg.getStackOffsets()[instr.get()] = currentStackOffset; } } }
    int stack_size = (-currentStackOffset + 15) & ~15;
    if (auto* os = cg.getTextStream()) {
        *os << "  addi.d $sp, $sp, -" << stack_size << "\n  st.d $r1, $sp, " << stack_size - 8 << "\n  st.d $r22, $sp, " << stack_size - 16 << "\n  addi.d $r22, $sp, " << stack_size << "\n";
    } else {
        cg.getAssembler().emitDWord(0x02C00000 | ((-stack_size & 0xFFF) << 10) | (3 << 5) | (3 << 0)); // addi.d $sp, $sp, -stack_size
        uint32_t off1 = (stack_size - 8) & 0xFFF;
        cg.getAssembler().emitDWord(0x29000000 | (off1 << 10) | (1 << 5) | (3 << 0)); // st.d $r1, $sp, off1
        uint32_t off2 = (stack_size - 16) & 0xFFF;
        cg.getAssembler().emitDWord(0x29000000 | (off2 << 10) | (22 << 5) | (3 << 0)); // st.d $r22, $sp, off2
        cg.getAssembler().emitDWord(0x02C00000 | (stack_size << 10) | (3 << 5) | (22 << 0)); // addi.d $r22, $sp, stack_size
        int arg_i = 0;
        for (auto& param : func.getParameters()) {
            if (arg_i < 8) {
                int32_t off = cg.getStackOffset(param.get());
                uint32_t funct = (param->getType()->getSize() <= 4) ? 0xA : 0xC;
                uint32_t imm = off & 0xFFF;
                cg.getAssembler().emitDWord(0x29000000 | (imm << 10) | ((4 + arg_i) << 5) | (3 << 0));
                arg_i++;
            }
        }
    }
}

void LoongArch64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << func.getName() << "_epilogue:\n  ld.d $r1, $r22, -8\n  ld.d $r22, $r22, -16\n  addi.d $sp, $r22, 0\n  jirl $r0, $r1, 0\n";
    } else {
        cg.getAssembler().emitDWord(0x2C000000 | ((-8 & 0xFFF) << 10) | (22 << 5) | (1 << 0)); // ld.d $r1, $r22, -8
        cg.getAssembler().emitDWord(0x2C000000 | ((-16 & 0xFFF) << 10) | (22 << 5) | (22 << 0)); // ld.d $r22, $r22, -16
        cg.getAssembler().emitDWord(0x02C00000 | (0 << 10) | (22 << 5) | (3 << 0)); // addi.d $sp, $r22, 0
        cg.getAssembler().emitDWord(0x4C000000 | (0 << 10) | (0 << 5) | (1 << 0)); // jirl $r0, $r1, 0
    }
}

void LoongArch64Architecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".text\n.globl _start\n_start:\n  bl main\n  li.w $r11, 93\n  syscall 0\n";
    } else {
        cg.addRelocation({cg.getAssembler().getCodeSize(), "R_LARCH_CALL", 0, "main", ".text"});
        cg.getAssembler().emitDWord(0x48000000); // bl 0
        cg.getAssembler().emitDWord(0x14000000 | ((93 & 0xFFFF) << 10) | (11 << 0)); // li.w $r11, 93
        cg.getAssembler().emitDWord(0x00000000); // syscall 0
    }
}

void LoongArch64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            if (rv->getType() && rv->getType()->isFloatingPoint()) {
                std::string flInst = (rv->getType()->getSize() == 4) ? "fld.s" : "fld.d";
                *os << "  " << flInst << " $f0, " << cg.getValueAsOperand(rv) << "\n";
            } else {
                std::string lInst = (rv->getType() && rv->getType()->getSize() <= 4) ? "ld.w" : "ld.d";
                *os << "  " << lInst << " $r4, " << cg.getValueAsOperand(rv) << "\n";
            }
        } else {
            emitLoadValue(cg, cg.getAssembler(), rv, 4); // $r4
        }
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  b " << i.getParent()->getParent()->getName() << "_epilogue\n";
    } else {
        emitFunctionEpilogue(cg, *i.getParent()->getParent());
    }
}

void LoongArch64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        std::string reg = "$r" + std::to_string(4 + argIndex);
        *os << "  ld.d " << reg << ", " << value << "\n";
    }
}

void LoongArch64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        std::string reg = "$r" + std::to_string(4 + argIndex);
        *os << "  st.d " << reg << ", " << dest << "\n";
    }
}

void LoongArch64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  addi.d $r4, $r4, " << val << "\n";
                *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  add.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitSMin(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  min.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitSMax(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  max.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (-val >= -2048 && -val <= 2047) {
                *os << "  addi.d $r4, $r4, " << -val << "\n";
                *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sub.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  mul.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Udiv);
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "div.du " : "div.d ") << "$r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Urem);
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "mod.du " : "mod.d ") << "$r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  andi $r4, $r4, " << val << "\n";
                *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  and $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  ori $r4, $r4, " << val << "\n";
                *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  or $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  xori $r4, $r4, " << val << "\n";
                *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  xor $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  slli.d $r4, $r4, " << (ci->getValue() & 63) << "\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sll.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srli.d $r4, $r4, " << (ci->getValue() & 63) << "\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  srl.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srai.d $r4, $r4, " << (ci->getValue() & 63) << "\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sra.d $r4, $r4, $r5\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  sub.d $r4, $r0, $r4\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  nor $r4, $r4, $r0\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < std::min(i.getOperands().size(), (size_t)9); ++j) {
            *os << "  ld.d $r" << (j - 1 + 4) << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        }
        ir::Value* calleeVal = (!i.getOperands().empty() && i.getOperands()[0]) ? i.getOperands()[0]->get() : nullptr;
        bool isDirect = calleeVal && (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                                     (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  bl " << calleeVal->getName() << "\n";
        } else if (calleeVal) {
            *os << "  ld.d $r12, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jirl $r1, $r12, 0\n";
        } else {
            *os << "  bl unk\n";
        }
        if (i.getType() && !i.getType()->isVoidTy()) {
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

bool LoongArch64Architecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) {
    ir::Value* calleeVal = callInst.getOperands()[0]->get();
    if (!calleeVal) return false;

    if (callInst.getType()->getTypeID() != ir::Type::VoidTyID) {
        if (retInst.getOperands().empty() || retInst.getOperands()[0]->get() != &callInst) {
            return false;
        }
    } else {
        if (!retInst.getOperands().empty()) return false;
    }

    size_t numArgs = callInst.getOperands().size() - 1;
    if (numArgs > 8) return false;

    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < callInst.getOperands().size(); ++j) {
            *os << "  ld.d $r" << (j - 1 + 4) << ", " << cg.getValueAsOperand(callInst.getOperands()[j]->get()) << "\n";
        }
        bool isDirect = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                        (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  b " << calleeVal->getName() << "\n";
        } else {
            *os << "  ld.d $r12, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jirl $r0, $r12, 0\n";
        }
        return true;
    }
    return false;
}

void LoongArch64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fadd." << (isFloat ? "s" : "d") << " $f0, $f0, $f1\n";
        *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fsub." << (isFloat ? "s" : "d") << " $f0, $f0, $f1\n";
        *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fmul." << (isFloat ? "s" : "d") << " $f0, $f0, $f1\n";
        *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fdiv." << (isFloat ? "s" : "d") << " $f0, $f0, $f1\n";
        *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq: *os << "  slt.d $r4, $r4, $r5\n  slt.d $r5, $r5, $r4\n  or $r4, $r4, $r5\n  xori $r4, $r4, 1\n"; break;
            case ir::Instruction::Cne: *os << "  slt.d $r4, $r4, $r5\n  slt.d $r5, $r5, $r4\n  or $r4, $r4, $r5\n"; break;
            case ir::Instruction::Cslt: *os << "  slt.d $r4, $r4, $r5\n"; break;
            case ir::Instruction::Csle: *os << "  slt.d $r4, $r5, $r4\n  xori $r4, $r4, 1\n"; break;
            case ir::Instruction::Csgt: *os << "  slt.d $r4, $r5, $r4\n"; break;
            case ir::Instruction::Csge: *os << "  slt.d $r4, $r4, $r5\n  xori $r4, $r4, 1\n"; break;
            default: *os << "  slt.d $r4, $r4, $r5\n  slt.d $r5, $r5, $r4\n  or $r4, $r4, $r5\n  xori $r4, $r4, 1\n"; break;
        }
        *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void LoongArch64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = i.getOperands()[0]->get();
        ir::Instruction::Opcode op = i.getOpcode();

        if (op == ir::Instruction::ExtUB) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi $r4, $r4, 255\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi $r4, $r4, 65535\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli.d $r4, $r4, 32\n";
            *os << "  srli.d $r4, $r4, 32\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli.d $r4, $r4, 56\n";
            *os << "  srai.d $r4, $r4, 56\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli.d $r4, $r4, 48\n";
            *os << "  srai.d $r4, $r4, 48\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            *os << "  ld.w $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  ext.w $r4, $r4\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  ld.d $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  movgr2fr." << (isFloat ? "s" : "d") << " $f0, $r4\n";
            *os << "  ffint." << (isFloat ? "s" : "d") << ".lu $f0, $f0\n";
            *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::SWtoF || op == ir::Instruction::Sltof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  ld.d $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  movgr2fr." << (isFloat ? "s" : "d") << " $f0, $r4\n";
            *os << "  ffint." << (isFloat ? "s" : "d") << ".l $f0, $f0\n";
            *os << "  fs" << (isFloat ? "t.s" : "t.d") << " $f0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::DToUI || op == ir::Instruction::SToUI) {
            bool isFloat = (f && f->getSize() == 4);
            *os << "  fl" << (isFloat ? "d.s" : "d.d") << " $f0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  ftint." << (isFloat ? "s" : "d") << ".lu $f0, $f0\n";
            *os << "  movfr2gr.d $r4, $f0\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::TruncD) {
            *os << "  ld.d $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  ext.w $r4, $r4\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ld.d $r4, " << cg.getValueAsOperand(src) << "\n";
            *os << "  st.d $r4, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void LoongArch64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void LoongArch64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}

LoongArch64ComplexAddress LoongArch64Architecture::matchComplexAddress(CodeGen& cg, ir::Value* val) const {
    LoongArch64ComplexAddress result;
    if (!val) return result;

    auto* inst = dynamic_cast<ir::Instruction*>(val);
    if (!inst) return result;

    if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();
        ir::Value* baseVal = nullptr;
        int64_t disp = 0;
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
            disp = static_cast<int64_t>(c1->getValue());
            baseVal = op0;
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
            disp = static_cast<int64_t>(c0->getValue());
            baseVal = op1;
        }
        if (baseVal && disp >= -2048 && disp <= 2047) {
            std::string bStr = cg.getValueAsOperand(baseVal);
            if (!bStr.empty() && (bStr[0] == '$') && bStr.find('(') == std::string::npos) {
                result.base = bStr;
                result.disp = disp;
                result.isValid = true;
                return result;
            }
        }
    }
    return result;
}

void LoongArch64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatingPoint();
        std::string loadMnemonic = "ld.d";
        size_t size = i.getType() ? i.getType()->getSize() : 8;
        if (isFloat) {
            loadMnemonic = (size == 4) ? "fld.s" : "fld.d";
        } else {
            if (size == 1) loadMnemonic = "ld.b";
            else if (size == 2) loadMnemonic = "ld.h";
            else if (size == 4) loadMnemonic = "ld.w";
        }

        ir::Value* ptrVal = i.getOperands()[0]->get();
        LoongArch64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        std::string regName = isFloat ? "$f1" : "$r5";
        std::string storeBackInst = isFloat ? ((size == 4) ? "fst.s" : "fst.d") : "st.d";

        if (addr.isValid) {
            *os << "  " << loadMnemonic << " " << regName << ", " << addr.format() << "\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ld.d $r4, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadMnemonic << " " << regName << ", $r4, 0\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void LoongArch64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* val = i.getOperands()[0]->get();
        bool isFloat = val->getType() && val->getType()->isFloatingPoint();
        std::string storeMnemonic = "st.d";
        size_t size = val->getType() ? val->getType()->getSize() : 8;
        if (isFloat) {
            storeMnemonic = (size == 4) ? "fst.s" : "fst.d";
        } else {
            if (size == 1) storeMnemonic = "st.b";
            else if (size == 2) storeMnemonic = "st.h";
            else if (size == 4) storeMnemonic = "st.w";
        }

        std::string regName = isFloat ? "$f1" : "$r5";
        std::string loadValInst = isFloat ? ((size == 4) ? "fld.s" : "fld.d") : "ld.d";

        ir::Value* ptrVal = i.getOperands()[1]->get();
        LoongArch64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        if (addr.isValid) {
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", " << addr.format() << "\n";
        } else {
            *os << "  ld.d $r4, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", $r4, 0\n";
        }
    }
}

void LoongArch64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    uint64_t size = 8;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) {
        if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get()))
            size = sizeConst->getValue();
    }
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # LoongArch64 Bump Allocation\n";
        *os << "  la $r4, heap_ptr\n";
        *os << "  ld.d $r5, $r4, 0\n";
        *os << "  st.d $r5, " << cg.getValueAsOperand(&i) << "\n";
        *os << "  addi.d $r5, $r5, " << alignedSize << "\n";
        *os << "  st.d $r5, $r4, 0\n";
    }
}

void LoongArch64Architecture::emitPhiCopies(CodeGen& cg, ir::BasicBlock* source, ir::BasicBlock* target) {
    if (!target) return;
    std::vector<std::pair<ir::Value*, ir::PhiNode*>> phiMoves;
    for (auto& instr : target->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
            ir::Value* incomingVal = phi->getIncomingValueForBlock(source);
            if (incomingVal) {
                phiMoves.push_back({incomingVal, phi});
            }
        }
    }
    if (phiMoves.empty()) return;

    auto* os = cg.getTextStream();
    if (!os) return;

    bool canDirectMove = true;
    if (phiMoves.size() > 1) {
        for (size_t i = 0; i < phiMoves.size(); ++i) {
            std::string dest_i = cg.getValueAsOperand(phiMoves[i].second);
            for (size_t j = i + 1; j < phiMoves.size(); ++j) {
                std::string src_j = cg.getValueAsOperand(phiMoves[j].first);
                if (dest_i == src_j) {
                    canDirectMove = false;
                    break;
                }
            }
            if (!canDirectMove) break;
        }
    }

    if (canDirectMove) {
        for (const auto& move : phiMoves) {
            ir::Value* incomingVal = move.first;
            ir::PhiNode* phi = move.second;
            std::string srcOp = cg.getValueAsOperand(incomingVal);
            std::string destOp = cg.getValueAsOperand(phi);
            if (srcOp == destOp) continue;

            const ir::Type* type = phi->getType();
            if (type && type->isFloatingPoint()) {
                std::string flInst = (type->getSize() == 4) ? "fld.s" : "fld.d";
                std::string fsInst = (type->getSize() == 4) ? "fst.s" : "fst.d";
                *os << "  " << flInst << " $f2, " << srcOp << "\n";
                *os << "  " << fsInst << " $f2, " << destOp << "\n";
            } else {
                std::string lInst = (type && type->getSize() <= 4) ? "ld.w" : "ld.d";
                std::string sInst = (type && type->getSize() <= 4) ? "st.w" : "st.d";
                *os << "  " << lInst << " $r12, " << srcOp << "\n";
                *os << "  " << sInst << " $r12, " << destOp << "\n";
            }
        }
        return;
    }

    for (const auto& move : phiMoves) {
        ir::Value* incomingVal = move.first;
        std::string srcOp = cg.getValueAsOperand(incomingVal);
        const ir::Type* type = incomingVal->getType();
        if (type && type->isFloatingPoint()) {
            std::string flInst = (type->getSize() == 4) ? "fld.s" : "fld.d";
            *os << "  " << flInst << " $f2, " << srcOp << "\n";
            *os << "  addi.d $sp, $sp, -16\n";
            *os << "  fst.d $f2, $sp, 0\n";
        } else {
            std::string lInst = (type && type->getSize() <= 4) ? "ld.w" : "ld.d";
            *os << "  " << lInst << " $r12, " << srcOp << "\n";
            *os << "  addi.d $sp, $sp, -16\n";
            *os << "  st.d $r12, $sp, 0\n";
        }
    }

    for (auto it = phiMoves.rbegin(); it != phiMoves.rend(); ++it) {
        ir::PhiNode* phi = it->second;
        std::string destOp = cg.getValueAsOperand(phi);
        const ir::Type* type = phi->getType();
        if (type && type->isFloatingPoint()) {
            std::string fsInst = (type->getSize() == 4) ? "fst.s" : "fst.d";
            *os << "  fld.d $f2, $sp, 0\n";
            *os << "  addi.d $sp, $sp, 16\n";
            *os << "  " << fsInst << " $f2, " << destOp << "\n";
        } else {
            std::string sInst = (type && type->getSize() <= 4) ? "st.w" : "st.d";
            *os << "  ld.d $r12, $sp, 0\n";
            *os << "  addi.d $sp, $sp, 16\n";
            *os << "  " << sInst << " $r12, " << destOp << "\n";
        }
    }
}

void LoongArch64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (i.getOperands().size() == 1) {
        auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
        emitPhiCopies(cg, i.getParent(), targetBB);
        if (auto* os = cg.getTextStream()) {
            *os << "  b " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
        }
        return;
    }

    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get());

    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";

        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        bool trueHasPhis = false;
        if (targetTrue) {
            for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        }
        bool falseHasPhis = false;
        if (targetFalse) {
            for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;
        }

        if (trueHasPhis || falseHasPhis) {
            std::string labelTrueCopies = ".L_true_copies_" + std::to_string((uintptr_t)&i);
            std::string labelFalseCopies = ".L_false_copies_" + std::to_string((uintptr_t)&i);

            *os << "  bnez $r4, " << labelTrueCopies << "\n";
            *os << "  b " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetTrue);
            *os << "  b " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetFalse);
            *os << "  b " << falseLabel << "\n";
        } else {
            *os << "  bnez $r4, " << trueLabel << "\n";
            *os << "  b " << falseLabel << "\n";
        }
    }
}

void LoongArch64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
    emitPhiCopies(cg, i.getParent(), targetBB);
    if (auto* os = cg.getTextStream()) {
        *os << "  b " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
    }
}

bool LoongArch64Architecture::emitMulAddFusion(CodeGen& cg, ir::Instruction& mul, ir::Instruction& add) {
    if (mul.getUseList().size() != 1) return false;
    if (!mul.getType() || !mul.getType()->isInteger()) return false;
    if (!add.getType() || !add.getType()->isInteger()) return false;

    ir::Value* m0 = mul.getOperands()[0]->get();
    ir::Value* m1 = mul.getOperands()[1]->get();
    ir::Value* a0 = add.getOperands()[0]->get();
    ir::Value* a1 = add.getOperands()[1]->get();

    if (a0 != &mul && a1 != &mul) return false;

    ir::Value* addOther = (a0 == &mul) ? a1 : a0;

    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(m0) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(m1) << "\n";
        *os << "  ld.d $r6, " << cg.getValueAsOperand(addOther) << "\n";
        *os << "  mul.d $r4, $r4, $r5\n";
        *os << "  add.d $r4, $r4, $r6\n";
        *os << "  st.d $r4, " << cg.getValueAsOperand(&add) << "\n";
        return true;
    }
    return false;
}

bool LoongArch64Architecture::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) {
    if (br.getOperands().size() < 3) return false;
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(br.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(br.getOperands()[2]->get());
    if (!targetTrue || !targetFalse) return false;

    ir::Value* l = cmp.getOperands()[0]->get();
    ir::Value* r = cmp.getOperands()[1]->get();

    std::string bOp = "beq";
    bool swapOps = false;
    bool isFloatCmp = (l->getType() && l->getType()->isFloatingPoint());

    if (isFloatCmp) {
        return false; // Float compare fusion not implemented
    }

    if (auto* os = cg.getTextStream()) {
        *os << "  ld.d $r4, " << cg.getValueAsOperand(l) << "\n";
        *os << "  ld.d $r5, " << cg.getValueAsOperand(r) << "\n";

        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        switch (cmp.getOpcode()) {
            case ir::Instruction::Ceq: bOp = "beq"; break;
            case ir::Instruction::Cne: bOp = "bne"; break;
            case ir::Instruction::Cslt: bOp = "blt"; break;
            case ir::Instruction::Csle: bOp = "ble"; break;
            case ir::Instruction::Csgt: bOp = "bgt"; break;
            case ir::Instruction::Csge: bOp = "bge"; break;
            default: bOp = "beq"; break;
        }

        *os << "  " << bOp << " $r4, $r5, " << trueLabel << "\n";
        *os << "  b " << falseLabel << "\n";
        return true;
    }
    return false;
}

void LoongArch64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # LoongArch64 syscall\n";
        *os << "  syscall 0\n";
    }
}

void LoongArch64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    emitCall(cg, i);
}

void LoongArch64Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 0; j < std::min(args.size(), (size_t)7); ++j) {
            *os << "  ld.d $r" << (j + 4) << ", " << cg.getValueAsOperand(args[j]) << "\n";
        }
        *os << "  li.w $r11, " << syscallNum << "\n";
        *os << "  syscall 0\n";
    }
}

void LoongArch64Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 0; j < std::min(args.size(), (size_t)8); ++j) {
            *os << "  ld.d $r" << (j + 4) << ", " << cg.getValueAsOperand(args[j]) << "\n";
        }
        *os << "  bl " << name << "\n";
    }
}

std::string LoongArch64Architecture::formatStackOperand(int offset) const {
    return std::to_string(offset) + "($sp)";
}

std::string LoongArch64Architecture::formatGlobalOperand(const std::string& name) const {
    return name;
}

bool LoongArch64Architecture::isCallerSaved(const std::string& reg) const {
    static const std::set<std::string> callerSaved = {"$r4", "$r5", "$r6", "$r7", "$r8", "$r9", "$r10", "$r11", "$r12", "$r13", "$r14", "$r15", "$r16", "$r17", "$r18", "$r19", "$r20", "$r21"};
    return callerSaved.count(reg) > 0;
}

bool LoongArch64Architecture::isCalleeSaved(const std::string& reg) const {
    static const std::set<std::string> calleeSaved = {"$r22", "$r23", "$r24", "$r25", "$r26", "$r27", "$r28", "$r29", "$r30", "$r31"};
    return calleeSaved.count(reg) > 0;
}

VectorCapabilities LoongArch64Architecture::getVectorCapabilities() const {
    return VectorCapabilities();
}

bool LoongArch64Architecture::supportsVectorWidth(unsigned width) const {
    return false;
}

bool LoongArch64Architecture::supportsVectorType(const ir::VectorType* type) const {
    return false;
}

bool LoongArch64Architecture::supportsVectorOperation(ir::Instruction::Opcode op, const ir::VectorType* type) const {
    return false;
}

bool LoongArch64Architecture::supportsVectorConversion(ir::Instruction::Opcode op, const ir::VectorType* srcType, const ir::VectorType* dstType) const {
    return false;
}

void LoongArch64Architecture::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {}
void LoongArch64Architecture::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {}
void LoongArch64Architecture::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {}
void LoongArch64Architecture::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) {}
void LoongArch64Architecture::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) {}

}
