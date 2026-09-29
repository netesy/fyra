#pragma once

#include "target/artifact/linker/LinkedImage.h"
#include <string>
#include <vector>

namespace target::artifact::executable {

class FlatBinaryWriter {
public:
    bool write(const linker::LinkedImage& image, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace target::artifact::executable
