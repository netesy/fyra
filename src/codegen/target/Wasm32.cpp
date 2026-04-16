#include "codegen/target/Wasm32.h"
#include "codegen/CodeGen.h"
#include "codegen/execgen/Assembler.h"
#include "codegen/target/WasmBinary.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Function.h"
#include "ir/FunctionType.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "transforms/CFGBuilder.h"
#include <ostream>
#include <set>
#include <vector>
#include <cassert>
#include <algorithm>
#include <map>

namespace codegen {
namespace target {


void Wasm32::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "(module\n";
        *os << "  (import \"wasi_unstable\" \"fd_write\" (func $fd_write (param i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_read\" (func $fd_read (param i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_seek\" (func $fd_seek (param i32 i64 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_datasync\" (func $fd_datasync (param i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_open\" (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_create_directory\" (func $path_create_directory (param i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_remove_directory\" (func $path_remove_directory (param i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_unlink_file\" (func $path_unlink_file (param i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_rename\" (func $path_rename (param i32 i32 i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_close\" (func $fd_close (param i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_filestat_get\" (func $fd_filestat_get (param i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"path_filestat_get\" (func $path_filestat_get (param i32 i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_readdir\" (func $fd_readdir (param i32 i32 i32 i64 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"clock_time_get\" (func $clock_time_get (param i32 i64 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"random_get\" (func $random_get (param i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"proc_exit\" (func $proc_exit (param i32)))\n";
        *os << "  (memory 1)\n";
        *os << "  (global $__heap_ptr (mut i32) (i32.const 1024))\n";
        *os << "  (global $__stack_ptr (mut i32) (i32.const 65536))\n";
        *os << "  (func $unknown_func (result i32) i32.const 0 return)\n";
        for (auto& func : cg.module.getFunctions()) *os << "  (export \"" << func->getName() << "\" (func $" << func->getName() << "))\n";
    } else {
        auto& assembler = cg.getAssembler();
        assembler.emitByte(0x00); assembler.emitByte(0x61); assembler.emitByte(0x73); assembler.emitByte(0x6d);
        assembler.emitByte(0x01); assembler.emitByte(0x00); assembler.emitByte(0x00); assembler.emitByte(0x00);
    }
}

void Wasm32::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    uint32_t local_idx = 0; for (auto& param : func.getParameters()) cg.getStackOffsets()[param.get()] = local_idx++;
    std::vector<ir::Value*> instr_locals; for (auto& bb : func.getBasicBlocks()) for (auto& instr : bb->getInstructions()) if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { cg.getStackOffsets()[instr.get()] = local_idx++; instr_locals.push_back(instr.get()); }
    if (auto* os = cg.getTextStream()) { *os << "  (func $" << func.getName(); for (auto& param : func.getParameters()) *os << " (param " << getWasmType(param->getType()) << ")"; if (auto* ft = dynamic_cast<const ir::FunctionType*>(func.getType())) if (!ft->getReturnType()->isVoidTy()) *os << " (result " << getWasmType(ft->getReturnType()) << ")"; *os << "\n"; for (auto* val : instr_locals) *os << "  (local $" << cg.getStackOffset(val) << " " << getWasmType(val->getType()) << ")\n"; if (needsTempLocals(func)) *os << "  (local $temp_i32_0 i32) (local $temp_i32_1 i32) (local $temp_i64_0 i64) (local $temp_i64_1 i64)\n  (local $temp_f32_0 f32) (local $temp_f32_1 f32) (local $temp_f64_0 f64) (local $temp_f64_1 f64)\n"; }
}

void Wasm32::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    (void)func; if (auto* os = cg.getTextStream()) *os << "  )\n"; else cg.getAssembler().emitByte(0x0B);
}

void Wasm32::emitRet(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { if (!instr.getOperands().empty()) { std::string rv = cg.getValueAsOperand(instr.getOperands()[0]->get()); if (!rv.empty()) *os << "  " << rv << "\n"; } *os << "  return\n"; }
    else { if (!instr.getOperands().empty()) { ir::Value* rv = instr.getOperands()[0]->get(); if (auto* c = dynamic_cast<ir::ConstantInt*>(rv)) { cg.getAssembler().emitByte(0x41); std::vector<uint8_t> b; codegen::wasm::encode_unsigned_leb128(b, c->getValue()); cg.getAssembler().emitBytes(b); } } cg.getAssembler().emitByte(0x0F); }
}

