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
    I64ShrU = 0x88
};

struct WasmInstruction {
    WasmOpcode opcode;
    int64_t intImm = 0;
    double floatImm = 0.0;
    uint32_t uintImm = 0;
    std::string symbolImm;

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
