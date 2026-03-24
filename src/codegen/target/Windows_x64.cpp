#include "codegen/target/Windows_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include <ostream>
#include <algorithm>

using namespace ir;
using namespace codegen;
using namespace codegen::target;

namespace {
    std::string getSuffix(const ir::Type* type) {
        if (!type || type->isPointerTy()) return "";
        if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
            if (it->getBitwidth() == 8) return "byte";
            if (it->getBitwidth() == 16) return "word";
            if (it->getBitwidth() == 32) return "dword";
        }
        return "qword";
    }

    std::string getEpilogueLabel(const ir::Function* func) {
        return func ? func->getName() + "_epilogue" : "L_epilogue";
    }
}

namespace codegen {
namespace target {

void Windows_x64::initRegisters() {
    integerRegs = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "rsi", "rdi", "rbx", "r12", "r13", "r14", "r15"};
    floatRegs = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    integerArgRegs = {"rcx", "rdx", "r8", "r9"};
    floatArgRegs = {"xmm0", "xmm1", "xmm2", "xmm3"};
    intReturnReg = "rax";
    floatReturnReg = "xmm0";
    stackPtrReg = "rsp";
    framePtrReg = "rbp";
    callerSaved = { {"rax",true}, {"rcx",true}, {"rdx",true}, {"r8",true}, {"r9",true}, {"r10",true}, {"r11",true} };
    calleeSaved = { {"rbx",true}, {"rsi",true}, {"rdi",true}, {"r12",true}, {"r13",true}, {"r14",true}, {"r15",true}, {"rbp",true} };
}

Windows_x64::Windows_x64() : X86_64Base() { initRegisters(); }

void Windows_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    currentStackOffset = 0;
    for (auto& param : func.getParameters()) {
        currentStackOffset -= 8;
        cg.getStackOffsets()[param.get()] = currentStackOffset;
    }
    int locals_size = func.getStackFrameSize();
    int total_stack = locals_size + (-currentStackOffset);
    // Windows requires 32 bytes shadow space if we call anything
    bool isLeaf = true;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) { isLeaf = false; break; }
        }
        if (!isLeaf) break;
    }
    if (!isLeaf) total_stack += 32;
    if (total_stack % 16 != 0) total_stack += 16 - (total_stack % 16);

    emitPrologue(cg, total_stack);
    
    if (auto* os = cg.getTextStream()) {
        int i = 0;
        for (auto& param : func.getParameters()) {
            if (i < 4) *os << "  mov [rbp + " << cg.getStackOffset(param.get()) << "], " << integerArgRegs[i] << "\n";
            i++;
        }
    }
}

void Windows_x64::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << getEpilogueLabel(&func) << ":\n";
    }
    emitEpilogue(cg);
}

void Windows_x64::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type*) {
    if (auto* os = cg.getTextStream()) {
        if (argIndex < 4) *os << "  mov " << integerArgRegs[argIndex] << ", " << value << "\n";
        else *os << "  mov [rsp + " << (argIndex * 8) << "], " << value << "\n";
    }
}
void Windows_x64::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type*) { (void)cg; (void)argIndex; (void)dest; }