void Wasm32::emitAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".add\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x6A);
}

void Wasm32::emitSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".sub\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x6B);
}

void Wasm32::emitMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".mul\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x6C);
}

void Wasm32::emitDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { std::string wt = getWasmType(instr.getType()); std::string wo = (instr.getOpcode() == ir::Instruction::Udiv) ? (wt + ".div_u") : (wt + ".div_s"); *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << wo << "\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x6D);
}

void Wasm32::emitRem(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { std::string wt = getWasmType(instr.getType()); std::string wo = (instr.getOpcode() == ir::Instruction::Urem) ? (wt + ".rem_u") : (wt + ".rem_s"); *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << wo << "\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitCopy(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitCall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { for (size_t i = 1; i < instr.getOperands().size(); ++i) { std::string arg = cg.getValueAsOperand(instr.getOperands()[i]->get()); if (!arg.empty()) *os << "  " << arg << "\n"; } std::string cl = cg.getValueAsOperand(instr.getOperands()[0]->get()); if (cl.empty() || cl[0] != '$') *os << "  call $" << cl << "\n"; else *os << "  call " << cl << "\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else { cg.getAssembler().emitByte(0x10); ir::Function* f = cg.module.getFunction(instr.getOperands()[0]->get()->getName()); if (f) { std::vector<uint8_t> b; codegen::wasm::encode_unsigned_leb128(b, cg.getWasmFunctionIndices().at(f)); cg.getAssembler().emitBytes(b); } }
}

void Wasm32::emitCmp(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { ir::Type* ty = instr.getOperands()[0]->get()->getType(); std::string pr = (ty->isFloatTy() || ty->isDoubleTy()) ? (ty->isFloatTy() ? "f32" : "f64") : (ty->isIntegerTy() && static_cast<ir::IntegerType*>(ty)->getBitwidth() > 32 ? "i64" : "i32"); *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n"; std::string op; switch(instr.getOpcode()) { case ir::Instruction::Ceq: case ir::Instruction::Ceqf: op = pr + ".eq"; break; case ir::Instruction::Cne: case ir::Instruction::Cnef: op = pr + ".ne"; break; case ir::Instruction::Cslt: op = pr + ".lt_s"; break; case ir::Instruction::Csle: op = pr + ".le_s"; break; case ir::Instruction::Csgt: op = pr + ".gt_s"; break; case ir::Instruction::Csge: op = pr + ".ge_s"; break; case ir::Instruction::Cult: op = pr + ".lt_u"; break; case ir::Instruction::Cule: op = pr + ".le_u"; break; case ir::Instruction::Cugt: op = pr + ".gt_u"; break; case ir::Instruction::Cuge: op = pr + ".ge_u"; break; case ir::Instruction::Clt: op = pr + ".lt"; break; case ir::Instruction::Cle: op = pr + ".le"; break; case ir::Instruction::Cgt: op = pr + ".gt"; break; case ir::Instruction::Cge: op = pr + ".ge"; break; default: op = pr + ".eq"; break; } *os << "  " << op << "\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitAnd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".and\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x71);
}

void Wasm32::emitOr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".or\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x72);
}

void Wasm32::emitXor(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".xor\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x73);
}

void Wasm32::emitShl(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".shl\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x74);
}

void Wasm32::emitShr(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".shr_u\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x75);
}

void Wasm32::emitSar(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << getWasmType(instr.getType()) << ".shr_s\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
    else cg.getAssembler().emitByte(0x76);
}

void Wasm32::emitNeg(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { ir::Value* op = instr.getOperands()[0]->get(); if (op->getType()->isFloatTy()) *os << "  " << cg.getValueAsOperand(op) << "\n  f32.neg\n"; else if (op->getType()->isDoubleTy()) *os << "  " << cg.getValueAsOperand(op) << "\n  f64.neg\n"; else *os << "  i32.const 0\n  " << cg.getValueAsOperand(op) << "\n  i32.sub\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitNot(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  i32.const -1\n  i32.xor\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitLoad(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n"; if (instr.getType()->isFloatTy()) *os << "  f32.load\n"; else if (auto* it = dynamic_cast<const ir::IntegerType*>(instr.getType())) { if (it->getBitwidth() == 8) *os << "  i32.load8_s\n"; else if (it->getBitwidth() == 16) *os << "  i32.load16_s\n"; else *os << "  i32.load\n"; } else *os << "  i32.load\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitStore(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n"; ir::Type* ty = instr.getOperands()[0]->get()->getType(); if (ty->isFloatTy()) *os << "  f32.store\n"; else if (auto* it = dynamic_cast<const ir::IntegerType*>(ty)) { if (it->getBitwidth() == 8) *os << "  i32.store8\n"; else if (it->getBitwidth() == 16) *os << "  i32.store16\n"; else *os << "  i32.store\n"; } else *os << "  i32.store\n"; void Wasm32::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.write" || cap == "io.read") { *os << "  global.get $__stack_ptr\n  i32.const 16\n  i32.sub\n  local.tee $temp_i32_0\n  local.get $temp_i32_0\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  i32.store offset=0\n  local.get $temp_i32_0\n  " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  i32.store offset=4\n  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  local.get $temp_i32_0\n  i32.const 1\n  local.get $temp_i32_0\n  i32.const 8\n  i32.add\n  call " << (cap == "io.write" ? "$fd_write" : "$fd_read") << "\n"; }
    else if (cap == "io.close") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call $fd_close\n";
    else if (cap == "io.seek") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  global.get $__heap_ptr\n  call $fd_seek\n  drop\n  global.get $__heap_ptr\n  i64.load\n";
    else if (cap == "io.flush") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call $fd_datasync\n";
    else if (cap == "io.stat") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $fd_filestat_get\n";
void Wasm32::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "process.exit") { std::string op = cg.getValueAsOperand(instr.getOperands()[0]->get()); if (!op.empty()) *os << "  " << op << "\n"; *os << "  call $proc_exit\n"; } else if (cap == "process.abort") *os << "  i32.const 1\n  call $proc_exit\n"; else if (cap == "process.info") *os << "  i32.const 0\n"; void Wasm32::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

TypeInfo Wasm32::getTypeInfo(const ir::Type* type) const {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) return { (uint8_t)(intTy->getBitwidth() <= 32 ? 32 : 64), (uint8_t)(intTy->getBitwidth() <= 32 ? 32 : 64), RegisterClass::Integer, false, true };
    if (type->isFloatTy()) return { 32, 32, RegisterClass::Float, true, true };
    if (type->isDoubleTy()) return { 64, 64, RegisterClass::Float, true, true };
    if (type->isPointerTy()) return { 32, 32, RegisterClass::Integer, false, false };
    return { 32, 32, RegisterClass::Integer, false, true };
}

