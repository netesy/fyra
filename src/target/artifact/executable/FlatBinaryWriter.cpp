#include "target/artifact/executable/FlatBinaryWriter.h"
#include <fstream>
#include <algorithm>
#include <vector>

namespace target::artifact::executable {

bool FlatBinaryWriter::write(const linker::LinkedImage& image, const std::string& outputPath) {
    lastError_.clear();

    std::vector<const linker::LinkedSection*> orderedSections;
    for (const auto& [name, section] : image.sections) {
        if (section.name == ".bss") continue; // .bss is uninitialized zero memory
        orderedSections.push_back(&section);
    }

    std::sort(orderedSections.begin(), orderedSections.end(), [](const auto* a, const auto* b) {
        return a->virtualAddress < b->virtualAddress;
    });

    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        lastError_ = "Cannot open flat binary output file: " + outputPath;
        return false;
    }

    uint64_t currentAddr = orderedSections.empty() ? 0 : orderedSections.front()->virtualAddress;

    for (const auto* section : orderedSections) {
        if (section->data.empty()) continue;

        if (section->virtualAddress > currentAddr) {
            uint64_t gap = section->virtualAddress - currentAddr;
            std::vector<uint8_t> zeros(gap, 0);
            file.write(reinterpret_cast<const char*>(zeros.data()), zeros.size());
        }

        file.write(reinterpret_cast<const char*>(section->data.data()), section->data.size());
        currentAddr = section->virtualAddress + section->data.size();
    }

    file.close();
    return !file.fail();
}

} // namespace target::artifact::executable
