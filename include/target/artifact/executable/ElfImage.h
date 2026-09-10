#pragma once

#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/LinkedImage.h"

#include <string>

namespace target::artifact::linker {

// The single target-specific final-image serializer for ELF executables and
// shared libraries. Linking has already completed before either entry point.
class ElfImageWriter {
public:
    bool writeExecutable(const LinkedImage& image, const std::string& outputPath);
    bool writeSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

class ElfExecutableImageBuilder {
public:
    bool build(const LinkedImage& image, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace target::artifact::linker
