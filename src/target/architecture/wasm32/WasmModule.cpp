#include "target/architecture/wasm32/WasmModule.h"
#include "target/architecture/wasm32/WasmBinary.h"
#include "ir/FunctionType.h"
#include "ir/SIMDInstruction.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/PhiNode.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cassert>
#include <set>
#include <functional>
#include <stdexcept>

namespace target::wasm {

static WasmValType mapType(const ir::Type* type) {
    if (!type) return WasmValType::I32;
    if (type->isVectorTy() || dynamic_cast<const ir::VectorType*>(type) != nullptr) return WasmValType::V128;
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

        auto handlePhiAssignments = [&](const ir::BasicBlock* fromBB, const ir::BasicBlock* toBB) {
            if (!toBB || !fromBB) return;
            std::vector<const ir::PhiNode*> phis;
            std::vector<const ir::Value*> incVals;

            for (auto& instPtr : toBB->getInstructions()) {
                if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
                    if (auto* incVal = phi->getIncomingValueForBlock(const_cast<ir::BasicBlock*>(fromBB))) {
                        phis.push_back(phi);
                        incVals.push_back(incVal);
                    }
                }
            }

            if (phis.empty()) return;

            for (size_t k = 0; k < phis.size(); ++k) {
                pushOperand(incVals[k]);
            }

            for (int k = static_cast<int>(phis.size()) - 1; k >= 0; --k) {
                if (localIndices.count(phis[k])) {
                    wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(phis[k])));
                } else {
                    wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::Drop));
                }
            }
        };

        // Pre-analyze successors and predecessors for CFG natural loop analysis
        std::map<const ir::BasicBlock*, std::vector<const ir::BasicBlock*>> cfgSuccessors;
        std::map<const ir::BasicBlock*, std::vector<const ir::BasicBlock*>> cfgPredecessors;

        for (auto& bbPtr : func->getBasicBlocks()) {
            const ir::BasicBlock* bb = bbPtr.get();
            for (auto& instPtr : bb->getInstructions()) {
                uint32_t op = instPtr->getOpcode();
                if (op == ir::Instruction::Jnz || op == ir::Instruction::Br || op == ir::Instruction::Jz) {
                    if (instPtr->getOperands().size() >= 3) {
                        auto* tBB = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[1]->get());
                        auto* fBB = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[2]->get());
                        if (tBB) { cfgSuccessors[bb].push_back(tBB); cfgPredecessors[tBB].push_back(bb); }
                        if (fBB && fBB != tBB) { cfgSuccessors[bb].push_back(fBB); cfgPredecessors[fBB].push_back(bb); }
                    } else if (instPtr->getOperands().size() == 1) {
                        auto* tBB = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[0]->get());
                        if (tBB) { cfgSuccessors[bb].push_back(tBB); cfgPredecessors[tBB].push_back(bb); }
                    }
                } else if (op == ir::Instruction::Jmp) {
                    if (!instPtr->getOperands().empty()) {
                        auto* tBB = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[0]->get());
                        if (tBB) { cfgSuccessors[bb].push_back(tBB); cfgPredecessors[tBB].push_back(bb); }
                    }
                }
            }
        }

        // Detect backedges and natural loops via DFS
        struct LoopInfo {
            const ir::BasicBlock* header = nullptr;
            std::set<const ir::BasicBlock*> bodyBlocks;
            std::vector<const ir::BasicBlock*> exitBBs;
        };

        std::map<const ir::BasicBlock*, LoopInfo> naturalLoops; // key = header
        std::vector<const ir::BasicBlock*> dfsStack;
        std::set<const ir::BasicBlock*> visitedDFS;

        std::function<void(const ir::BasicBlock*)> findNaturalLoops = [&](const ir::BasicBlock* bb) {
            visitedDFS.insert(bb);
            dfsStack.push_back(bb);

            for (const ir::BasicBlock* succ : cfgSuccessors[bb]) {
                auto it = std::find(dfsStack.begin(), dfsStack.end(), succ);
                if (it != dfsStack.end()) {
                    // Backedge bb -> succ found! succ is header H, bb is latch L
                    const ir::BasicBlock* header = succ;
                    LoopInfo& loop = naturalLoops[header];
                    loop.header = header;
                    loop.bodyBlocks.insert(header);
                    loop.bodyBlocks.insert(bb);

                    std::vector<const ir::BasicBlock*> worklist = {bb};
                    while (!worklist.empty()) {
                        const ir::BasicBlock* curr = worklist.back();
                        worklist.pop_back();
                        for (const ir::BasicBlock* pred : cfgPredecessors[curr]) {
                            if (!loop.bodyBlocks.count(pred)) {
                                loop.bodyBlocks.insert(pred);
                                worklist.push_back(pred);
                            }
                        }
                    }
                } else if (!visitedDFS.count(succ)) {
                    findNaturalLoops(succ);
                }
            }

            dfsStack.pop_back();
        };

        if (!func->getBasicBlocks().empty()) {
            findNaturalLoops(func->getBasicBlocks().front().get());
        }

        auto isReachableFromEntryWithout = [&](const ir::BasicBlock* targetBB, const ir::BasicBlock* blockedBB) -> bool {
            if (targetBB == blockedBB) return false;
            const ir::BasicBlock* entryBB = func->getBasicBlocks().front().get();
            if (entryBB == targetBB) return true;

            std::vector<const ir::BasicBlock*> worklist = {entryBB};
            std::set<const ir::BasicBlock*> visited = {entryBB};

            while (!worklist.empty()) {
                const ir::BasicBlock* curr = worklist.back();
                worklist.pop_back();

                if (curr == blockedBB) continue;
                if (curr == targetBB) return true;

                if (cfgSuccessors.count(curr)) {
                    for (const ir::BasicBlock* succ : cfgSuccessors.at(curr)) {
                        if (!visited.count(succ)) {
                            visited.insert(succ);
                            worklist.push_back(succ);
                        }
                    }
                }
            }
            return false;
        };

        // Validate loop reducibility & compute exit blocks
        for (auto& pair : naturalLoops) {
            LoopInfo& loop = pair.second;
            for (const ir::BasicBlock* b : loop.bodyBlocks) {
                if (b != loop.header) {
                    if (isReachableFromEntryWithout(b, loop.header)) {
                        throw std::runtime_error("wasm32: unsupported irreducible/cyclic CFG in function " + func->getName());
                    }
                }
                for (const ir::BasicBlock* succ : cfgSuccessors[b]) {
                    if (!loop.bodyBlocks.count(succ)) {
                        if (std::find(loop.exitBBs.begin(), loop.exitBBs.end(), succ) == loop.exitBBs.end()) {
                            loop.exitBBs.push_back(succ);
                        }
                    }
                }
            }
        }

        struct Scope {
            enum Kind { Block, Loop, If } kind;
            const ir::BasicBlock* targetBB;
        };
        std::vector<Scope> scopeStack;

        auto getScopeDepth = [&](const ir::BasicBlock* targetBB) -> uint32_t {
            for (int idx = static_cast<int>(scopeStack.size()) - 1; idx >= 0; --idx) {
                if (scopeStack[idx].targetBB == targetBB) {
                    return static_cast<uint32_t>(static_cast<int>(scopeStack.size()) - 1 - idx);
                }
            }
            return 0;
        };

        std::set<const ir::BasicBlock*> processedBBs;
        std::set<const ir::BasicBlock*> activeDFS;
        const ir::BasicBlock* currentMergeBB = nullptr;

        std::function<void(ir::Instruction&, const ir::BasicBlock*)> processInstruction = [&](ir::Instruction& i, const ir::BasicBlock* currentBB) {
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

                case ir::Instruction::VLoad:
                    pushOperand(i.getOperands()[0]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128Load));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::VStore:
                    pushOperand(i.getOperands()[1]->get());
                    pushOperand(i.getOperands()[0]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128Store));
                    break;

                case ir::Instruction::VAdd: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::I32x4Add;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->getSize() == 1) op = WasmSIMDOpcode::I8x16Add;
                        else if (vt->getElementType()->getSize() == 2) op = WasmSIMDOpcode::I16x8Add;
                        else if (vt->getElementType()->getSize() == 4) op = WasmSIMDOpcode::I32x4Add;
                        else if (vt->getElementType()->getSize() == 8) op = WasmSIMDOpcode::I64x2Add;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VSub: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::I32x4Sub;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->getSize() == 1) op = WasmSIMDOpcode::I8x16Sub;
                        else if (vt->getElementType()->getSize() == 2) op = WasmSIMDOpcode::I16x8Sub;
                        else if (vt->getElementType()->getSize() == 4) op = WasmSIMDOpcode::I32x4Sub;
                        else if (vt->getElementType()->getSize() == 8) op = WasmSIMDOpcode::I64x2Sub;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VMul: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::I32x4Mul;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->getSize() == 2) op = WasmSIMDOpcode::I16x8Mul;
                        else if (vt->getElementType()->getSize() == 4) op = WasmSIMDOpcode::I32x4Mul;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VFAdd: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::F32x4Add;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->isDoubleTy()) op = WasmSIMDOpcode::F64x2Add;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VFSub: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::F32x4Sub;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->isDoubleTy()) op = WasmSIMDOpcode::F64x2Sub;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VFMul: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::F32x4Mul;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->isDoubleTy()) op = WasmSIMDOpcode::F64x2Mul;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VFDiv: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    WasmSIMDOpcode op = WasmSIMDOpcode::F32x4Div;
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(i.getType())) {
                        if (vt->getElementType()->isDoubleTy()) op = WasmSIMDOpcode::F64x2Div;
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(op));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VAnd:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128And));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::VOr:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128Or));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::VXor:
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128Xor));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                case ir::Instruction::VBroadcast: {
                    pushOperand(i.getOperands()[0]->get());
                    auto* opTy = i.getOperands()[0]->get()->getType();
                    WasmSIMDOpcode splatOp = WasmSIMDOpcode::I32x4Splat;
                    if (opTy && opTy->isFloatTy()) splatOp = WasmSIMDOpcode::F32x4Splat;
                    else if (opTy && opTy->isDoubleTy()) splatOp = WasmSIMDOpcode::F64x2Splat;
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(splatOp));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VExtract: {
                    pushOperand(i.getOperands()[0]->get());
                    uint32_t lane = 0;
                    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
                        lane = static_cast<uint32_t>(ci->getValue());
                    }
                    auto* retTy = i.getType();
                    WasmSIMDOpcode extOp = WasmSIMDOpcode::I32x4ExtractLane;
                    if (retTy && retTy->isFloatTy()) extOp = WasmSIMDOpcode::F32x4ExtractLane;
                    else if (retTy && retTy->isDoubleTy()) extOp = WasmSIMDOpcode::F64x2ExtractLane;
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(extOp, lane));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VInsert: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    uint32_t lane = 0;
                    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                        lane = static_cast<uint32_t>(ci->getValue());
                    }
                    auto* valTy = i.getOperands()[1]->get()->getType();
                    WasmSIMDOpcode insOp = WasmSIMDOpcode::I32x4ReplaceLane;
                    if (valTy && valTy->isFloatTy()) insOp = WasmSIMDOpcode::F32x4ReplaceLane;
                    else if (valTy && valTy->isDoubleTy()) insOp = WasmSIMDOpcode::F64x2ReplaceLane;
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(insOp, lane));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VCmp: {
                    pushOperand(i.getOperands()[0]->get());
                    pushOperand(i.getOperands()[1]->get());
                    ir::VectorCompareOp pred = ir::VectorCompareOp::EQ;
                    if (i.getOperands().size() > 2) {
                        if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                            pred = static_cast<ir::VectorCompareOp>(ci->getValue());
                        }
                    }
                    auto* opTy = i.getOperands()[0]->get()->getType();
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(opTy)) opTy = vt->getElementType();

                    WasmSIMDOpcode cmpOp = WasmSIMDOpcode::I32x4Eq;
                    if (opTy && opTy->isFloatTy()) {
                        switch (pred) {
                            case ir::VectorCompareOp::EQ: cmpOp = WasmSIMDOpcode::F32x4Eq; break;
                            case ir::VectorCompareOp::NE: cmpOp = WasmSIMDOpcode::F32x4Ne; break;
                            case ir::VectorCompareOp::LT: cmpOp = WasmSIMDOpcode::F32x4Lt; break;
                            case ir::VectorCompareOp::LE: cmpOp = WasmSIMDOpcode::F32x4Le; break;
                            case ir::VectorCompareOp::GT: cmpOp = WasmSIMDOpcode::F32x4Gt; break;
                            case ir::VectorCompareOp::GE: cmpOp = WasmSIMDOpcode::F32x4Ge; break;
                            default: cmpOp = WasmSIMDOpcode::F32x4Eq; break;
                        }
                    } else if (opTy && opTy->isDoubleTy()) {
                        switch (pred) {
                            case ir::VectorCompareOp::EQ: cmpOp = WasmSIMDOpcode::F64x2Eq; break;
                            case ir::VectorCompareOp::NE: cmpOp = WasmSIMDOpcode::F64x2Ne; break;
                            case ir::VectorCompareOp::LT: cmpOp = WasmSIMDOpcode::F64x2Lt; break;
                            case ir::VectorCompareOp::LE: cmpOp = WasmSIMDOpcode::F64x2Le; break;
                            case ir::VectorCompareOp::GT: cmpOp = WasmSIMDOpcode::F64x2Gt; break;
                            case ir::VectorCompareOp::GE: cmpOp = WasmSIMDOpcode::F64x2Ge; break;
                            default: cmpOp = WasmSIMDOpcode::F64x2Eq; break;
                        }
                    } else {
                        switch (pred) {
                            case ir::VectorCompareOp::EQ: cmpOp = WasmSIMDOpcode::I32x4Eq; break;
                            case ir::VectorCompareOp::NE: cmpOp = WasmSIMDOpcode::I32x4Ne; break;
                            case ir::VectorCompareOp::LT: cmpOp = WasmSIMDOpcode::I32x4LtS; break;
                            case ir::VectorCompareOp::LE: cmpOp = WasmSIMDOpcode::I32x4LeS; break;
                            case ir::VectorCompareOp::GT: cmpOp = WasmSIMDOpcode::I32x4GtS; break;
                            case ir::VectorCompareOp::GE: cmpOp = WasmSIMDOpcode::I32x4GeS; break;
                            case ir::VectorCompareOp::ULT: cmpOp = WasmSIMDOpcode::I32x4LtU; break;
                            case ir::VectorCompareOp::ULE: cmpOp = WasmSIMDOpcode::I32x4LeU; break;
                            case ir::VectorCompareOp::UGT: cmpOp = WasmSIMDOpcode::I32x4GtU; break;
                            case ir::VectorCompareOp::UGE: cmpOp = WasmSIMDOpcode::I32x4GeU; break;
                            default: cmpOp = WasmSIMDOpcode::I32x4Eq; break;
                        }
                    }
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(cmpOp));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;
                }

                case ir::Instruction::VSelect:
                    pushOperand(i.getOperands()[1]->get());
                    pushOperand(i.getOperands()[2]->get());
                    pushOperand(i.getOperands()[0]->get());
                    wasmFunc.body.push_back(WasmInstruction::makeSIMD(WasmSIMDOpcode::V128Bitselect));
                    if (localIndices.count(&i)) {
                        wasmFunc.body.push_back(WasmInstruction::makeLocalSet(localIndices.at(&i)));
                    }
                    break;

                default:
                    break;
            }
        };

        auto findTargetMerge = [&](const ir::BasicBlock* b1, const ir::BasicBlock* b2) -> const ir::BasicBlock* {
            if (!b1 || !b2) return nullptr;
            const ir::BasicBlock* term1 = nullptr;
            const ir::BasicBlock* term2 = nullptr;
            for (auto& instPtr : b1->getInstructions()) {
                if (instPtr->getOpcode() == ir::Instruction::Jmp && !instPtr->getOperands().empty()) {
                    term1 = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[0]->get());
                }
            }
            for (auto& instPtr : b2->getInstructions()) {
                if (instPtr->getOpcode() == ir::Instruction::Jmp && !instPtr->getOperands().empty()) {
                    term2 = dynamic_cast<const ir::BasicBlock*>(instPtr->getOperands()[0]->get());
                }
            }
            if (term1 && term1 == term2) return term1;
            return nullptr;
        };

        auto isExitBlockForAnyLoop = [&](const ir::BasicBlock* targetBB) -> bool {
            for (int idx = static_cast<int>(scopeStack.size()) - 1; idx >= 0; --idx) {
                if (scopeStack[idx].kind == Scope::Block && scopeStack[idx].targetBB == targetBB) {
                    return true;
                }
            }
            return false;
        };

        std::function<void(const ir::BasicBlock*)> lowerBB = [&](const ir::BasicBlock* bb) {
            if (!bb) return;
            if (processedBBs.count(bb)) return;

            bool isLoopHeader = naturalLoops.count(bb) > 0;
            const LoopInfo* loopPtr = isLoopHeader ? &naturalLoops[bb] : nullptr;

            if (isLoopHeader && !activeDFS.count(bb)) {
                for (int eIdx = static_cast<int>(loopPtr->exitBBs.size()) - 1; eIdx >= 0; --eIdx) {
                    scopeStack.push_back({Scope::Block, loopPtr->exitBBs[eIdx]});
                    wasmFunc.body.push_back(WasmInstruction::makeBlock());
                }
                scopeStack.push_back({Scope::Loop, bb});
                wasmFunc.body.push_back(WasmInstruction::makeLoop());
            }

            processedBBs.insert(bb);
            activeDFS.insert(bb);

            for (auto& instPtr : bb->getInstructions()) {
                ir::Instruction& i = *instPtr;

                if (i.getOpcode() == ir::Instruction::Jnz || i.getOpcode() == ir::Instruction::Br) {
                    if (i.getOperands().size() >= 3) {
                        auto* trueBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[1]->get());
                        auto* falseBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[2]->get());

                        if (isExitBlockForAnyLoop(trueBB) || isExitBlockForAnyLoop(falseBB)) {
                            pushOperand(i.getOperands()[0]->get());
                            if (isExitBlockForAnyLoop(trueBB)) {
                                handlePhiAssignments(bb, trueBB);
                                uint32_t depth = getScopeDepth(trueBB);
                                wasmFunc.body.push_back(WasmInstruction::makeBrIf(depth));
                                if (falseBB) {
                                    handlePhiAssignments(bb, falseBB);
                                    lowerBB(falseBB);
                                }
                            } else {
                                wasmFunc.body.push_back(WasmInstruction::makeSimple(WasmOpcode::I32Eqz));
                                handlePhiAssignments(bb, falseBB);
                                uint32_t depth = getScopeDepth(falseBB);
                                wasmFunc.body.push_back(WasmInstruction::makeBrIf(depth));
                                if (trueBB) {
                                    handlePhiAssignments(bb, trueBB);
                                    lowerBB(trueBB);
                                }
                            }
                            continue;
                        }

                        pushOperand(i.getOperands()[0]->get());
                        scopeStack.push_back({Scope::If, nullptr});
                        wasmFunc.body.push_back(WasmInstruction::makeIf());

                        const ir::BasicBlock* mergeBB = findTargetMerge(trueBB, falseBB);
                        const ir::BasicBlock* oldMerge = currentMergeBB;
                        if (mergeBB) currentMergeBB = mergeBB;

                        if (trueBB) {
                            handlePhiAssignments(bb, trueBB);
                            lowerBB(trueBB);
                        }

                        if (falseBB && falseBB != trueBB) {
                            wasmFunc.body.push_back(WasmInstruction::makeElse());
                            handlePhiAssignments(bb, falseBB);
                            lowerBB(falseBB);
                        }

                        scopeStack.pop_back();
                        wasmFunc.body.push_back(WasmInstruction::makeEnd());
                        currentMergeBB = oldMerge;

                        if (mergeBB && !processedBBs.count(mergeBB)) {
                            lowerBB(mergeBB);
                        }
                        continue;
                    } else if (i.getOperands().size() == 1) {
                        auto* targetBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[0]->get());
                        if (targetBB) {
                            handlePhiAssignments(bb, targetBB);
                            if (activeDFS.count(targetBB) || isExitBlockForAnyLoop(targetBB)) {
                                uint32_t depth = getScopeDepth(targetBB);
                                wasmFunc.body.push_back(WasmInstruction::makeBr(depth));
                            } else if (targetBB != currentMergeBB) {
                                lowerBB(targetBB);
                            }
                        }
                        continue;
                    }
                } else if (i.getOpcode() == ir::Instruction::Jmp) {
                    if (!i.getOperands().empty()) {
                        auto* targetBB = dynamic_cast<const ir::BasicBlock*>(i.getOperands()[0]->get());
                        if (targetBB) {
                            handlePhiAssignments(bb, targetBB);
                            if (activeDFS.count(targetBB) || isExitBlockForAnyLoop(targetBB)) {
                                uint32_t depth = getScopeDepth(targetBB);
                                wasmFunc.body.push_back(WasmInstruction::makeBr(depth));
                            } else if (targetBB != currentMergeBB) {
                                lowerBB(targetBB);
                            }
                        }
                    }
                    continue;
                }

                processInstruction(i, bb);
            }

            activeDFS.erase(bb);

            if (isLoopHeader) {
                wasmFunc.body.push_back(WasmInstruction::makeEnd()); // end loop
                scopeStack.pop_back();
                for (const ir::BasicBlock* exitBB : loopPtr->exitBBs) {
                    wasmFunc.body.push_back(WasmInstruction::makeEnd()); // end block
                    scopeStack.pop_back();
                    if (!processedBBs.count(exitBB)) {
                        lowerBB(exitBB);
                    }
                }
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
            std::string tStr = "i32";
            if (p == WasmValType::I64) tStr = "i64";
            else if (p == WasmValType::F32) tStr = "f32";
            else if (p == WasmValType::F64) tStr = "f64";
            else if (p == WasmValType::V128) tStr = "v128";
            ss << " (param " << tStr << ")";
        }
        for (auto r : func.type.results) {
            std::string tStr = "i32";
            if (r == WasmValType::I64) tStr = "i64";
            else if (r == WasmValType::F32) tStr = "f32";
            else if (r == WasmValType::F64) tStr = "f64";
            else if (r == WasmValType::V128) tStr = "v128";
            ss << " (result " << tStr << ")";
        }
        ss << "\n";

        for (const auto& loc : func.locals) {
            std::string typeStr = "i32";
            if (loc.type == WasmValType::I64) typeStr = "i64";
            else if (loc.type == WasmValType::F32) typeStr = "f32";
            else if (loc.type == WasmValType::F64) typeStr = "f64";
            else if (loc.type == WasmValType::V128) typeStr = "v128";
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
                case WasmOpcode::Drop: ss << "    drop\n"; break;
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
                case WasmOpcode::SIMDPrefix: {
                    switch (inst.simdOpcode) {
                        case WasmSIMDOpcode::V128Load: ss << "    v128.load\n"; break;
                        case WasmSIMDOpcode::V128Store: ss << "    v128.store\n"; break;
                        case WasmSIMDOpcode::I32x4Add: ss << "    i32x4.add\n"; break;
                        case WasmSIMDOpcode::I32x4Sub: ss << "    i32x4.sub\n"; break;
                        case WasmSIMDOpcode::I32x4Mul: ss << "    i32x4.mul\n"; break;
                        case WasmSIMDOpcode::F32x4Add: ss << "    f32x4.add\n"; break;
                        case WasmSIMDOpcode::F32x4Sub: ss << "    f32x4.sub\n"; break;
                        case WasmSIMDOpcode::F32x4Mul: ss << "    f32x4.mul\n"; break;
                        case WasmSIMDOpcode::F32x4Div: ss << "    f32x4.div\n"; break;
                        case WasmSIMDOpcode::V128And: ss << "    v128.and\n"; break;
                        case WasmSIMDOpcode::V128Or: ss << "    v128.or\n"; break;
                        case WasmSIMDOpcode::V128Xor: ss << "    v128.xor\n"; break;
                        case WasmSIMDOpcode::I32x4Splat: ss << "    i32x4.splat\n"; break;
                        case WasmSIMDOpcode::I32x4ExtractLane: ss << "    i32x4.extract_lane " << inst.uintImm << "\n"; break;
                        case WasmSIMDOpcode::I32x4ReplaceLane: ss << "    i32x4.replace_lane " << inst.uintImm << "\n"; break;
                        case WasmSIMDOpcode::I32x4Eq: ss << "    i32x4.eq\n"; break;
                        case WasmSIMDOpcode::F32x4Eq: ss << "    f32x4.eq\n"; break;
                        case WasmSIMDOpcode::F32x4Lt: ss << "    f32x4.lt\n"; break;
                        case WasmSIMDOpcode::F32x4Gt: ss << "    f32x4.gt\n"; break;
                        case WasmSIMDOpcode::F64x2Add: ss << "    f64x2.add\n"; break;
                        case WasmSIMDOpcode::F64x2Mul: ss << "    f64x2.mul\n"; break;
                        case WasmSIMDOpcode::V128Bitselect: ss << "    v128.bitselect\n"; break;
                        default: ss << "    v128.simd_op\n"; break;
                    }
                    break;
                }
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
                    case WasmOpcode::SIMDPrefix:
                        encodeUnsignedLeb(body, static_cast<uint32_t>(inst.simdOpcode));
                        if (inst.simdOpcode == WasmSIMDOpcode::V128Load || inst.simdOpcode == WasmSIMDOpcode::V128Store) {
                            encodeUnsignedLeb(body, 0); // align
                            encodeUnsignedLeb(body, 0); // offset
                        } else if (inst.simdOpcode == WasmSIMDOpcode::I32x4ExtractLane || inst.simdOpcode == WasmSIMDOpcode::I32x4ReplaceLane ||
                                   inst.simdOpcode == WasmSIMDOpcode::F32x4ExtractLane || inst.simdOpcode == WasmSIMDOpcode::F32x4ReplaceLane) {
                            body.push_back(static_cast<uint8_t>(inst.uintImm)); // lane index
                        }
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