const std::vector<std::string>& Wasm32::getRegisters(RegisterClass regClass) const { (void)regClass; static const std::vector<std::string> empty = {}; return empty; }
const std::string& Wasm32::getReturnRegister(const ir::Type* type) const { (void)type; static const std::string empty = ""; return empty;
}

void Wasm32::emitPrologue(CodeGen& cg, int stack_size) {
    (void)cg; (void)stack_size;
}

void Wasm32::emitEpilogue(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) *os << "  end_function\n"; else cg.getAssembler().emitByte(0x0B);
}

size_t Wasm32::getMaxRegistersForArgs() const {
    return 0;
}

void Wasm32::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {
    (void)argIndex; (void)type; if (auto* os = cg.getTextStream()) *os << "  " << value << "\n";
}

void Wasm32::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {
    (void)type; if (auto* os = cg.getTextStream()) *os << "  local.get " << argIndex << "\n  local.set " << dest << "\n"; }
const std::vector<std::string>& Wasm32::getIntegerArgumentRegisters() const { static const std::vector<std::string> regs = {}; return regs; }
const std::vector<std::string>& Wasm32::getFloatArgumentRegisters() const { static const std::vector<std::string> regs = {}; return regs; }
const std::string& Wasm32::getIntegerReturnRegister() const { static const std::string reg = ""; return reg; }
const std::string& Wasm32::getFloatReturnRegister() const { static const std::string reg = ""; return reg; }
bool Wasm32::isCallerSaved(const std::string& reg) const { return false; }
bool Wasm32::isCalleeSaved(const std::string& reg) const { return false; }
std::string Wasm32::formatConstant(const ir::ConstantInt* C) const { return (C->getType()->getSize() <= 4 ? "i32.const " : "i64.const ") + std::to_string(C->getValue()); }
std::string Wasm32::getWasmType(const ir::Type* type) { if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) return intTy->getBitwidth() <= 32 ? "i32" : "i64"; if (type->isFloatTy()) return "f32"; if (type->isDoubleTy()) return "f64"; if (type->isPointerTy()) return "i32"; return "i32";
}

