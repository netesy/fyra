#include "fyra/CompilerPipeline.h"
#include "ir/Validator.h"
#include "ir/PhiNode.h"
#include "ir/SIMDInstruction.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/DominanceFrontier.h"
#include "transforms/PhiInsertion.h"
#include "transforms/SSARenamer.h"
#include "transforms/Mem2Reg.h"
#include "transforms/FunctionInliner.h"
#include "transforms/DivisionStrengthReduction.h"
#include "transforms/SCCP.h"
#include "transforms/CopyElimination.h"
#include "transforms/GVN.h"
#include "transforms/ControlFlowSimplification.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/ScalarEvolution.h"
#include "transforms/LoopVectorizer.h"
#include "transforms/SLPVectorizer.h"
#include "transforms/LoopUnroll.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/ErrorReporter.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetDescriptor.h"
#include "target/core/TargetInfo.h"
#include <iostream>
#include <map>

namespace fyra {

std::unique_ptr<ir::Module> cloneModule(const ir::Module& srcModule) {
    auto newModule = std::make_unique<ir::Module>(srcModule.getName(), srcModule.getContextShared());
    newModule->setSourceFilename(srcModule.getSourceFilename());

    for (const auto& pair : srcModule.getExternDecls()) {
        newModule->addExternDecl(pair.first, pair.second);
    }

    std::map<const ir::Value*, ir::Value*> valueMap;

    // 1. Clone Global Variables
    for (const auto& gv : srcModule.getGlobalVariables()) {
        auto newGv = std::make_unique<ir::GlobalVariable>(gv->getType(), gv->getName(), gv->getInitializer(), gv->isThreadLocal(), gv->getSection());
        valueMap[gv.get()] = newGv.get();
        newModule->addGlobalVariable(std::move(newGv));
    }

    // 2. First Pass for Functions: signatures, parameters, basic blocks
    for (const auto& fn : srcModule.getFunctions()) {
        auto newFn = std::make_unique<ir::Function>(fn->getType(), fn->getName(), newModule.get());
        newFn->setVariadic(fn->isVariadic());
        newFn->setExported(fn->isExported());
        valueMap[fn.get()] = newFn.get();

        for (const auto& param : fn->getParameters()) {
            auto newParam = std::make_unique<ir::Parameter>(param->getType(), param->getName());
            valueMap[param.get()] = newParam.get();
            newFn->addParameter(std::move(newParam));
        }

        for (const auto& bb : fn->getBasicBlocks()) {
            auto newBb = std::make_unique<ir::BasicBlock>(newFn.get(), bb->getName());
            valueMap[bb.get()] = newBb.get();
            newFn->getBasicBlocks().push_back(std::move(newBb));
        }

        newModule->addFunction(std::move(newFn));
    }

    // 3. Second Pass: clone instructions and populate initial operands
    for (const auto& fn : srcModule.getFunctions()) {
        ir::Function* newFn = newModule->getFunction(fn->getName());
        if (!newFn) continue;

        auto oldBbIt = fn->getBasicBlocks().begin();
        auto newBbIt = newFn->getBasicBlocks().begin();

        for (; oldBbIt != fn->getBasicBlocks().end() && newBbIt != newFn->getBasicBlocks().end(); ++oldBbIt, ++newBbIt) {
            ir::BasicBlock* oldBb = oldBbIt->get();
            ir::BasicBlock* newBb = newBbIt->get();

            for (const auto& instOwner : oldBb->getInstructions()) {
                ir::Instruction* oldInst = instOwner.get();

                std::vector<ir::Value*> newOps;
                for (const auto& opUse : oldInst->getOperands()) {
                    if (!opUse) {
                        newOps.push_back(nullptr);
                        continue;
                    }
                    ir::Value* oldVal = opUse->get();
                    ir::Value* newVal = (oldVal && valueMap.count(oldVal)) ? valueMap[oldVal] : oldVal;
                    newOps.push_back(newVal);
                }

                if (auto* oldPhi = dynamic_cast<const ir::PhiNode*>(oldInst)) {
                    ir::Instruction* allocVar = nullptr;
                    if (oldPhi->getVariable() && valueMap.count(oldPhi->getVariable())) {
                        allocVar = dynamic_cast<ir::Instruction*>(valueMap[oldPhi->getVariable()]);
                    }
                    auto phiOwner = std::make_unique<ir::PhiNode>(oldPhi->getType(), static_cast<unsigned>(newOps.size()), allocVar, newBb);
                    for (size_t i = 0; i + 1 < newOps.size(); i += 2) {
                        auto* predBb = dynamic_cast<ir::BasicBlock*>(newOps[i]);
                        ir::Value* val = newOps[i + 1];
                        if (predBb) {
                            phiOwner->addIncoming(val, predBb);
                        }
                    }
                    valueMap[oldInst] = phiOwner.get();
                    newBb->getInstructions().push_back(std::move(phiOwner));
                } else if (auto* oldVec = dynamic_cast<const ir::VectorInstruction*>(oldInst)) {
                    auto vecOwner = std::make_unique<ir::VectorInstruction>(oldVec->getType(), oldVec->getOpcode(), newOps, oldVec->getVectorWidth(), newBb);
                    if (oldVec->getShuffleMask()) {
                        vecOwner->setShuffleMask(*oldVec->getShuffleMask());
                    }
                    valueMap[oldInst] = vecOwner.get();
                    newBb->getInstructions().push_back(std::move(vecOwner));
                } else if (auto* oldSys = dynamic_cast<const ir::SyscallInstruction*>(oldInst)) {
                    auto sysOwner = std::make_unique<ir::SyscallInstruction>(oldSys->getType(), newOps, oldSys->getSyscallId(), newBb);
                    valueMap[oldInst] = sysOwner.get();
                    newBb->getInstructions().push_back(std::move(sysOwner));
                } else if (auto* oldExt = dynamic_cast<const ir::ExternCallInstruction*>(oldInst)) {
                    auto extOwner = std::make_unique<ir::ExternCallInstruction>(oldExt->getType(), newOps, oldExt->getCapability(), newBb);
                    valueMap[oldInst] = extOwner.get();
                    newBb->getInstructions().push_back(std::move(extOwner));
                } else {
                    auto instOwnerNew = std::make_unique<ir::Instruction>(oldInst->getType(), oldInst->getOpcode(), newOps, newBb);
                    valueMap[oldInst] = instOwnerNew.get();
                    newBb->getInstructions().push_back(std::move(instOwnerNew));
                }
            }
        }
    }

    // 4. Third Pass: resolve any forward references in operands
    for (const auto& fn : srcModule.getFunctions()) {
        ir::Function* newFn = newModule->getFunction(fn->getName());
        if (!newFn) continue;

        auto oldBbIt = fn->getBasicBlocks().begin();
        auto newBbIt = newFn->getBasicBlocks().begin();

        for (; oldBbIt != fn->getBasicBlocks().end() && newBbIt != newFn->getBasicBlocks().end(); ++oldBbIt, ++newBbIt) {
            ir::BasicBlock* oldBb = oldBbIt->get();
            ir::BasicBlock* newBb = newBbIt->get();

            auto oldInstIt = oldBb->getInstructions().begin();
            auto newInstIt = newBb->getInstructions().begin();

            for (; oldInstIt != oldBb->getInstructions().end() && newInstIt != newBb->getInstructions().end(); ++oldInstIt, ++newInstIt) {
                ir::Instruction* oldInst = oldInstIt->get();
                ir::Instruction* newInst = newInstIt->get();

                for (size_t i = 0; i < oldInst->getOperands().size(); ++i) {
                    if (!oldInst->getOperands()[i]) continue;
                    ir::Value* oldVal = oldInst->getOperands()[i]->get();
                    if (oldVal && valueMap.count(oldVal)) {
                        if (i < newInst->getOperands().size() && newInst->getOperands()[i]) {
                            newInst->getOperands()[i]->set(valueMap[oldVal]);
                        }
                    }
                }
            }
        }
    }

    return newModule;
}

PipelineResult CompilerPipeline::runValidation(ir::Module& module) {
    PipelineResult res;
    std::vector<std::string> irErrors;
    if (!ir::Validator::validateModule(module, irErrors)) {
        res.success = false;
        res.errors = std::move(irErrors);
    } else {
        res.success = true;
    }
    return res;
}

PipelineResult CompilerPipeline::runSSA(ir::Module& module) {
    PipelineResult res;
    if (isSSAPrepared_) {
        res.success = true;
        return res;
    }

    for (auto& func : module.getFunctions()) {
        if (func->getBasicBlocks().empty()) continue;
        transforms::CFGBuilder::run(*func);
        transforms::DominatorTree domTree; domTree.run(*func);
        transforms::DominanceFrontier domFrontier; domFrontier.run(*func, domTree);
        transforms::PhiInsertion phiInserter; phiInserter.run(*func, domFrontier);
        transforms::SSARenamer ssaRenamer; ssaRenamer.run(*func, domTree);

        transforms::Mem2Reg mem2reg;
        mem2reg.run(*func);
    }

    isSSAPrepared_ = true;
    res.success = true;
    return res;
}

PipelineResult CompilerPipeline::runOptimizations(ir::Module& module, const PipelineConfig& config) {
    PipelineResult res;
    if (isOptimized_) {
        res.success = true;
        return res;
    }

    int optLevel = 2;
    if (config.optLevel == OptimizationLevel::O0) optLevel = 0;
    else if (config.optLevel == OptimizationLevel::O1) optLevel = 1;
    else if (config.optLevel == OptimizationLevel::O2) optLevel = 2;

    if (optLevel > 0) {
        transforms::FunctionInliner inliner;
        inliner.runOnModule(module);
    }

    auto desc = target::TargetDescriptor::fromString(config.targetTriple);
    bool isWasm = (desc && desc->arch == target::Arch::WASM32);

    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);

