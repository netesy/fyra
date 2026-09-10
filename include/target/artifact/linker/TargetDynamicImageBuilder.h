#pragma once

#include "target/artifact/linker/LinkedImage.h"
#include <string>
#include <vector>
#include <memory>

namespace target {
namespace artifact {
namespace linker {

class TargetDynamicImageBuilder {
public:
    virtual ~TargetDynamicImageBuilder() = default;

    virtual bool buildSharedLibrary(const LinkedImage& image, const std::string& outputPath) = 0;
    virtual std::string getLastError() const { return lastError_; }

    static std::unique_ptr<TargetDynamicImageBuilder> createForTarget(target::Arch arch, target::OS os);

protected:
    std::string lastError_;
};

class ElfDynamicImageBuilder : public TargetDynamicImageBuilder {
public:
    ElfDynamicImageBuilder() = default;
    ~ElfDynamicImageBuilder() override = default;

    bool buildSharedLibrary(const LinkedImage& image, const std::string& outputPath) override;
};

} // namespace linker
} // namespace artifact
} // namespace target