void Wasm32::emitFAdd(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  f32.add\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; } else cg.getAssembler().emitByte(0x92);
}

void Wasm32::emitFSub(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  f32.sub\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; } else cg.getAssembler().emitByte(0x93);
}

void Wasm32::emitFMul(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  f32.mul\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; } else cg.getAssembler().emitByte(0x94);
}

void Wasm32::emitFDiv(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  f32.div\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; } else cg.getAssembler().emitByte(0x95);
}

void Wasm32::emitCast(CodeGen& cg, ir::Instruction& instr, const ir::Type* fromType, const ir::Type* toType) {
    if (auto* os = cg.getTextStream()) { *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n"; if (fromType->isIntegerTy() && toType->isFloatTy()) *os << "  f32.convert_i32_s\n"; else if (fromType->isFloatTy() && toType->isIntegerTy()) *os << "  i32.trunc_f32_s\n"; if (cg.getStackOffsets().count(&instr)) *os << "  local.set " << cg.getStackOffset(&instr) << "\n"; }
}

void Wasm32::emitVAStart(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
}

void Wasm32::emitVAArg(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n";
}

void Wasm32::emitVAEnd(CodeGen& cg, ir::Instruction& instr) {
    (void)cg; (void)instr;
}

size_t Wasm32::getPointerSize() const {
    return 32;
}

bool Wasm32::isLeaf(ir::Function& func) const {
    for (auto& bb : func.getBasicBlocks()) for (auto& instr : bb->getInstructions()) if (instr->getOpcode() == ir::Instruction::Call || instr->getOpcode() == ir::Instruction::Syscall) return false;
    return true;
}

bool Wasm32::needsTempLocals(ir::Function& func) const {
    for (auto& bb : func.getBasicBlocks()) for (auto& instr : bb->getInstructions()) if (instr->getOpcode() == ir::Instruction::Alloc || instr->getOpcode() == ir::Instruction::Alloc4 || instr->getOpcode() == ir::Instruction::Alloc16 || instr->getOpcode() == ir::Instruction::ExternCall) return true;
    return false;
}

void Wasm32::emitMemoryOperation(CodeGen& cg, const std::string& operation, const ir::Type* type, size_t alignment, size_t offset) {
    (void)cg; (void)operation; (void)type; (void)alignment; (void)offset;
}

void Wasm32::emitTypeConversion(CodeGen& cg, const ir::Type* fromType, const ir::Type* toType, bool isUnsigned) {
    (void)cg; (void)fromType; (void)toType; (void)isUnsigned; }

}
}