    for (auto& func : module.getFunctions()) {
        if (func->getBasicBlocks().empty()) continue;
        if (optLevel == 0) continue;

        transforms::SCCP enhanced_sccp(error_reporter);
        transforms::ControlFlowSimplification cfg_simplifier(error_reporter);
        transforms::DeadInstructionElimination enhanced_dce(error_reporter);
        transforms::CopyElimination copy_elim;
        transforms::GVN gvn;
        transforms::LoopInvariantCodeMotion licm(error_reporter);
        transforms::ScalarEvolution scev;
        transforms::LoopUnroll loop_unroll(error_reporter);
        const target::TargetDescriptor vectorTarget = desc.value_or(
            target::TargetDescriptor{target::Arch::X64, target::OS::Linux});
        transforms::LoopVectorizer loop_vectorizer(error_reporter, vectorTarget);
        transforms::SLPVectorizer slp_vectorizer(error_reporter, vectorTarget);
        transforms::DivisionStrengthReduction div_sr(error_reporter);

        bool optimization_changed = true;
        int iteration = 1;
        const int maxIterations = (optLevel >= 2) ? 5 : 2;

        if (!isWasm) {
            while (optimization_changed && iteration <= maxIterations) {
                optimization_changed = false;
                if (div_sr.run(*func)) optimization_changed = true;
                if (enhanced_sccp.run(*func)) optimization_changed = true;
                if (copy_elim.run(*func)) optimization_changed = true;
                if (gvn.run(*func)) optimization_changed = true;
                if (cfg_simplifier.run(*func)) optimization_changed = true;
                if (optLevel >= 2 && licm.run(*func)) optimization_changed = true;
                if (optLevel >= 2 && scev.run(*func)) optimization_changed = true;
                if (optLevel >= 2 && config.enableLoopVectorization && loop_vectorizer.run(*func)) optimization_changed = true;
                if (optLevel >= 2 && config.enableSLP && slp_vectorizer.run(*func)) optimization_changed = true;
                if (optLevel >= 2 && config.enableLoopUnroll && loop_unroll.run(*func)) optimization_changed = true;
                if (enhanced_dce.run(*func)) optimization_changed = true;
                iteration++;
            }
        }
    }

