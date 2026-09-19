#include "target/architecture/wasm32/Wasm32Architecture.h"
#include "target/architecture/wasm32/WasmModule.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/FunctionType.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include <ostream>
#include <algorithm>

namespace target {

Wasm32Architecture::Wasm32Architecture() {}

TypeInfo Wasm32Architecture::getTypeInfo(const ir::Type* type) const {
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        if (it->getBitwidth() <= 32) return {32, 32, RegisterClass::Integer, false, true};
        return {64, 64, RegisterClass::Integer, false, true};
    }
    if (type->isFloatTy()) return {32, 32, RegisterClass::Float, true, true};
    if (type->isDoubleTy()) return {64, 64, RegisterClass::Float, true, true};
    return {32, 32, RegisterClass::Integer, false, false};
}

const std::vector<std::string>& Wasm32Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> empty = {}; return empty;
}
const std::string& Wasm32Architecture::getReturnRegister(const ir::Type* type) const {
    static const std::string empty = ""; return empty;
}

std::string Wasm32Architecture::getWasmType(const ir::Type* type) const {
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) return (it->getBitwidth() <= 32) ? "i32" : "i64";
    if (type->isFloatTy()) return "f32";
    if (type->isDoubleTy()) return "f64";
    return "i32";
}

std::string Wasm32Architecture::formatStackOperand(int o) const { return "(local.get " + std::to_string(o) + ")"; }
std::string Wasm32Architecture::formatGlobalOperand(const std::string& n) const { return n; }
std::string Wasm32Architecture::formatConstant(const ir::ConstantInt* C) const { return getWasmType(C->getType()) + ".const " + std::to_string(C->getValue()); }
std::string Wasm32Architecture::formatConstant(const ir::ConstantFP* C) const { return getWasmType(C->getType()) + ".const " + std::to_string(C->getValue()); }

void Wasm32Architecture::emitHeader(CodeGen& cg) {
    wasm::WasmModule wasmMod = wasm::WasmLowering::lower(cg.module);

    if (auto* os = cg.getTextStream()) {
        std::string wat = wasm::WasmWatWriter::write(wasmMod);
        *os << wat;
    } else {
        std::vector<uint8_t> bytes = wasm::WasmBinaryWriter::write(wasmMod);
        cg.getAssembler().emitBytes(bytes);
    }
}

void Wasm32Architecture::emitFooter(CodeGen& cg) {
}

void Wasm32Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
}

void Wasm32Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
}

void Wasm32Architecture::emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) {
}

void Wasm32Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}

void Wasm32Architecture::emitTypeSection(CodeGen& cg) {}
void Wasm32Architecture::emitFunctionSection(CodeGen& cg) {}
void Wasm32Architecture::emitExportSection(CodeGen& cg) {}
void Wasm32Architecture::emitCodeSection(CodeGen& cg) {}

void Wasm32Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {}
void Wasm32Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void Wasm32Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

void Wasm32Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {}
void Wasm32Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {}

} // namespace target
