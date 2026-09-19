#pragma once

#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <ostream>

namespace target::wasm {

enum class WasmValType : uint8_t {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
    V128 = 0x7B
};

struct WasmFunctionType {
    std::vector<WasmValType> params;
    std::vector<WasmValType> results;

    bool operator==(const WasmFunctionType& other) const {
        return params == other.params && results == other.results;
    }
};

enum class WasmOpcode : uint8_t {
    Unreachable = 0x00,
    Nop = 0x01,
    Block = 0x02,
    Loop = 0x03,
    If = 0x04,
    Else = 0x05,
    End = 0x0B,
    Br = 0x0C,
    BrIf = 0x0D,
    Return = 0x0F,
    Call = 0x10,
    Drop = 0x1A,
    LocalGet = 0x20,
    LocalSet = 0x21,
    LocalTee = 0x22,
    I32Const = 0x41,
    I64Const = 0x42,
    F32Const = 0x43,
    F64Const = 0x44,
    I32Eqz = 0x45,
    I32Eq = 0x46,
    I32Ne = 0x47,
    I32LtS = 0x48,
    I32LtU = 0x49,
    I32GtS = 0x4A,
    I32GtU = 0x4B,
    I32LeS = 0x4C,
    I32LeU = 0x4D,
    I32GeS = 0x4E,
    I32GeU = 0x4F,
    I64Eqz = 0x50,
    I64Eq = 0x51,
    I64Ne = 0x52,
    I64LtS = 0x53,
    I64LtU = 0x54,
    I64GtS = 0x55,
    I64GtU = 0x56,
    I64LeS = 0x57,
    I64LeU = 0x58,
    I64GeS = 0x59,
    I64GeU = 0x5A,
    I32Add = 0x6A,
    I32Sub = 0x6B,
    I32Mul = 0x6C,
    I32DivS = 0x6D,
    I32DivU = 0x6E,
    I32RemS = 0x6F,
    I32RemU = 0x70,
    I32And = 0x71,
    I32Or = 0x72,
    I32Xor = 0x73,
    I32Shl = 0x74,
    I32ShrS = 0x75,
    I32ShrU = 0x76,
    I64Add = 0x7C,
    I64Sub = 0x7D,
    I64Mul = 0x7E,
    I64DivS = 0x7F,
    I64DivU = 0x80,
    I64RemS = 0x81,
    I64RemU = 0x82,
    I64And = 0x83,
    I64Or = 0x84,
    I64Xor = 0x85,
    I64Shl = 0x86,
    I64ShrS = 0x87,
    I64ShrU = 0x88,
    SIMDPrefix = 0xFD
};

enum class WasmSIMDOpcode : uint32_t {
    V128Load = 0x00,
    V128Store = 0x0B,
    I8x16Splat = 0x0F,
    I16x8Splat = 0x10,
    I32x4Splat = 0x11,
    I64x2Splat = 0x12,
    F32x4Splat = 0x13,
    F64x2Splat = 0x14,
    I8x16ExtractLaneS = 0x15,
    I8x16ExtractLaneU = 0x16,
    I8x16ReplaceLane = 0x17,
    I16x8ExtractLaneS = 0x18,
    I16x8ExtractLaneU = 0x19,
    I16x8ReplaceLane = 0x1A,
    I32x4ExtractLane = 0x1B,
    I32x4ReplaceLane = 0x1C,
    I64x2ExtractLane = 0x1D,
    I64x2ReplaceLane = 0x1E,
    F32x4ExtractLane = 0x1F,
    F32x4ReplaceLane = 0x20,
    F64x2ExtractLane = 0x21,
    F64x2ReplaceLane = 0x22,
    V128And = 0x4E,
    V128Or = 0x50,
    V128Xor = 0x51,
    V128Bitselect = 0x52,
    I8x16Add = 0x6E,
    I8x16Sub = 0x71,
    I16x8Add = 0x8E,
    I16x8Sub = 0x91,
    I16x8Mul = 0x95,
    I32x4Eq = 0x8C,
    I32x4Ne = 0x8D,
    I32x4LtS = 0x8E,
    I32x4LtU = 0x8F,
    I32x4GtS = 0x90,
    I32x4GtU = 0x91,
    I32x4LeS = 0x92,
    I32x4LeU = 0x93,
    I32x4GeS = 0x94,
    I32x4GeU = 0x95,
    I32x4Add = 0xAE,
    I32x4Sub = 0xAF,
    I32x4Mul = 0xB5,
    I64x2Add = 0xCE,
    I64x2Sub = 0xCF,
    F32x4Eq = 0x41,
    F32x4Ne = 0x42,
    F32x4Lt = 0x43,
    F32x4Gt = 0x44,
    F32x4Le = 0x45,
    F32x4Ge = 0x46,
    F32x4Add = 0xE4,
    F32x4Sub = 0xE5,
    F32x4Mul = 0xE6,
    F32x4Div = 0xE7,
    F64x2Eq = 0x47,
    F64x2Ne = 0x48,
    F64x2Lt = 0x49,
    F64x2Gt = 0x4A,
    F64x2Le = 0x4B,
    F64x2Ge = 0x4C,
    F64x2Add = 0xF0,
    F64x2Sub = 0xF1,
    F64x2Mul = 0xF2,
    F64x2Div = 0xF3
};

struct WasmInstruction {
    WasmOpcode opcode;
    WasmSIMDOpcode simdOpcode = WasmSIMDOpcode::I32x4Add;
    int64_t intImm = 0;
    double floatImm = 0.0;
    uint32_t uintImm = 0;
    std::string symbolImm;
    bool isSIMD = false;

