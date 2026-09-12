#include "target/architecture/wasm32/WasmModule.h"
#include "target/architecture/wasm32/WasmBinary.h"
#include "ir/FunctionType.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cassert>
#include <set>
#include <functional>

namespace target::wasm {

static WasmValType mapType(const ir::Type* type) {
    if (!type) return WasmValType::I32;
    if (type->isFloatTy()) return WasmValType::F32;
    if (type->isDoubleTy()) return WasmValType::F64;
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        if (it->getBitwidth() > 32) return WasmValType::I64;
    }
    return WasmValType::I32;
}

WasmModule WasmLowering::lower(const ir::Module& irModule) {
    WasmModule module;
    std::map<const ir::Function*, uint32_t> funcIndices;

    // Index functions in irModule
    for (auto& funcPtr : irModule.getFunctions()) {
        const ir::Function* func = funcPtr.get();
        const auto* ft = dynamic_cast<const ir::FunctionType*>(func->getType());

        WasmFunctionType wasmFt;
        if (ft) {
            for (auto* pt : ft->getParamTypes()) wasmFt.params.push_back(mapType(pt));
            if (ft->getReturnType() && !ft->getReturnType()->isVoidTy()) {
                wasmFt.results.push_back(mapType(ft->getReturnType()));
            }
        }

        uint32_t typeIdx = module.getOrAddType(wasmFt);
        uint32_t funcIdx = static_cast<uint32_t>(module.functions.size());
        funcIndices[func] = funcIdx;

        WasmFunction wasmFunc;
        wasmFunc.name = func->getName();
        wasmFunc.typeIndex = typeIdx;
        wasmFunc.type = wasmFt;
        wasmFunc.isExported = func->isExported() || func->getName() == "$main" || func->getName() == "main";

        std::string expName = func->getName();
        if (!expName.empty() && expName[0] == '$') expName = expName.substr(1);
        wasmFunc.exportName = expName;

        module.functions.push_back(wasmFunc);
    }

    // Lower function bodies
    for (auto& funcPtr : irModule.getFunctions()) {
        const ir::Function* func = funcPtr.get();
        uint32_t funcIdx = funcIndices[func];
        WasmFunction& wasmFunc = module.functions[funcIdx];

        std::map<const ir::Value*, uint32_t> localIndices;
        uint32_t localIdx = 0;

        for (auto& p : func->getParameters()) {
            localIndices[p.get()] = localIdx++;
        }

        uint32_t paramCount = localIdx;
        std::vector<WasmValType> extraLocalTypes;

        for (auto& bb : func->getBasicBlocks()) {
            for (auto& i : bb->getInstructions()) {
                if (i->getType() && i->getType()->getTypeID() != ir::Type::VoidTyID) {
                    localIndices[i.get()] = localIdx++;
                    extraLocalTypes.push_back(mapType(i->getType()));
                }
            }
        }

        if (!extraLocalTypes.empty()) {
            uint32_t count = 0;
            WasmValType curType = extraLocalTypes[0];
            for (auto t : extraLocalTypes) {
                if (t == curType) {
                    count++;
                } else {
                    wasmFunc.locals.push_back({count, curType});
                    count = 1;
                    curType = t;
                }
            }
            if (count > 0) wasmFunc.locals.push_back({count, curType});
        }

        auto pushOperand = [&](const ir::Value* val) {
            if (!val) return;
            if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
                wasmFunc.body.push_back(WasmInstruction::makeConstI32(static_cast<int32_t>(ci->getValue())));
            } else if (auto* cfp = dynamic_cast<const ir::ConstantFP*>(val)) {
                wasmFunc.body.push_back(WasmInstruction::makeConstF32(static_cast<float>(cfp->getValue())));
            } else {
                uint32_t idx = 0;
                if (localIndices.count(val)) {
                    idx = localIndices.at(val);
                } else {
                    size_t pIdx = 0;
                    for (auto& p : func->getParameters()) {
                        if (p.get() == val || (!p->getName().empty() && p->getName() == val->getName())) {
                            idx = pIdx;
                            break;
                        }
                        pIdx++;
                    }
                }
                wasmFunc.body.push_back(WasmInstruction::makeLocalGet(idx));
            }
        };

        std::set<const ir::BasicBlock*> processedBBs;

        std::function<void(ir::Instruction&)> processInstruction = [&](ir::Instruction& i) {
            switch (i.getOpcode()) {
                case ir::Instruction::Ret:
                    if (!i.getOperands().empty()) {
                        pushOperand(i.getOperands()[0]->get());
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeReturn());
                    break;

                case ir::Instruction::Add:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Add));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Sub:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Sub));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Mul:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Mul));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Div:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32DivS));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Udiv:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32DivU));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Rem:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32RemS));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Urem:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32RemU));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::And:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32And));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Or:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Or));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Xor:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Xor));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Shl:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Shl));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Shr:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32ShrU));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Sar:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32ShrS));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Copy:
                    pushOperand(i.getOperands()[0]->get());
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::Ceq:
                case ir::Instruction::Cne:
                case ir::Instruction::Cslt:
                case ir::Instruction::Csle:
                case ir::Instruction::Csgt:
                case ir::Instruction::Csge:
                case ir::Instruction::Cult:
                case ir::Instruction::Cule:
                case ir::Instruction::Cugt:
                case ir::Instruction::Cuge: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmOpcode op = WasmOpcode::I32Eq;
                    switch (i.getOpcode()) {
                        case ir::Instruction::Ceq: op = WasmOpcode::I32Eq; break;
                        case ir::Instruction::Cne: op = WasmOpcode::I32Ne; break;
                        case ir::Instruction::Cslt: op = WasmOpcode::I32LtS; break;
                        case ir::Instruction::Csle: op = WasmOpcode::I32LeS; break;
                        case ir::Instruction::Csgt: op = WasmOpcode::I32GtS; break;
                        case ir::Instruction::Csge: op = WasmOpcode::I32GeS; break;
                        case ir::Instruction::Cult: op = WasmOpcode::I32LtU; break;
                        case ir::Instruction::Cule: op = WasmOpcode::I32LeU; break;
                        case ir::Instruction::Cugt: op = WasmOpcode::I32GtU; break;
                        case ir::Instruction::Cuge: op = WasmOpcode::I32GeU; break;
                        default: break;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::Call: {
                    if (!i.getOperands().empty()) {
                        const ir::Value* calleeVal = i.getOperands()[0]->get();
                        const ir::Function* calleeFunc = dynamic_cast<const ir::Function*>(calleeVal);
                        for (size_t idx = 1; idx < i.getOperands().size(); ++idx) {
                            pushOperand(i.getOperands()[idx]->get());
                        }
                        uint32_t targetIdx = 0;
                        if (calleeFunc && funcIndices.count(calleeFunc)) {
                            targetIdx = funcIndices[calleeFunc];
                        }
                        wasmFunc.body.push_back(WasmInstruction::makeCall(targetIdx, calleeVal->getName()));
                        if (i.getType() && !i.getType()->isVoidTy() && localIndices.count(&i)) {
                            wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        };

        std::function<void(const ir::BasicBlock*)> lowerBB = [&](const ir::BasicBlock* bb) {
            if (!bb || processedBBs.count(bb)) return;
            processedBBs.insert(bb);

            for (auto& instPtr : bb->getInstructions()) {
                ir::Instruction& i = *instPtr;

                if (i.getOpcode() == ir::Instruction::Jnz || i.getOpcode() == ir::Instruction::Br) {
                    if (i.getOperands().size() >= 3) {
                        pushOperand(i.getOperands()[0]->get());
                        wasmFunc.body.push_back(WasmInstruction::makeIf());

                        auto* trueBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[1]->get());
                        if (trueBB) lowerBB(trueBB);

                        auto* falseBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[2]->get());
                        if (falseBB && falseBB != trueBB) {
                            wasmFunc.body.push_back(WasmInstruction::makeElse());
                            lowerBB(falseBB);
                        }

                        wasmFunc.body.push_back(WasmInstruction::makeEnd());
                        continue;
                    } else if (i.getOperands().size() == 1) {
                        auto* targetBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[0]->get());
                        if (targetBB) lowerBB(targetBB);
                        continue;
                    }
                } else if (i.getOpcode() == ir::Instruction::Jmp) {
                    if (!i.getOperands().empty()) {
                        auto* targetBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[0]->get());
                        if (targetBB) lowerBB(targetBB);
                    }
                    continue;
                }

                processInstruction(i);
            }
        };

        if (!func->getBasicBlocks().empty()) {
            lowerBB(func->getBasicBlocks().front().get());
        }

        for (auto& bb : func->getBasicBlocks()) {
            if (!processedBBs.count(bb.get())) {
                lowerBB(bb.get());
            }
        }

        if (!wasmFunc.type.results.empty()) {
            wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::Unreachable));
        }

        if (wasmFunc.isExported) {
            module.exports.push_back({wasmFunc.exportName, WasmExportKind::Func, funcIdx});
        }
    }

    return module;
}

