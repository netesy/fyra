#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Value.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include <iostream>

namespace ir {

Instruction::Instruction(Type* ty, Opcode op, const std::vector<Value*>& operands, BasicBlock* parent)
    : User(ty), opcode(op), parent(parent) {
    for (Value* v : operands) {
        addOperand(v);
    }
}

static void printValue(std::ostream& os, Value* v) {
    if (!v) {
        os << "null";
        return;
    }
    if (auto* ci = dynamic_cast<ConstantInt*>(v)) {
        os << ci->getValue();
    } else if (auto* cf = dynamic_cast<ConstantFP*>(v)) {
        os << cf->getValue();
    } else if (auto* cs = dynamic_cast<ConstantString*>(v)) {
        os << "\"" << cs->getValue() << "\"";
    } else if (v->getName().empty()) {
        os << "<unnamed>";
    } else {
        os << "%" << v->getName();
    }
}

void Instruction::print(std::ostream& os) const {
    if (!getName().empty()) {
        os << "%" << getName() << " = ";
    }

    switch (opcode) {
        case Add: os << "add"; break;
        case Sub: os << "sub"; break;
        case Mul: os << "mul"; break;
        case Div: os << "div"; break;
        case Udiv: os << "udiv"; break;
        case Rem: os << "rem"; break;
        case Urem: os << "urem"; break;
        case And: os << "and"; break;
        case Or: os << "or"; break;
        case Xor: os << "xor"; break;
        case Shl: os << "shl"; break;
        case Shr: os << "shr"; break;
        case Sar: os << "sar"; break;
        case FAdd: os << "fadd"; break;
        case FSub: os << "fsub"; break;
        case FMul: os << "fmul"; break;
        case FDiv: os << "fdiv"; break;
        case FRem: os << "frem"; break;
        case Neg: os << "neg"; break;
        case Not: os << "not"; break;
        case Ret: os << "ret"; break;
        case Jmp: os << "jmp"; break;
        case Jz: os << "jz"; break;
        case Jnz: os << "jnz"; break;
        case Br: os << "br"; break;
        case Hlt: os << "hlt"; break;
        case Alloc: os << "alloc"; break;
        case Alloc4: os << "alloc4"; break;
        case Alloc16: os << "alloc16"; break;
        case Load: os << "load"; break;
        case Loadd: os << "loadd"; break;
        case Loads: os << "loads"; break;
        case Loadl: os << "loadl"; break;
        case Loaduw: os << "loaduw"; break;
        case Loadsh: os << "loadsh"; break;
        case Loaduh: os << "loaduh"; break;
        case Loadsb: os << "loadsb"; break;
        case Loadub: os << "loadub"; break;
        case Store: os << "store"; break;
        case Stored: os << "stored"; break;
        case Stores: os << "stores"; break;
        case Storel: os << "storel"; break;
        case Storeh: os << "storeh"; break;
        case Storeb: os << "storeb"; break;
        case Call: os << "call"; break;
        case Phi: os << "phi"; break;
        case Copy: os << "copy"; break;
        case Ceq: os << "ceq"; break;
        case Cne: os << "cne"; break;
        case Csle: os << "csle"; break;
        case Cslt: os << "cslt"; break;
        case Csge: os << "csge"; break;
        case Csgt: os << "csgt"; break;
        case Cule: os << "cule"; break;
        case Cult: os << "cult"; break;
        case Cuge: os << "cuge"; break;
        case Cugt: os << "cugt"; break;
        case Ceqf: os << "ceqf"; break;
        case Cnef: os << "cnef"; break;
        case Cle: os << "cle"; break;
        case Clt: os << "clt"; break;
        case Cge: os << "cge"; break;
        case Cgt: os << "cgt"; break;
        case Co: os << "co"; break;
        case Cuo: os << "cuo"; break;
        case Blit: os << "blit"; break;
        case ExtUB: os << "extub"; break;
        case ExtUH: os << "extuh"; break;
        case ExtUW: os << "extuw"; break;
        case ExtSB: os << "extsb"; break;
        case ExtSH: os << "extsh"; break;
        case ExtSW: os << "extsw"; break;
        case ExtS: os << "exts"; break;
        case TruncD: os << "truncd"; break;
        case SWtoF: os << "swtof"; break;
        case UWtoF: os << "uwtof"; break;
        case DToSI: os << "dtosi"; break;
        case DToUI: os << "dtoui"; break;
        case SToSI: os << "stosi"; break;
        case SToUI: os << "stoui"; break;
        case Sltof: os << "sltof"; break;
        case Ultof: os << "ultof"; break;
        case Cast: os << "cast"; break;
        case VAStart: os << "vastart"; break;
        case VAArg: os << "vaarg"; break;
        case Syscall: os << "syscall"; break;
        default: os << "v_op"; break;
    }

    for (const auto& operand : getOperands()) {
        os << " ";
        if (operand) printValue(os, operand->get());
        else os << "null_use";
    }
}

SyscallInstruction::SyscallInstruction(Type* ty, const std::vector<Value*>& operands, SyscallId id, BasicBlock* parent)
    : Instruction(ty, Syscall, operands, parent), id(id) {}

void SyscallInstruction::print(std::ostream& os) const {
    if (!getName().empty()) {
        os << "%" << getName() << " = ";
    }
    os << "syscall";
    os << "(";
    if (id != SyscallId::None) {
        os << syscallIdToString(id);
        if (getOperands().size() > 0) os << ", ";
    }
    for (size_t i = 0; i < getOperands().size(); ++i) {
        if (getOperands()[i]) printValue(os, getOperands()[i]->get());
        else os << "null_use";
        if (i < getOperands().size() - 1) os << ", ";
    }
    os << ")";
}

} // namespace ir
