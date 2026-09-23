#pragma once

#include "fyra/CompilerPipeline.h"
#include "target/artifact/linker/InternalLinker.h"
#include <string>
#include <vector>
#include <memory>

namespace fyra {

enum class OutputKind {
    Assembly,
    Object,
    Executable,
    StaticLibrary,
    SharedLibrary,
    WAT,
    Wasm
};

struct BuildResult {
    bool success{false};
    OutputKind kind{OutputKind::Assembly};
    std::string outputPath;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class BackendBuilder {
public:
    explicit BackendBuilder(ir::Module& module);

    BackendBuilder& target(const std::string& triple);
    BackendBuilder& optimize(OptimizationLevel level);

    BackendBuilder& validate(bool enabled = true);
    BackendBuilder& enableSLP(bool enabled = true);
    BackendBuilder& enableLoopVectorization(bool enabled = true);
    BackendBuilder& enableLoopUnroll(bool enabled = true);

    BackendBuilder& addObject(const std::string& path);
    BackendBuilder& addStaticLibrary(const std::string& path);

    BackendBuilder& importSymbol(
        const std::string& symbol,
        const std::string& dependencyLibrary);

    BuildResult emitAssembly(const std::string& path);
    BuildResult emitObject(const std::string& path);

    BuildResult emitWAT(const std::string& path);
    BuildResult emitWasm(const std::string& path);

    BuildResult emitStaticLibrary(const std::string& path);
    BuildResult emitSharedLibrary(const std::string& path);
    BuildResult emitExecutable(const std::string& path);

private:
    ir::Module& srcModule_;
    CompilerPipeline pipeline_;
    PipelineConfig config_;

    std::vector<std::string> inputObjectPaths_;
    std::vector<std::string> inputStaticLibPaths_;
    std::vector<target::artifact::linker::DynamicImport> dynamicImports_;

    std::unique_ptr<ir::Module> preparedModule_;
    bool isPrepared_{false};

    void ensurePrepared(BuildResult& result);
    void invalidatePrepared();
    target::artifact::object::ObjectArtifact buildModuleObjectArtifact(BuildResult& result);
    std::string resolveTargetTriple(const std::string& triple);
};

} // namespace fyra
