#include "target/core/ArchitectureInfo.h"
#include "ir/Constant.h"
#include <cstring>

namespace target {

std::string ArchitectureInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

std::string ArchitectureInfo::formatConstant(const ir::ConstantFP* C) const {
    uint64_t bits = 0;
    double val = C->getValue();
    std::memcpy(&bits, &val, sizeof(double));
    return getImmediatePrefix() + std::to_string(bits);
}

}
