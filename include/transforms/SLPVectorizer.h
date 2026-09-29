#pragma once

#include "transforms/TransformPass.h"
#include "ir/Instruction.h"
#include "target/core/TargetDescriptor.h"
#include <memory>
#include <vector>

namespace ir { class VectorType; }

namespace transforms {

// A deliberately local superword-level vectorizer.  Plans are built without
// mutating IR and are committed only after legality and target checks pass.
class SLPVectorizer : public TransformPass {
public:
    struct SLPPack {
        std::vector<ir::Instruction*> lanes;
        ir::VectorType* type = nullptr;
        ir::Instruction::Opcode vectorOpcode = ir::Instruction::VAdd;
        unsigned widthBits = 0;
    };

    struct SLPMemoryPack {
        ir::Value* base = nullptr;
        int64_t firstOffset = 0;
        int64_t elementSize = 0;
        unsigned lanes = 0;
        bool isLoad = false;
        bool isStore = false;
        ir::Type* elementType = nullptr;
        std::vector<ir::Instruction*> scalarAccesses;
    };

    explicit SLPVectorizer(std::shared_ptr<ErrorReporter> reporter = nullptr,
                           target::TargetDescriptor target = {target::Arch::X64, target::OS::Linux})
        : TransformPass("SLPVectorizer", reporter), target_(std::move(target)) {}

    bool performTransformation(ir::Function& func) override;

private:
    target::TargetDescriptor target_;
};

} // namespace transforms