std::string WasmWatWriter::write(const WasmModule& module) {
    std::stringstream ss;
    ss << "(module\n";

    for (const auto& func : module.functions) {
        std::string funcLabel = func.name;
        if (!funcLabel.empty() && funcLabel[0] != '$') funcLabel = "$" + funcLabel;
        ss << "  (func " << funcLabel;
        for (auto p : func.type.params) {
            ss << " (param " << (p == WasmValType::I32 ? "i32" : (p == WasmValType::I64 ? "i64" : (p == WasmValType::F32 ? "f32" : "f64"))) << ")";
        }
        for (auto r : func.type.results) {
            ss << " (result " << (r == WasmValType::I32 ? "i32" : (r == WasmValType::I64 ? "i64" : (r == WasmValType::F32 ? "f32" : "f64"))) << ")";
        }
        ss << "\n";

        for (const auto& loc : func.locals) {
            std::string typeStr = "i32";
            if (loc.type == WasmValType::I64) typeStr = "i64";
            else if (loc.type == WasmValType::F32) typeStr = "f32";
            else if (loc.type == WasmValType::F64) typeStr = "f64";
            for (uint32_t c = 0; c < loc.count; ++c) {
                ss << "    (local " << typeStr << ")\n";
            }
        }

        for (const auto& inst : func.body) {
            switch (inst.opcode) {
                case WasmOpcode::I32Const: ss << "    i32.const " << inst.intImm << "\n"; break;
                case WasmOpcode::I64Const: ss << "    i64.const " << inst.intImm << "\n"; break;
                case WasmOpcode::F32Const: ss << "    f32.const " << inst.floatImm << "\n"; break;
                case WasmOpcode::LocalGet: ss << "    local.get " << inst.uintImm << "\n"; break;
                case WasmOpcode::LocalSet: ss << "    local.set " << inst.uintImm << "\n"; break;
                case WasmOpcode::LocalTee: ss << "    local.tee " << inst.uintImm << "\n"; break;
                case WasmOpcode::I32Add: ss << "    i32.add\n"; break;
                case WasmOpcode::I32Sub: ss << "    i32.sub\n"; break;
                case WasmOpcode::I32Mul: ss << "    i32.mul\n"; break;
                case WasmOpcode::I32DivS: ss << "    i32.div_s\n"; break;
                case WasmOpcode::I32DivU: ss << "    i32.div_u\n"; break;
                case WasmOpcode::I32RemS: ss << "    i32.rem_s\n"; break;
                case WasmOpcode::I32RemU: ss << "    i32.rem_u\n"; break;
                case WasmOpcode::I32And: ss << "    i32.and\n"; break;
                case WasmOpcode::I32Or: ss << "    i32.or\n"; break;
                case WasmOpcode::I32Xor: ss << "    i32.xor\n"; break;
                case WasmOpcode::I32Shl: ss << "    i32.shl\n"; break;
                case WasmOpcode::I32ShrS: ss << "    i32.shr_s\n"; break;
                case WasmOpcode::I32ShrU: ss << "    i32.shr_u\n"; break;
                case WasmOpcode::I32Eq: ss << "    i32.eq\n"; break;
                case WasmOpcode::I32Ne: ss << "    i32.ne\n"; break;
                case WasmOpcode::I32LtS: ss << "    i32.lt_s\n"; break;
                case WasmOpcode::I32LtU: ss << "    i32.lt_u\n"; break;
                case WasmOpcode::I32LeS: ss << "    i32.le_s\n"; break;
                case WasmOpcode::I32LeU: ss << "    i32.le_u\n"; break;
                case WasmOpcode::I32GtS: ss << "    i32.gt_s\n"; break;
                case WasmOpcode::I32GtU: ss << "    i32.gt_u\n"; break;
                case WasmOpcode::I32GeS: ss << "    i32.ge_s\n"; break;
                case WasmOpcode::I32GeU: ss << "    i32.ge_u\n"; break;
                case WasmOpcode::Call: {
                    if (!inst.symbolImm.empty()) {
                        std::string sym = inst.symbolImm;
                        if (sym[0] != '$') sym = "$" + sym;
                        ss << "    call " << sym << "\n";
                    } else {
                        ss << "    call " << inst.uintImm << "\n";
                    }
                    break;
                }
                case WasmOpcode::Br: ss << "    br " << inst.uintImm << "\n"; break;
                case WasmOpcode::BrIf: ss << "    br_if " << inst.uintImm << "\n"; break;
                case WasmOpcode::Block: ss << "    block\n"; break;
                case WasmOpcode::Loop: ss << "    loop\n"; break;
                case WasmOpcode::If: ss << "    if\n"; break;
                case WasmOpcode::Else: ss << "    else\n"; break;
                case WasmOpcode::Return: ss << "    return\n"; break;
                case WasmOpcode::Unreachable: ss << "    unreachable\n"; break;
                case WasmOpcode::End: ss << "    end\n"; break;
                default: break;
            }
        }
        ss << "  )\n";
    }

    for (const auto& exp : module.exports) {
        std::string funcLabel = exp.name;
        if (!funcLabel.empty() && funcLabel[0] != '$') funcLabel = "$" + funcLabel;
        ss << "  (export \"" << exp.name << "\" (func " << funcLabel << "))\n";
    }

    ss << ")\n";
    return ss.str();
}