void Wasm32::emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) {
    auto* os = cg.getTextStream(); if (!os) return;
    emitFunctionPrologue(cg, func);
    if (func.getBasicBlocks().empty()) { emitFunctionEpilogue(cg, func); return; }
    std::vector<ir::BasicBlock*> rpo; std::set<ir::BasicBlock*> visited; std::vector<ir::BasicBlock*> postOrder;
    transforms::CFGBuilder::run(func);
    auto dfs = [&](auto self, ir::BasicBlock* bb) -> void {
        visited.insert(bb);
        for (auto* succ : bb->getSuccessors()) if (visited.find(succ) == visited.end()) self(self, succ);
        postOrder.push_back(bb);
    };
    dfs(dfs, func.getBasicBlocks().front().get());
    rpo.assign(postOrder.rbegin(), postOrder.rend());
    for (auto& bb_ptr : func.getBasicBlocks()) if (visited.find(bb_ptr.get()) == visited.end()) rpo.push_back(bb_ptr.get());
    std::map<ir::BasicBlock*, size_t> posMap;
    for (size_t i = 0; i < rpo.size(); ++i) posMap[rpo[i]] = i;
    struct ControlRange { bool isLoop; size_t start; size_t end; ir::BasicBlock* target; };
    std::vector<ControlRange> ranges;
    for (size_t i = 0; i < rpo.size(); ++i) {
        ir::BasicBlock* bb = rpo[i];
        for (auto* succ : bb->getSuccessors()) {
            if (posMap.find(succ) == posMap.end()) continue;
            if (posMap[succ] > i) {
                bool found = false;
                for (auto& r : ranges) if (!r.isLoop && r.target == succ) { r.start = std::min(r.start, i); found = true; break; }
                if (!found) ranges.push_back({false, i, posMap[succ] - 1, succ});
            } else {
                bool found = false;
                for (auto& r : ranges) if (r.isLoop && r.target == succ) { r.end = std::max(r.end, i); found = true; break; }
                if (!found) ranges.push_back({true, posMap[succ], i, succ});
            }
        }
    }
    std::sort(ranges.begin(), ranges.end(), [](const ControlRange& a, const ControlRange& b) { if (a.start != b.start) return a.start < b.start; return a.end > b.end; });
    std::map<size_t, std::vector<ControlRange>> startsAt, endsAt;
    for (const auto& r : ranges) { startsAt[r.start].push_back(r); endsAt[r.end].push_back(r); }
    visitedBlocks.clear();
    for (size_t i = 0; i < rpo.size(); ++i) {
        ir::BasicBlock* bb = rpo[i];
        if (startsAt.count(i)) { for (const auto& r : startsAt[i]) { if (r.isLoop) *os << "  loop $" << r.target->getName() << "_loop\n"; else *os << "  block $" << r.target->getName() << "\n"; } }
        *os << "    ;; Block: " << bb->getName() << "\n"; visitedBlocks.insert(bb);
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Br || instr->getOpcode() == ir::Instruction::Jnz) {
                if (instr->getOperands().size() == 1) {
                    ir::BasicBlock* target = static_cast<ir::BasicBlock*>(instr->getOperands()[0]->get());
                    emitPhis(cg, target, bb, "    ");
                    if (posMap[target] <= i) *os << "    br $" << target->getName() << "_loop\n"; else *os << "    br $" << target->getName() << "\n";
                } else if (instr->getOperands().size() >= 3) {
                    ir::Value* cond = instr->getOperands()[0]->get();
                    ir::BasicBlock* trueBB = static_cast<ir::BasicBlock*>(instr->getOperands()[1]->get());
                    ir::BasicBlock* falseBB = static_cast<ir::BasicBlock*>(instr->getOperands()[2]->get());
                    std::string condOp = cg.getValueAsOperand(cond); if (!condOp.empty()) *os << "    " << condOp << "\n";
                    *os << "    if\n"; emitPhis(cg, trueBB, bb, "      ");
                    if (posMap.count(trueBB) && posMap[trueBB] <= i) *os << "      br $" << trueBB->getName() << "_loop\n"; else *os << "      br $" << trueBB->getName() << "\n";
                    *os << "    else\n"; emitPhis(cg, falseBB, bb, "      ");
                    if (posMap.count(falseBB) && posMap[falseBB] <= i) *os << "      br $" << falseBB->getName() << "_loop\n"; else *os << "      br $" << falseBB->getName() << "\n";
                    *os << "    end\n";
                }
            } else if (instr->getOpcode() == ir::Instruction::Jmp) {
                ir::Value* targetVal = instr->getOperands()[0]->get();
                if (auto* target = dynamic_cast<ir::BasicBlock*>(targetVal)) {
                    emitPhis(cg, target, bb, "    ");
                    if (posMap.count(target) && posMap[target] <= i) *os << "    br $" << target->getName() << "_loop\n"; else *os << "    br $" << target->getName() << "\n";
                } else cg.emitInstruction(*instr);
            } else if (instr->getOpcode() != ir::Instruction::Phi) cg.emitInstruction(*instr);
        }
        if (endsAt.count(i)) {
            auto& endList = endsAt[i]; std::sort(endList.begin(), endList.end(), [](const ControlRange& a, const ControlRange& b) { return a.start > b.start; });
            for (const auto& r : endList) { if (r.isLoop) *os << "  end ;; $" << r.target->getName() << "_loop\n"; else *os << "  end ;; $" << r.target->getName() << "\n"; }
        }
    }
    emitFunctionEpilogue(cg, func);
}

