#include "codegen/target/X86_64Base.h"
#include "codegen/CodeGen.h"
#include "codegen/execgen/Assembler.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
using namespace ir;
using namespace codegen;
using namespace codegen::target;
X86_64Base::X86_64Base() {
    regToIdx = {{"rax",0}, {"rcx",1}, {"rdx",2}, {"rbx",3}, {"rsp",4}, {"rbp",5}, {"rsi",6}, {"rdi",7},
                {"r8",8}, {"r9",9}, {"r10",10}, {"r11",11}, {"r12",12}, {"r13",13}, {"r14",14}, {"r15",15},
                {"%rax",0}, {"%rcx",1}, {"%rdx",2}, {"%rbx",3}, {"%rsp",4}, {"%rbp",5}, {"%rsi",6}, {"%rdi",7},
                {"%r8",8}, {"%r9",9}, {"%r10",10}, {"%r11",11}, {"%r12",12}, {"%r13",13}, {"%r14",14}, {"%r15",15}};
}
TypeInfo X86_64Base::getTypeInfo(const ir::Type* type) const {
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
const std::vector<std::string>& X86_64Base::getRegisters(RegisterClass regClass) const {
    if (regClass == RegisterClass::Integer) return integerRegs;
    if (regClass == RegisterClass::Float) return floatRegs;
    return vectorRegs;
}
const std::string& X86_64Base::getReturnRegister(const ir::Type* type) const {
    if (type && (type->isFloatTy() || type->isDoubleTy())) return floatReturnReg;
    return intReturnReg;
}
bool X86_64Base::isCallerSaved(const std::string& reg) const { return callerSaved.count(reg) && callerSaved.at(reg); }
bool X86_64Base::isCalleeSaved(const std::string& reg) const { return calleeSaved.count(reg) && calleeSaved.at(reg); }
void X86_64Base::emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) {
    if (auto* os = cg.getTextStream()) { *os << getBBLabel(&bb) << ":\n"; }
    else { CodeGen::SymbolInfo s; s.name = getBBLabel(&bb); s.sectionName = ".text"; s.value = cg.getAssembler().getCodeSize(); s.type = 0; s.binding = 1; cg.addSymbol(s); }
}
void X86_64Base::emitRegMem(execgen::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
    if (rex) as.emitByte(rex);
    as.emitByte(opcode);
    if (offset >= -128 && offset <= 127) { as.emitByte(0x45 | (reg << 3)); as.emitByte((uint8_t)offset); }
    else { as.emitByte(0x85 | (reg << 3)); as.emitDWord(offset); }
}
void X86_64Base::emitLoadValue(CodeGen& cg, execgen::Assembler& as, ir::Value* v, uint8_t regIdx) {
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) { uint8_t rex = (regIdx >= 8) ? 0x49 : 0x48; as.emitByte(rex); as.emitByte(0xB8 + (regIdx & 7)); as.emitQWord(ci->getValue()); }
    else { int32_t offset = cg.getStackOffset(v); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(as, rex, 0x8B, regIdx & 7, offset); }
}
void X86_64Base::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx) { int32_t offset = cg.getStackOffset(&instr); uint8_t rex = (regIdx >= 8) ? 0x4C : 0x48; emitRegMem(cg.getAssembler(), rex, 0x89, regIdx & 7, offset); }
uint8_t X86_64Base::getRex(const ir::Type* t) { if (!t || t->isPointerTy()) return 0x48; if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 64) return 0x48; } return 0; }
uint8_t X86_64Base::getOpcode(uint8_t baseOp, const ir::Type* t) { if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() == 8) return baseOp - 1; } return baseOp; }
uint8_t X86_64Base::getArchRegIndex(const std::string& regName) { auto it = regToIdx.find(regName); return (it != regToIdx.end()) ? it->second : 0; }
