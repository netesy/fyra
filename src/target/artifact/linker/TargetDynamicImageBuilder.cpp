#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/executable/PeImage.h"
#include "target/artifact/executable/ElfImage.h"

namespace target {
namespace artifact {
namespace linker {



std::unique_ptr<TargetDynamicImageBuilder> TargetDynamicImageBuilder::createForTarget(target::Arch arch, target::OS os) {
    if (arch == target::Arch::X64 && os == target::OS::Linux) {
        return std::make_unique<ElfDynamicImageBuilder>();
    } else if (os == target::OS::Windows) {
        return std::make_unique<PeDynamicImageBuilder>();
    } else if (os == target::OS::MacOS) {
        return std::make_unique<MachODynamicImageBuilder>();
    }
    return nullptr;
}

bool ElfDynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    ElfImageWriter writer;
    if (!writer.writeSharedLibrary(plan, outputPath)) {
        lastError_ = writer.getLastError();
        return false;
    }
    lastError_.clear();
    return true;
}

bool PeDynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    if (plan.os != target::OS::Windows) {
        lastError_ = "shared-library output target OS mismatch for PE builder";
        return false;
    }
    if (plan.arch != target::Arch::X64 || plan.outputKind != LinkOutputKind::SharedLibrary) {
        lastError_ = "PE shared-library builder requires a Windows x64 shared-library plan";
        return false;
    }
    if (!plan.imports.empty() || !plan.dependencies.empty() || !plan.relocations.empty()) {
        lastError_ = "PE imports, dependencies, and dynamic relocations are not implemented";
        return false;
    }
    std::string imageName = outputPath;
    const size_t slash = imageName.find_last_of("/\\");
    if (slash != std::string::npos) imageName.erase(0, slash + 1);
    auto image = executable::createPeImageFromDynamicPlan(plan, imageName);
    executable::PeImageWriter writer;
    if (!writer.write(std::move(image), outputPath)) {
        lastError_ = writer.getLastError();
        return false;
    }
    return true;
}

bool MachODynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    (void)outputPath;
    if (plan.os != target::OS::MacOS) {
        lastError_ = "shared-library output target OS mismatch for Mach-O builder";
        return false;
    }
    lastError_ = "shared-library output not implemented for target: macOS/Mach-O";
    return false;
}

} // namespace linker
} // namespace artifact
} // namespace target