    if (error_reporter->hasCriticalErrors()) {
        res.success = false;
        res.errors.push_back("Critical optimization errors detected");
        return res;
    }

    isOptimized_ = true;
    res.success = true;
    return res;
}

PipelineResult CompilerPipeline::runRegAlloc(ir::Module& module, const PipelineConfig& config) {
    PipelineResult res;
    if (isRegAllocated_) {
        res.success = true;
        return res;
    }

    auto desc = target::TargetDescriptor::fromString(config.targetTriple);
    if (!desc) {
        res.success = false;
        res.errors.push_back("Invalid target triple for regalloc: " + config.targetTriple);
        return res;
    }

    if (desc->arch != target::Arch::WASM32) {
        auto targetInfoForAlloc = target::TargetResolver::resolve(*desc);
        if (!targetInfoForAlloc) {
            res.success = false;
            res.errors.push_back("Could not resolve target info for: " + config.targetTriple);
            return res;
        }
        for (auto& func : module.getFunctions()) {
            if (func->getBasicBlocks().empty()) continue;
            transforms::RegAllocRewriter rewriter;
            rewriter.run(*func, targetInfoForAlloc.get());
        }
    }

    isRegAllocated_ = true;
    res.success = true;
    return res;
}

PipelineResult CompilerPipeline::run(ir::Module& module, const PipelineConfig& config) {
    PipelineResult res;
    if (config.validate) {
        res = runValidation(module);
        if (!res.success) return res;
    }

    res = runSSA(module);
    if (!res.success) return res;

    res = runOptimizations(module, config);
    if (!res.success) return res;

    res = runRegAlloc(module, config);
    return res;
}

} // namespace fyra