static void encodeUnsignedLeb(std::vector<uint8_t>& vec, uint32_t val) {
    codegen::wasm::encode_unsigned_leb128(vec, val);
}

static void encodeSignedLeb(std::vector<uint8_t>& vec, int32_t val) {
    codegen::wasm::encode_signed_leb128(vec, val);
}

std::vector<uint8_t> WasmBinaryWriter::write(const WasmModule& module) {
    std::vector<uint8_t> out = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    // 1. Type Section
    if (!module.types.empty()) {
        out.push_back(codegen::wasm::WasmSection::TYPE);
        std::vector<uint8_t> content;
        encodeUnsignedLeb(content, module.types.size());
        for (const auto& type : module.types) {
            content.push_back(0x60); // func
            encodeUnsignedLeb(content, type.params.size());
            for (auto p : type.params) content.push_back(static_cast<uint8_t>(p));
            encodeUnsignedLeb(content, type.results.size());
            for (auto r : type.results) content.push_back(static_cast<uint8_t>(r));
        }
        encodeUnsignedLeb(out, content.size());
        out.insert(out.end(), content.begin(), content.end());
    }

    // 3. Function Section
    if (!module.functions.empty()) {
        out.push_back(codegen::wasm::WasmSection::FUNCTION);
        std::vector<uint8_t> content;
        encodeUnsignedLeb(content, module.functions.size());
        for (const auto& func : module.functions) {
            encodeUnsignedLeb(content, func.typeIndex);
        }
        encodeUnsignedLeb(out, content.size());
        out.insert(out.end(), content.begin(), content.end());
    }

    // 7. Export Section
    if (!module.exports.empty()) {
        out.push_back(codegen::wasm::WasmSection::EXPORT);
        std::vector<uint8_t> content;
        encodeUnsignedLeb(content, module.exports.size());
        for (const auto& exp : module.exports) {
            encodeUnsignedLeb(content, exp.name.size());
            content.insert(content.end(), exp.name.begin(), exp.name.end());
            content.push_back(static_cast<uint8_t>(exp.kind));
            encodeUnsignedLeb(content, exp.index);
        }
        encodeUnsignedLeb(out, content.size());
        out.insert(out.end(), content.begin(), content.end());
    }

    // 10. Code Section
    if (!module.functions.empty()) {
        out.push_back(codegen::wasm::WasmSection::CODE);
        std::vector<uint8_t> content;
        encodeUnsignedLeb(content, module.functions.size());

        for (const auto& func : module.functions) {
            std::vector<uint8_t> body;
            // Local decls
            encodeUnsignedLeb(body, func.locals.size());
            for (const auto& loc : func.locals) {
                encodeUnsignedLeb(body, loc.count);
                body.push_back(static_cast<uint8_t>(loc.type));
            }
            // Bytecode
            for (const auto& inst : func.body) {
                body.push_back(static_cast<uint8_t>(inst.opcode));
                switch (inst.opcode) {
                    case WasmOpcode::Block:
                    case WasmOpcode::Loop:
                    case WasmOpcode::If:
                        body.push_back(0x40); // void block type in Wasm binary format
                        break;
                    case WasmOpcode::I32Const:
                        encodeSignedLeb(body, static_cast<int32_t>(inst.intImm));
                        break;
                    case WasmOpcode::I64Const:
                        encodeSignedLeb(body, static_cast<int32_t>(inst.intImm));
                        break;
                    case WasmOpcode::LocalGet:
                    case WasmOpcode::LocalSet:
                    case WasmOpcode::LocalTee:
                    case WasmOpcode::Call:
                    case WasmOpcode::Br:
                    case WasmOpcode::BrIf:
                        encodeUnsignedLeb(body, inst.uintImm);
                        break;
                    default:
                        break;
                }
            }
            body.push_back(static_cast<uint8_t>(WasmOpcode::End)); // 0x0B function end

            encodeUnsignedLeb(content, body.size());
            content.insert(content.end(), body.begin(), body.end());
        }

        encodeUnsignedLeb(out, content.size());
        out.insert(out.end(), content.begin(), content.end());
    }

    return out;
}

} // namespace target::wasm
