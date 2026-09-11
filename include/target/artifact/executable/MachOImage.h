#pragma once

#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/LinkedImage.h"

#include <string>

namespace target::artifact::linker {

class MachOImageWriter {
public:
    bool writeExecutable(const LinkedImage& image, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

class MachOExecutableImageBuilder {
public:
    bool build(const LinkedImage& image, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace target::artifact::linker