void Windows_x64::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&instr), l = cg.getValueAsOperand(instr.getOperands()[0]->get()), r = cg.getValueAsOperand(instr.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  add " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&instr), l = cg.getValueAsOperand(instr.getOperands()[0]->get()), r = cg.getValueAsOperand(instr.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  sub " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&instr), l = cg.getValueAsOperand(instr.getOperands()[0]->get()), r = cg.getValueAsOperand(instr.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  imul " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&instr);
        *os << "  mov rax, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  cdq\n  idiv " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov " << d << ", rax\n";
    }
}
void Windows_x64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&instr);
        *os << "  mov rax, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  cdq\n  idiv " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov " << d << ", rdx\n";
    }
}
void Windows_x64::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get()), r = cg.getValueAsOperand(i.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  and " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get()), r = cg.getValueAsOperand(i.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  or " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get()), r = cg.getValueAsOperand(i.getOperands()[1]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        *os << "  xor " << d << ", " << r << "\n";
    }
}
void Windows_x64::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) *os << "  shl " << d << ", " << ci->getValue() << "\n";
        else { *os << "  mov cl, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  shl " << d << ", cl\n"; }
    }
}
void Windows_x64::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) *os << "  shr " << d << ", " << ci->getValue() << "\n";
        else { *os << "  mov cl, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  shr " << d << ", cl\n"; }
    }
}
void Windows_x64::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), l = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != l) *os << "  mov " << d << ", " << l << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) *os << "  sar " << d << ", " << ci->getValue() << "\n";
        else { *os << "  mov cl, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  sar " << d << ", cl\n"; }
    }
}
void Windows_x64::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), s = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != s) *os << "  mov " << d << ", " << s << "\n";
        *os << "  neg " << d << "\n";
    }
}
void Windows_x64::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), s = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != s) *os << "  mov " << d << ", " << s << "\n";
        *os << "  not " << d << "\n";
    }
}
void Windows_x64::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), s = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != s) *os << "  mov " << d << ", " << s << "\n";
    }
}
void Windows_x64::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string l = cg.getValueAsOperand(i.getOperands()[0]->get()), r = cg.getValueAsOperand(i.getOperands()[1]->get()), d = cg.getValueAsOperand(&i);
        *os << "  mov rax, " << l << "\n  cmp rax, " << r << "\n";
        std::string set;
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq: set = "sete"; break; case ir::Instruction::Cne: set = "setne"; break;
            case ir::Instruction::Cslt: set = "setl"; break; case ir::Instruction::Csle: set = "setle"; break;
            case ir::Instruction::Csgt: set = "setg"; break; case ir::Instruction::Csge: set = "setge"; break;
            default: set = "sete"; break;
        }
        *os << "  " << set << " al\n  movzx eax, al\n  mov " << d << ", eax\n";
    }
}
void Windows_x64::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), p = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (p.find("[") != std::string::npos || p.find("rax") != std::string::npos || p.find("rcx") != std::string::npos || p.find("rdx") != std::string::npos || p.find("rbp") != std::string::npos || p.find("rsp") != std::string::npos || p.find("rdi") != std::string::npos || p.find("rsi") != std::string::npos || (p.size() >= 2 && p[0] == 'r' && isdigit(p[1]))) {
             *os << "  mov " << d << ", [" << p << "]\n";
        } else {
             *os << "  mov rax, " << p << "\n  mov " << d << ", [rax]\n";
        }
    }
}
void Windows_x64::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string v = cg.getValueAsOperand(i.getOperands()[0]->get()), p = cg.getValueAsOperand(i.getOperands()[1]->get());
        if (p.find("[") != std::string::npos || p.find("rax") != std::string::npos || p.find("rcx") != std::string::npos || p.find("rdx") != std::string::npos || p.find("rbp") != std::string::npos || p.find("rsp") != std::string::npos || p.find("rdi") != std::string::npos || p.find("rsi") != std::string::npos || (p.size() >= 2 && p[0] == 'r' && isdigit(p[1]))) {
            *os << "  mov [" << p << "], " << v << "\n";
        } else {
            *os << "  mov rdx, " << p << "\n  mov [rdx], " << v << "\n";
        }
    }
}
void Windows_x64::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  cmp " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", 0\n  jne " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        if (i.getOperands().size() > 2) *os << "  jmp " << cg.getValueAsOperand(i.getOperands()[2]->get()) << "\n";
    }
}
void Windows_x64::emitJmp(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) *os << "  jmp " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n"; }
void Windows_x64::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) {
            std::string src = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (src != "rax") *os << "  mov rax, " << src << "\n";
        }
        *os << "  jmp " << getEpilogueLabel(i.getParent()->getParent()) << "\n";
        return;
    }
    emitEpilogue(cg);
}
void Windows_x64::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string sizeStr = "8";
        if (!i.getOperands().empty()) {
            ir::Value* sizeVal = i.getOperands()[0]->get();
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(sizeVal)) {
                sizeStr = std::to_string(ci->getValue());
            }
        }
        *os << "  mov rax, [__heap_ptr]\n  mov " << cg.getValueAsOperand(&i) << ", rax\n  add rax, " << sizeStr << "\n  mov [__heap_ptr], rax\n";
    }
}
void Windows_x64::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < std::min(i.getOperands().size(), (size_t)5); ++j)
            *os << "  mov " << integerArgRegs[j-1] << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        if (!i.getType()->isVoidTy()) *os << "  mov " << cg.getValueAsOperand(&i) << ", rax\n";
    }
}

void Windows_x64::emitVAStart(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitVAArg(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitFAdd(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitFSub(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitFMul(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitFDiv(CodeGen&, ir::Instruction&) {}
void Windows_x64::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type*, const ir::Type*) {
    if (auto* os = cg.getTextStream()) {
        std::string d = cg.getValueAsOperand(&i), src = cg.getValueAsOperand(i.getOperands()[0]->get());
        if (d != src) *os << "  mov " << d << ", " << src << "\n";
    }
}

}
}