    static WasmInstruction makeSIMD(WasmSIMDOpcode simdOp, uint32_t imm = 0) {
        WasmInstruction inst;
        inst.opcode = WasmOpcode::SIMDPrefix;
        inst.simdOpcode = simdOp;
        inst.uintImm = imm;
        inst.isSIMD = true;
        return inst;
    }

    static WasmInstruction makeConstI32(int32_t val) {
        WasmInstruction inst; inst.opcode = WasmOpcode::I32Const; inst.intImm = val; return inst;
    }
    static WasmInstruction makeConstI64(int64_t val) {
        WasmInstruction inst; inst.opcode = WasmOpcode::I64Const; inst.intImm = val; return inst;
    }
    static WasmInstruction makeConstF32(float val) {
        WasmInstruction inst; inst.opcode = WasmOpcode::F32Const; inst.floatImm = val; return inst;
    }
    static WasmInstruction makeLocalGet(uint32_t localIdx) {
        WasmInstruction inst; inst.opcode = WasmOpcode::LocalGet; inst.uintImm = localIdx; return inst;
    }
    static WasmInstruction makeLocalSet(uint32_t localIdx) {
        WasmInstruction inst; inst.opcode = WasmOpcode::LocalSet; inst.uintImm = localIdx; return inst;
    }
    static WasmInstruction makeCall(uint32_t funcIdx, const std::string& name = "") {
        WasmInstruction inst; inst.opcode = WasmOpcode::Call; inst.uintImm = funcIdx; inst.symbolImm = name; return inst;
    }
    static WasmInstruction makeBr(uint32_t depth) {
        WasmInstruction inst; inst.opcode = WasmOpcode::Br; inst.uintImm = depth; return inst;
    }
    static WasmInstruction makeBrIf(uint32_t depth) {
        WasmInstruction inst; inst.opcode = WasmOpcode::BrIf; inst.uintImm = depth; return inst;
    }
    static WasmInstruction makeBlock() {
        WasmInstruction inst; inst.opcode = WasmOpcode::Block; return inst;
    }
    static WasmInstruction makeLoop() {
        WasmInstruction inst; inst.opcode = WasmOpcode::Loop; return inst;
    }
    static WasmInstruction makeIf() {
        WasmInstruction inst; inst.opcode = WasmOpcode::If; return inst;
    }
    static WasmInstruction makeElse() {
        WasmInstruction inst; inst.opcode = WasmOpcode::Else; return inst;
    }
    static WasmInstruction makeReturn() {
        WasmInstruction inst; inst.opcode = WasmOpcode::Return; return inst;
    }
    static WasmInstruction makeEnd() {
        WasmInstruction inst; inst.opcode = WasmOpcode::End; return inst;
    }
    static WasmInstruction makeSimple(WasmOpcode op) {
        WasmInstruction inst; inst.opcode = op; return inst;
    }
};

struct WasmLocalGroup {
    uint32_t count;
    WasmValType type;
};

struct WasmFunction {
    std::string name;
    uint32_t typeIndex;
    WasmFunctionType type;
    std::vector<WasmLocalGroup> locals;
    std::vector<WasmInstruction> body;
    bool isExported = false;
    std::string exportName;
};

enum class WasmExportKind : uint8_t {
    Func = 0x00,
    Table = 0x01,
    Mem = 0x02,
    Global = 0x03
};

struct WasmExport {
    std::string name;
    WasmExportKind kind;
    uint32_t index;
};

struct WasmImport {
    std::string moduleName;
    std::string fieldName;
    WasmExportKind kind;
    uint32_t typeIndex;
    std::string funcName;
};

struct WasmModule {
    std::vector<WasmFunctionType> types;
    std::vector<WasmImport> imports;
    std::vector<WasmFunction> functions;
    std::vector<WasmExport> exports;

    uint32_t getOrAddType(const WasmFunctionType& type) {
        for (size_t i = 0; i < types.size(); ++i) {
            if (types[i] == type) return static_cast<uint32_t>(i);
        }
        types.push_back(type);
        return static_cast<uint32_t>(types.size() - 1);
    }
};

class WasmLowering {
public:
    static WasmModule lower(const ir::Module& irModule);
};

class WasmWatWriter {
public:
    static std::string write(const WasmModule& module);
};

class WasmBinaryWriter {
public:
    static std::vector<uint8_t> write(const WasmModule& module);
};

} // namespace target::wasm
