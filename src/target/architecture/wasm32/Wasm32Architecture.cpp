#include "target/architecture/wasm32/Wasm32Architecture.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include "codegen/target/WasmBinary.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/FunctionType.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include <ostream>
#include <algorithm>

namespace codegen {
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

void Wasm32Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    uint32_t idx = 0; for (auto& p : func.getParameters()) cg.getStackOffsets()[p.get()] = idx++;
    for (auto& bb : func.getBasicBlocks()) { for (auto& i : bb->getInstructions()) { if (i->getType()->getTypeID() != ir::Type::VoidTyID) cg.getStackOffsets()[i.get()] = idx++; } }
    if (auto* os = cg.getTextStream()) {
        *os << "  (func $" << func.getName();
        for (auto& p : func.getParameters()) *os << " (param " << getWasmType(p->getType()) << ")";
        if (auto* ft = dynamic_cast<const ir::FunctionType*>(func.getType())) {
            if (!ft->getReturnType()->isVoidTy()) *os << " (result " << getWasmType(ft->getReturnType()) << ")";
        }
        *os << "\n";
    }
}

void Wasm32Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) *os << "  )\n";
    else cg.getAssembler().emitByte(0x0B);
}

void Wasm32Architecture::emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) {
}

void Wasm32Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty()) *os << "  " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  return\n";
    }
}

void Wasm32Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << getWasmType(i.getType()) << ".add\n";
        if (cg.getStackOffsets().count(&i)) *os << "  local.set " << cg.getStackOffset(&i) << "\n";
    }
}

void Wasm32Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i); if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (spec) osInfo.emitIOCapability(cg, i, *spec, *this);
}

std::string Wasm32Architecture::getWasmType(const ir::Type* type) const {
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) return (it->getBitwidth() <= 32) ? "i32" : "i64";
    if (type->isFloatTy()) return "f32"; if (type->isDoubleTy()) return "f64";
    return "i32";
}

std::string Wasm32Architecture::formatStackOperand(int o) const { return "(local.get " + std::to_string(o) + ")"; }
std::string Wasm32Architecture::formatGlobalOperand(const std::string& n) const { return n; }
std::string Wasm32Architecture::formatConstant(const ir::ConstantInt* C) const { return getWasmType(C->getType()) + ".const " + std::to_string(C->getValue()); }

void Wasm32Architecture::emitTypeSection(CodeGen& cg) {
    auto& assembler = cg.getAssembler(); assembler.emitByte(codegen::wasm::WasmSection::TYPE);
    std::vector<uint8_t> content; codegen::wasm::encode_unsigned_leb128(content, cg.getWasmTypeIndices().size());
    for (auto const& [type, index] : cg.getWasmTypeIndices()) { (void)index; content.push_back(codegen::wasm::WasmType::FUNC); codegen::wasm::encode_unsigned_leb128(content, type->getParamTypes().size()); for (size_t i = 0; i < type->getParamTypes().size(); ++i) content.push_back(codegen::wasm::WasmType::I32); codegen::wasm::encode_unsigned_leb128(content, 1); content.push_back(codegen::wasm::WasmType::I32); }
    std::vector<uint8_t> size_bytes; codegen::wasm::encode_unsigned_leb128(size_bytes, content.size()); assembler.emitBytes(size_bytes); assembler.emitBytes(content);
}

void Wasm32Architecture::emitFunctionSection(CodeGen& cg) {
    auto& assembler = cg.getAssembler(); assembler.emitByte(codegen::wasm::WasmSection::FUNCTION);
    std::vector<uint8_t> content; codegen::wasm::encode_unsigned_leb128(content, cg.getWasmFunctionIndices().size());
    for (auto const& [func, index] : cg.getWasmFunctionIndices()) { (void)index; const auto* ft = dynamic_cast<const ir::FunctionType*>(func->getType()); uint32_t type_idx = cg.getWasmTypeIndices().at(ft); codegen::wasm::encode_unsigned_leb128(content, type_idx); }
    std::vector<uint8_t> size_bytes; codegen::wasm::encode_unsigned_leb128(size_bytes, content.size()); assembler.emitBytes(size_bytes); assembler.emitBytes(content);
}

void Wasm32Architecture::emitExportSection(CodeGen& cg) {
    auto& assembler = cg.getAssembler(); assembler.emitByte(codegen::wasm::WasmSection::EXPORT);
    std::vector<uint8_t> content; codegen::wasm::encode_unsigned_leb128(content, cg.getWasmFunctionIndices().size());
    for (auto const& [func, index] : cg.getWasmFunctionIndices()) { std::string name = func->getName(); if (name[0] == '$') name = name.substr(1); codegen::wasm::encode_unsigned_leb128(content, name.size()); content.insert(content.end(), name.begin(), name.end()); content.push_back(codegen::wasm::WasmExportType::FUNC_EXPORT); codegen::wasm::encode_unsigned_leb128(content, index); }
    std::vector<uint8_t> size_bytes; codegen::wasm::encode_unsigned_leb128(size_bytes, content.size()); assembler.emitBytes(size_bytes); assembler.emitBytes(content);
}

void Wasm32Architecture::emitCodeSection(CodeGen& cg) {
    auto& assembler = cg.getAssembler(); assembler.emitByte(codegen::wasm::WasmSection::CODE);
    std::vector<uint8_t> content; codegen::wasm::encode_unsigned_leb128(content, cg.getWasmFunctionBodies().size());
    for (const auto& func_body : cg.getWasmFunctionBodies()) { std::vector<uint8_t> func_content; codegen::wasm::encode_unsigned_leb128(func_content, 0); func_content.insert(func_content.end(), func_body.begin(), func_body.end()); std::vector<uint8_t> func_size_bytes; codegen::wasm::encode_unsigned_leb128(func_size_bytes, func_content.size()); content.insert(content.end(), func_size_bytes.begin(), func_size_bytes.end()); content.insert(content.end(), func_content.begin(), func_content.end()); }
    std::vector<uint8_t> size_bytes; codegen::wasm::encode_unsigned_leb128(size_bytes, content.size()); assembler.emitBytes(size_bytes); assembler.emitBytes(content);
}

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
void Wasm32Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {}
void Wasm32Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {}
void Wasm32Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void Wasm32Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
}
