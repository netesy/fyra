#include "target/artifact/linker/DynamicLinkPlan.h"

namespace target {
namespace artifact {
namespace linker {

DynamicLinkPlan DynamicLinkPlan::createFromLinkedImage(const LinkedImage& image,
                                                        const std::vector<DynamicImport>& dynamicImports) {
    DynamicLinkPlan plan;
    plan.arch = image.arch;
    plan.os = image.os;
    plan.outputKind = image.outputKind;
    plan.sections = image.sections;
    plan.entryAddress = image.entryAddress;
    plan.entrySymbolName = image.entrySymbolName;
    plan.relocationFixupVmas = image.relocationFixupVmas;
    plan.importThunkVmas = image.importThunkVmas;
    plan.dataImportFixups = image.dataImportFixups;

    for (const auto& [name, sym] : image.symbols) {
        if (sym.isGlobal) {
            DynamicExport exp;
            exp.symbol = sym.name;
            exp.isFunction = sym.isFunction;
            exp.address = sym.virtualAddress;
            exp.size = sym.size;
            exp.sectionName = sym.sectionName;
            plan.exports.push_back(exp);
        }
    }

    for (const auto& imp : dynamicImports) {
        plan.imports.push_back(imp);

        bool hasDep = false;
        for (const auto& dep : plan.dependencies) {
            if (dep.libraryName == imp.dependencyLibrary) {
                hasDep = true;
                break;
            }
        }
        if (!hasDep) {
            plan.dependencies.push_back({imp.dependencyLibrary});
        }
    }

    return plan;
}

const LinkedSection* DynamicLinkPlan::findSection(const std::string& name) const {
    auto it = sections.find(name);
    return (it != sections.end()) ? &it->second : nullptr;
}

} // namespace linker
} // namespace artifact
} // namespace target
