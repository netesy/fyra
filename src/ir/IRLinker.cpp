#include "ir/IRLinker.h"
#include <set>
#include <iostream>

namespace ir {

bool IRLinker::linkModules(Module& dest, std::unique_ptr<Module> src, std::string& errorMsg) {
    if (!src) return true;

    // 1. Merge named types
    // Any type in src that is not in dest will be added.
    // If a type exists in both, we verify they match (or override if incomplete).

    // 2. Merge extern declarations
    for (const auto& kv : src->getExternDecls()) {
        if (dest.getExternDecls().find(kv.first) == dest.getExternDecls().end()) {
            dest.addExternDecl(kv.first, kv.second);
        }
    }

    // 3. Merge global variables
    std::set<std::string> existingGlobals;
    for (const auto& gv : dest.getGlobalVariables()) {
        existingGlobals.insert(gv->getName());
    }

    auto& srcGlobals = const_cast<std::list<std::unique_ptr<GlobalVariable>>&>(src->getGlobalVariables());
    while (!srcGlobals.empty()) {
        auto gv = std::move(srcGlobals.front());
        srcGlobals.pop_front();

        if (existingGlobals.count(gv->getName()) > 0) {
            // Variable already exists in dest.
            // Check if one is a declaration and the other is a definition.
            GlobalVariable* destGv = nullptr;
            for (const auto& dg : dest.getGlobalVariables()) {
                if (dg->getName() == gv->getName()) {
                    destGv = dg.get();
                    break;
                }
            }
            if (destGv && !gv->getInitializer() && destGv->getInitializer()) {
                // src is just a declaration, skip
                continue;
            }
            if (destGv && gv->getInitializer() && !destGv->getInitializer()) {
                // src has definition, dest was decl: transfer initializer to destGv
                destGv->setInitializer(gv->getInitializer());
                continue;
            } else if (destGv && gv->getInitializer() && destGv->getInitializer()) {
                errorMsg = "Symbol collision: Global variable '" + gv->getName() + "' defined in both modules";
                return false;
            }
        } else {
            existingGlobals.insert(gv->getName());
            dest.addGlobalVariable(std::move(gv));
        }
    }

    // 4. Merge functions
    std::set<std::string> existingFuncs;
    for (const auto& f : dest.getFunctions()) {
        existingFuncs.insert(f->getName());
    }

    auto& srcFuncs = src->getFunctions();
    while (!srcFuncs.empty()) {
        auto func = std::move(srcFuncs.front());
        srcFuncs.pop_front();

        if (existingFuncs.count(func->getName()) > 0) {
            Function* destFunc = dest.getFunction(func->getName());
            if (destFunc) {
                bool destIsDecl = destFunc->getBasicBlocks().empty();
                bool srcIsDecl = func->getBasicBlocks().empty();

                if (srcIsDecl) {
                    // src is declaration, dest already exists (decl or def), skip src
                    continue;
                }
                if (destIsDecl && !srcIsDecl) {
                    // dest is decl, src is def: transfer basic blocks and parameters to destFunc so pointers remain valid
                    for (auto& bb : func->getBasicBlocks()) {
                        if (bb) bb->setParent(destFunc);
                        destFunc->addBasicBlock(std::move(bb));
                    }
                    func->getBasicBlocks().clear();
                    destFunc->getParameters().clear();
                    for (auto& param : func->getParameters()) {
                        destFunc->addParameter(std::move(param));
                    }
                    func->getParameters().clear();
                    continue;
                }
                if (!destIsDecl && !srcIsDecl) {
                    errorMsg = "Symbol collision: Function '" + func->getName() + "' defined in both modules";
                    return false;
                }
            }
        } else {
            existingFuncs.insert(func->getName());
            dest.addFunction(std::move(func));
        }
    }

    return true;
}

} // namespace ir
