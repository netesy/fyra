#include "target/core/ArchitectureInfo.h"
#include "ir/Constant.h"

namespace target {

std::string ArchitectureInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

}
