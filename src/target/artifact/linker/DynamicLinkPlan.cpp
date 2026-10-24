#include "target/artifact/linker/DynamicLinkPlan.h"
#include <unordered_set>

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

    // The linker records every import it actually bound - the pre-declared ones
    // plus any Windows system import it classified from an undefined reference -
    // in image.importLibraryNames keyed by the canonical export name. Fold those
    // into the plan so a self-contained image emits a complete import table even
    // though the caller never pre-declared the CRT/Win32 symbols.
    std::unordered_set<std::string> dataSymbols;
    for (const auto& fixup : image.dataImportFixups) dataSymbols.insert(fixup.symbolName);
    std::unordered_set<std::string> declared;
    for (const auto& imp : plan.imports) declared.insert(imp.symbol);
    for (const auto& [name, dll] : image.importLibraryNames) {
        if (declared.count(name)) continue;
        DynamicImport imp;
        imp.symbol = name;
        imp.dependencyLibrary = dll;
        imp.kind = dataSymbols.count(name) ? DynamicImportKind::Data : DynamicImportKind::Function;
        plan.imports.push_back(imp);
        declared.insert(name);
        bool hasDep = false;
        for (const auto& dep : plan.dependencies) {
            if (dep.libraryName == dll) { hasDep = true; break; }
        }
        if (!hasDep) plan.dependencies.push_back({dll});
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