void Wasm32::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        // Wasm handles return value in emit*Call
    }
}

void Wasm32::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.write" || cap == "io.read") { *os << "  global.get $__stack_ptr\n  i32.const 16\n  i32.sub\n  local.tee $temp_i32_0\n  local.get $temp_i32_0\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  i32.store offset=0\n  local.get $temp_i32_0\n  " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  i32.store offset=4\n  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  local.get $temp_i32_0\n  i32.const 1\n  local.get $temp_i32_0\n  i32.const 8\n  i32.add\n  call " << (cap == "io.write" ? "$fd_write" : "$fd_read") << "\n"; }
    else if (cap == "io.close") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call $fd_close\n";
    else if (cap == "io.seek") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  global.get $__heap_ptr\n  call $fd_seek\n  drop\n  global.get $__heap_ptr\n  i64.load\n";
    else if (cap == "io.flush") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call $fd_datasync\n";
    else if (cap == "io.stat") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $fd_filestat_get\n";
void Wasm32::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "process.exit") { std::string op = cg.getValueAsOperand(instr.getOperands()[0]->get()); if (!op.empty()) *os << "  " << op << "\n"; *os << "  call $proc_exit\n"; } else if (cap == "process.abort") *os << "  i32.const 1\n  call $proc_exit\n"; else if (cap == "process.info") *os << "  i32.const 0\n"; void Wasm32::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Wasm32::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Wasm32::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return; if (cap == "process.exit") { std::string op = cg.getValueAsOperand(instr.getOperands()[0]->get()); if (!op.empty()) *os << "  " << op << "\n"; *os << "  call $proc_exit\n"; } else if (cap == "process.abort") *os << "  i32.const 1\n  call $proc_exit\n"; else if (cap == "process.info") *os << "  i32.const 0\n"; void Wasm32::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Wasm32::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap; void Wasm32::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)cg; (void)instr; (void)cap; void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Wasm32::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Wasm32::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap; void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  i32.const -38\n";
}

void Wasm32::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "env.get") *os << "  i32.const 0\n";
    else *os << "  i32.const 0\n";
}

void Wasm32::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "system.page_size") *os << "  i32.const 65536\n";
    else if (cap == "system.cpu_count") *os << "  i32.const 1\n";
    else *os << "  i32.const -38\n";
}

void Wasm32::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  i32.const -38\n";
}

void Wasm32::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return; if (cap == "random.u64") *os << "  global.get $__heap_ptr\n  i32.const 8\n  call $random_get\n  drop\n  global.get $__heap_ptr\n  i64.load\n"; else if (cap == "random.bytes") *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call $random_get\n"; void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) { (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)instr; (void)cap; if (auto* os = cg.getTextStream()) *os << "  i32.const 0\n"; void Wasm32::emitFunctionTableCall(CodeGen& cg, const std::string& tableIndex, const std::string& typeIndex) { (void)cg; (void)tableIndex; (void)typeIndex;
}

void Wasm32::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "debug.trace") {
        *os << "  nop\n";
    } else if (cap == "debug.log" && !instr.getOperands().empty()) {
        *os << "  " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  drop\n";
    } else {
        *os << "  i32.const 0\n";
    }
}

void Wasm32::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  i32.const -38\n";
}

void Wasm32::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr;
    if (cap == "tty.isatty") *os << "  i32.const 1\n";
    else *os << "  i32.const -38\n";
}

void Wasm32::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  i32.const -38\n";
}

void Wasm32::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    (void)instr; (void)cap;
    *os << "  i32.const -38\n";
}


} // namespace target
} // namespace codegen
